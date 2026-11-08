#include "TermScorer.h"

#include "EditDistance.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>

namespace {

constexpr double completion_weight = 0.5;
constexpr std::size_t window_postings = 4096;

struct PrefixRun {
    const std::vector<Posting>* postings;
    double boost;
    bool exact;
};

struct PrefixTally {
    int doc_id;
    double exact_tf = 0;
    double completion_tf = 0;
};

PrefixTally* merge_runs(const PrefixTally* a, const PrefixTally* a_end,
                        const PrefixTally* b, const PrefixTally* b_end, PrefixTally* out) {
    while (a != a_end && b != b_end) {
        if (a->doc_id < b->doc_id) {
            *out++ = *a++;
        } else if (b->doc_id < a->doc_id) {
            *out++ = *b++;
        } else {
            *out++ = {a->doc_id, a->exact_tf + b->exact_tf, a->completion_tf + b->completion_tf};
            ++a;
            ++b;
        }
    }
    out = std::copy(a, a_end, out);
    return std::copy(b, b_end, out);
}

ScoredDocs by_doc(const std::unordered_map<int, double>& scores) {
    ScoredDocs docs;
    docs.reserve(scores.size());
    for (const auto& [doc_id, score] : scores) {
        docs.push_back({doc_id, score});
    }
    std::ranges::sort(docs, {}, &ScoredDoc::doc_id);
    return docs;
}

}

bool ranks_before(const SearchHit& a, const SearchHit& b) {
    if (a.score != b.score) {
        return a.score > b.score;
    }
    return a.doc_id < b.doc_id;
}

void TopKCollector::offer(SearchHit hit) {
    if (worst_on_top.size() < capacity) {
        worst_on_top.push(std::move(hit));
    } else if (capacity > 0 && ranks_before(hit, worst_on_top.top())) {
        worst_on_top.pop();
        worst_on_top.push(std::move(hit));
    }
}

std::vector<SearchHit> TopKCollector::take() {
    std::vector<SearchHit> ranked(worst_on_top.size());
    for (auto it = ranked.rbegin(); it != ranked.rend(); ++it) {
        *it = worst_on_top.top();
        worst_on_top.pop();
    }
    return ranked;
}

double TermScorer::idf(std::size_t doc_freq) const {
    if (scoring.scorer == Scorer::Bm25) {
        const double n = static_cast<double>(doc_freq);
        return std::log(1.0 + (total_docs - n + 0.5) / (n + 0.5));
    }
    return std::log(static_cast<double>(total_docs + 1) / (doc_freq + 1)) + 1.0;
}

double TermScorer::term_idf(const std::string& term) const {
    const std::vector<Posting>* postings = scope.postings(term);
    return postings ? idf(postings->size()) : 0.0;
}

double TermScorer::average_length(std::string_view field) const {
    return scoring.scorer == Scorer::Bm25 ? scope.index.get_average_field_length(field) : 0.0;
}

double TermScorer::weighted_tf(std::string_view field, int doc_id, double frequency, double boost,
                               double average_length) const {
    double tf = frequency * boost;
    if (scoring.scorer == Scorer::Bm25 && average_length > 0.0) {
        const double length = scope.index.get_field_length(field, doc_id);
        tf /= 1.0 - scoring.bm25.b + scoring.bm25.b * length / average_length;
    }
    return tf;
}

double TermScorer::weighted_tf(std::string_view field, int doc_id, double frequency) const {
    return weighted_tf(field, doc_id, frequency, scope.boosts.get(field), average_length(field));
}

double TermScorer::combine(double term_idf, double tf) const {
    return scoring.scorer == Scorer::Bm25 ? term_idf * tf / (scoring.bm25.k1 + tf) : tf * term_idf;
}

ScoredDocs TermScorer::score_by_doc(const std::string& term) const {
    ScoredDocs summed;
    const std::vector<Posting>* postings = scope.postings(term);
    if (!postings) {
        return summed;
    }

    const double idf_of_term = idf(postings->size());

    ScoredDocs merged;
    scope.for_each_field_postings(term, [&](std::string_view field_name, const std::vector<Posting>& field_postings,
                                            double boost) {
        const double field_average = average_length(field_name);
        merged.clear();
        merged.reserve(summed.size() + field_postings.size());
        auto known = summed.begin();
        for (const auto& posting : field_postings) {
            for (; known != summed.end() && known->doc_id < posting.doc_id; ++known) {
                merged.push_back(*known);
            }
            const double tf = weighted_tf(field_name, posting.doc_id, static_cast<double>(posting.term_frequency),
                                          boost, field_average);
            if (known != summed.end() && known->doc_id == posting.doc_id) {
                merged.push_back({posting.doc_id, known->score + tf});
                ++known;
            } else {
                merged.push_back({posting.doc_id, 0.0 + tf});
            }
        }
        merged.insert(merged.end(), known, summed.end());
        summed.swap(merged);
    });
    for (ScoredDoc& doc : summed) {
        doc.score = combine(idf_of_term, doc.score);
    }
    return summed;
}

