#include "Searcher.h"
#include "EditDistance.h"
#include "Utf8.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include <queue>

namespace {

constexpr double completion_weight = 0.5;
constexpr std::size_t max_fuzzy_candidates = 20;

struct Scope {
    const InvertedIndex& index;
    const FieldBoosts& boosts;
    std::optional<std::string_view> field;

    const std::vector<Posting>* postings(const std::string& term) const {
        return field ? index.get_postings(*field, term) : index.get_postings(term);
    }

    std::vector<TermPostings> terms_with_prefix(std::string_view prefix) const {
        return field ? index.terms_with_prefix(*field, prefix) : index.terms_with_prefix(prefix);
    }

    int documents() const {
        return field ? index.get_field_documents(*field) : index.get_total_documents();
    }

    template <typename Visit>
    void for_each_field_postings(const std::string& term, Visit&& visit) const {
        if (field) {
            if (const std::vector<Posting>* found = index.get_postings(*field, term)) {
                visit(*field, *found, boosts.get(*field));
            }
            return;
        }
        for (const FieldPostings& entry : index.get_field_postings(term)) {
            visit(entry.field, *entry.postings, boosts.get(entry.field));
        }
    }
};

struct PrefixRun {
    const std::vector<Posting>* postings;
    double weight;
    bool exact;
};

struct PrefixExpansion {
    std::vector<PrefixRun> runs;
};

struct PrefixTally {
    int doc_id;
    double exact_tf = 0;
    double completion_tf = 0;
};

std::size_t max_completions(std::size_t prefix_length) {
    if (prefix_length <= 1) {
        return 0;
    }
    if (prefix_length == 2) {
        return 4;
    }
    return 32;
}

double idf(int total_docs, std::size_t doc_freq) {
    return std::log(static_cast<double>(total_docs + 1) / (doc_freq + 1)) + 1.0;
}

double bm25_idf(int total_docs, std::size_t doc_freq) {
    const double n = static_cast<double>(doc_freq);
    return std::log(1.0 + (total_docs - n + 0.5) / (n + 0.5));
}

double idf_for(const Scoring& scoring, int total_docs, std::size_t doc_freq) {
    return scoring.scorer == Scorer::Bm25 ? bm25_idf(total_docs, doc_freq) : idf(total_docs, doc_freq);
}

bool ranks_before(const SearchHit& a, const SearchHit& b) {
    if (a.score != b.score) {
        return a.score > b.score;
    }
    return a.doc_id < b.doc_id;
}

class TopKCollector {
private:
    std::size_t capacity;
    std::priority_queue<SearchHit, std::vector<SearchHit>, decltype(&ranks_before)> worst_on_top{&ranks_before};

public:
    explicit TopKCollector(std::size_t capacity) : capacity(capacity) {}

    void offer(SearchHit hit) {
        if (worst_on_top.size() < capacity) {
            worst_on_top.push(std::move(hit));
        } else if (capacity > 0 && ranks_before(hit, worst_on_top.top())) {
            worst_on_top.pop();
            worst_on_top.push(std::move(hit));
        }
    }