ScoredDocs TermScorer::score(const std::string& term) const {
    return score_by_doc(term);
}

ScoredDocs TermScorer::score_prefix(const std::string& term, const std::vector<std::string>& completions) const {
    std::vector<PrefixRun> runs;
    auto add_runs = [&](const std::string& expanded, bool exact) {
        scope.for_each_field_postings(expanded, [&](std::string_view, const std::vector<Posting>& postings,
                                                    double boost) {
            runs.push_back({&postings, boost, exact});
        });
    };
    add_runs(term, true);
    for (const std::string& completion : completions) {
        add_runs(completion, false);
    }

    std::vector<std::span<const Posting>> pending;
    std::size_t total_postings = 0;
    for (const PrefixRun& run : runs) {
        pending.emplace_back(*run.postings);
        total_postings += run.postings->size();
    }

    ScoredDocs docs;
    docs.reserve(total_postings);
    std::vector<PrefixTally> tallies;
    std::vector<PrefixTally> scratch;
    std::vector<std::size_t> bounds;
    std::vector<std::size_t> next_bounds;
    for (;;) {
        int window_end = std::numeric_limits<int>::max();
        for (const auto& rest : pending) {
            if (rest.size() > window_postings) {
                window_end = std::min(window_end, rest[window_postings].doc_id);
            }
        }

        bounds.assign(1, 0);
        for (std::size_t run = 0; run < runs.size(); run++) {
            const auto taken = std::ranges::lower_bound(pending[run], window_end, {}, &Posting::doc_id);
            const std::size_t count = taken - pending[run].begin();
            tallies.resize(bounds.back() + count);
            PrefixTally* out = tallies.data() + bounds.back();
            for (const Posting& posting : pending[run].first(count)) {
                const double tf = posting.term_frequency * runs[run].boost;
                *out++ = runs[run].exact ? PrefixTally{posting.doc_id, tf, 0} : PrefixTally{posting.doc_id, 0, tf};
            }
            bounds.push_back(bounds.back() + count);
            pending[run] = pending[run].subspan(count);
        }
        if (bounds.back() == 0) {
            break;
        }

        scratch.resize(tallies.size());
        while (bounds.size() > 2) {
            next_bounds.assign(1, 0);
            PrefixTally* out = scratch.data();
            for (std::size_t run = 0; run + 1 < bounds.size(); run += 2) {
                const PrefixTally* a = tallies.data() + bounds[run];
                const PrefixTally* a_end = tallies.data() + bounds[run + 1];
                if (run + 2 < bounds.size()) {
                    out = merge_runs(a, a_end, a_end, tallies.data() + bounds[run + 2], out);
                } else {
                    out = std::copy(a, a_end, out);
                }
                next_bounds.push_back(out - scratch.data());
            }
            tallies.swap(scratch);
            bounds.swap(next_bounds);
        }

        for (const PrefixTally& tally : std::span(tallies).first(bounds.back())) {
            const double score = tally.exact_tf > 0
                ? tally.exact_tf
                : completion_weight * tally.completion_tf / (tally.completion_tf + 1.0);
            docs.push_back({tally.doc_id, score});
        }
    }

    const double prefix_idf = idf(docs.size());
    for (ScoredDoc& doc : docs) {
        doc.score *= prefix_idf;
    }
    return docs;
}

ScoredDocs TermScorer::score_fuzzy(const std::vector<FuzzyMatch>& candidates, const ScoredDocs& exact) const {
    double exact_floor = std::numeric_limits<double>::infinity();
    for (const ScoredDoc& doc : exact) {
        exact_floor = std::min(exact_floor, doc.score);
    }

    DocScores best;
    for (const FuzzyMatch& match : candidates) {
        const double factor = fuzzy_score_factor(match.distance);
        for (const auto& [doc_id, score] : score_by_doc(match.term)) {
            if (std::ranges::binary_search(exact, doc_id, {}, &ScoredDoc::doc_id)) {
                continue;
            }
            double& slot = best[doc_id];
            slot = std::max(slot, factor * std::min(score, exact_floor));
        }
    }
    return by_doc(best);
}

TermScorer::PhraseTally TermScorer::phrase(const std::vector<std::string>& terms) const {
    double phrase_idf = 0.0;
    for (const std::string& term : terms) {
        phrase_idf += term_idf(term);
    }
    return PhraseTally(*this, phrase_idf);
}

void TermScorer::PhraseTally::add(std::string_view field, int doc_id, int frequency) {
    weighted_tfs[doc_id] += scorer.weighted_tf(field, doc_id, frequency);
}

ScoredDocs TermScorer::PhraseTally::scores() const {
    ScoredDocs docs;
    docs.reserve(weighted_tfs.size());
    for (const auto& [doc_id, tf] : weighted_tfs) {
        docs.push_back({doc_id, scorer.combine(idf, tf)});
    }
    std::ranges::sort(docs, {}, &ScoredDoc::doc_id);
    return docs;
}