    std::vector<SearchHit> take() {
        std::vector<SearchHit> ranked(worst_on_top.size());
        for (auto it = ranked.rbegin(); it != ranked.rend(); ++it) {
            *it = worst_on_top.top();
            worst_on_top.pop();
        }
        return ranked;
    }
};

bool more_common(const TermPostings& a, const TermPostings& b) {
    if (a.postings->size() != b.postings->size()) {
        return a.postings->size() > b.postings->size();
    }
    return a.term < b.term;
}

PrefixExpansion expand(const Scope& scope, const std::string& prefix) {
    PrefixExpansion expansion;
    auto add_runs = [&](const std::string& term, bool exact) {
        scope.for_each_field_postings(term, [&](std::string_view, const std::vector<Posting>& postings, double weight) {
            expansion.runs.push_back({&postings, weight, exact});
        });
    };
    add_runs(prefix, true);

    const std::size_t limit = max_completions(utf8::count_code_points(prefix));
    if (limit == 0) {
        return expansion;
    }

    std::vector<TermPostings> completions = scope.terms_with_prefix(prefix);
    std::erase_if(completions, [&prefix](const TermPostings& match) { return match.term == prefix; });

    if (completions.size() > limit) {
        std::partial_sort(completions.begin(), completions.begin() + limit, completions.end(), more_common);
        completions.resize(limit);
    }
    for (const TermPostings& completion : completions) {
        add_runs(std::string(completion.term), false);
    }
    return expansion;
}

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

std::vector<PrefixTally> merge_postings(const PrefixExpansion& expansion) {
    std::size_t total_postings = 0;
    for (const PrefixRun& run : expansion.runs) {
        total_postings += run.postings->size();
    }

    std::vector<PrefixTally> tallies(total_postings);
    std::vector<std::size_t> bounds{0};
    for (const PrefixRun& run : expansion.runs) {
        PrefixTally* out = tallies.data() + bounds.back();
        for (const Posting& posting : *run.postings) {
            const double tf = posting.term_frequency * run.weight;
            *out++ = run.exact ? PrefixTally{posting.doc_id, tf, 0} : PrefixTally{posting.doc_id, 0, tf};
        }
        bounds.push_back(bounds.back() + run.postings->size());
    }

    std::vector<PrefixTally> scratch(bounds.size() > 2 ? total_postings : 0);
    std::vector<std::size_t> next_bounds;
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
    tallies.resize(bounds.back());
    return tallies;
}

std::vector<SearchHit> prefix_scores(const PrefixExpansion& expansion, int total_docs, const Scoring& scoring) {
    const std::vector<PrefixTally> tallies = merge_postings(expansion);
    const double prefix_idf = idf_for(scoring, total_docs, tallies.size());

    std::vector<SearchHit> hits;
    hits.reserve(tallies.size());
    for (const PrefixTally& tally : tallies) {
        if (tally.exact_tf > 0) {
            hits.push_back({tally.doc_id, tally.exact_tf * prefix_idf});
        } else {
            const double tf = tally.completion_tf;
            hits.push_back({tally.doc_id, completion_weight * tf / (tf + 1.0) * prefix_idf});
        }
    }
    return hits;
}

using DocScores = std::unordered_map<int, double>;

class TermScorer {
private:
    const Scope& scope;
    const InvertedIndex& index;
    const Scoring& scoring;
    int total_docs;

public:
    TermScorer(const Scope& scope, const InvertedIndex& index, const Scoring& scoring)
        : scope(scope), index(index), scoring(scoring), total_docs(scope.documents()) {}

    DocScores score(const std::string& term) const {
        DocScores scores;
        const std::vector<Posting>* postings = scope.postings(term);
        if (!postings) {
            return scores;
        }

        const double term_idf = idf_for(scoring, total_docs, postings->size());
        const double k1 = scoring.bm25.k1;
        const double b = scoring.bm25.b;
        const bool bm25 = scoring.scorer == Scorer::Bm25;

        DocScores weighted_tfs;
        scope.for_each_field_postings(term, [&](std::string_view field_name, const std::vector<Posting>& field_postings,
                                                double weight) {
            const double average_length = bm25 ? index.get_average_field_length(field_name) : 0.0;
            for (const auto& posting : field_postings) {
                double tf = static_cast<double>(posting.term_frequency) * weight;
                if (bm25 && average_length > 0.0) {
                    const double length = index.get_field_length(field_name, posting.doc_id);
                    tf /= 1.0 - b + b * length / average_length;
                }
                weighted_tfs[posting.doc_id] += tf;
            }
        });
        for (const auto& [doc_id, tf] : weighted_tfs) {
            scores[doc_id] = bm25 ? term_idf * tf / (k1 + tf) : tf * term_idf;
        }
        return scores;
    }
};

std::vector<FuzzyMatch> fuzzy_candidates(const InvertedIndex& index, const std::string& term) {
    const std::size_t max_distance = fuzzy_max_distance(utf8::count_code_points(term));
    std::vector<FuzzyMatch> matches;
    if (max_distance == 0) {
        return matches;
    }
    for (FuzzyMatch& match : index.fuzzy_terms(term, max_distance)) {
        if (match.distance > 0) {
            matches.push_back(std::move(match));
        }
    }
    auto frequency = [&index](const FuzzyMatch& match) {
        const std::vector<Posting>* postings = index.get_postings(match.term);
        return postings ? postings->size() : std::size_t{0};
    };
    std::ranges::stable_sort(matches, [&](const FuzzyMatch& a, const FuzzyMatch& b) {
        return a.distance != b.distance ? a.distance < b.distance : frequency(a) > frequency(b);
    });
    if (matches.size() > max_fuzzy_candidates) {
        matches.resize(max_fuzzy_candidates);
    }
    return matches;
}

DocScores fuzzy_scores(const TermScorer& scorer, const InvertedIndex& index, const std::string& term,
                       const DocScores& exact) {
    double exact_floor = std::numeric_limits<double>::infinity();
    for (const auto& [doc_id, score] : exact) {
        exact_floor = std::min(exact_floor, score);
    }

    DocScores best;
    for (const FuzzyMatch& match : fuzzy_candidates(index, term)) {
        const double factor = fuzzy_score_factor(match.distance);
        for (const auto& [doc_id, score] : scorer.score(match.term)) {
            if (exact.contains(doc_id)) {
                continue;
            }
            double& slot = best[doc_id];
            slot = std::max(slot, factor * std::min(score, exact_floor));
        }
    }
    return best;
}

}

Searcher::Searcher(const InvertedIndex& index, const Analyzer& analyzer, FieldBoosts boosts,
                   const DocumentKeys* keys, Scoring scoring, Fuzziness fuzziness)
    : inverted_index(index), analyzer(analyzer), boosts(std::move(boosts)), keys(keys), scoring(scoring),
      fuzziness(fuzziness) {}

std::vector<Suggestion> Searcher::suggest(const std::string& query_string, std::size_t limit) const {
    return suggest_scoped(query_string, std::nullopt, limit);
}

std::vector<Suggestion> Searcher::suggest_in(std::string_view field, const std::string& query_string,
                                             std::size_t limit) const {
    return suggest_scoped(query_string, field, limit);
}

std::vector<SearchHit> Searcher::search(const std::string& query_string, int top_k) const {
    return search_scoped(query_string, std::nullopt, top_k);
}

std::vector<SearchHit> Searcher::search_in(std::string_view field, const std::string& query_string, int top_k) const {
    return search_scoped(query_string, field, top_k);
}

std::vector<Suggestion> Searcher::suggest_scoped(const std::string& query_string,
                                                 std::optional<std::string_view> field, std::size_t limit) const {
    const AnalyzedQuery query = analyzer.analyze_query(query_string);
    if (!query.prefix) {
        return {};
    }

    const Scope scope{inverted_index, boosts, field};
    std::vector<TermPostings> matches = scope.terms_with_prefix(*query.prefix);
    const auto kept = matches.begin() + std::min(limit, matches.size());
    std::partial_sort(matches.begin(), kept, matches.end(), more_common);

    std::vector<Suggestion> suggestions;
    suggestions.reserve(kept - matches.begin());
    for (auto it = matches.begin(); it != kept; ++it) {
        suggestions.push_back({std::string(it->term), it->postings->size()});
    }
    return suggestions;
}

std::vector<SearchHit> Searcher::search_scoped(const std::string& query_string,
                                               std::optional<std::string_view> field, int top_k) const {
    AnalyzedQuery query = analyzer.analyze_query(query_string);
    const Scope scope{inverted_index, boosts, field};
    DocScores doc_scores;
    const int total_docs = scope.documents();
    const TermScorer scorer(scope, inverted_index, scoring);

    for (const auto& term : query.terms) {
        const DocScores exact = scorer.score(term);
        for (const auto& [doc_id, score] : exact) {
            doc_scores[doc_id] += score;
        }
        if (fuzziness == Fuzziness::On) {
            for (const auto& [doc_id, score] : fuzzy_scores(scorer, inverted_index, term, exact)) {
                doc_scores[doc_id] += score;
            }
        }
    }

    std::vector<SearchHit> results;
    if (query.prefix) {
        results = prefix_scores(expand(scope, *query.prefix), total_docs, scoring);
    }

    if (!doc_scores.empty()) {
        for (const SearchHit& hit : results) {
            doc_scores[hit.doc_id] += hit.score;
        }
        results.clear();
        results.reserve(doc_scores.size());
        for (const auto& [doc_id, score] : doc_scores) {
            results.push_back({doc_id, score});
        }
    }

    TopKCollector collector(top_k > 0 ? static_cast<std::size_t>(top_k) : 0);
    for (SearchHit& hit : results) {
        collector.offer(std::move(hit));
    }
    results = collector.take();

    if (keys) {
        for (SearchHit& hit : results) {
            hit.key = keys->key_of(hit.doc_id);
        }
    }
    return results;
}
