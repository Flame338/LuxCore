#include "Searcher.h"
#include <cmath>
#include <algorithm>

namespace {

constexpr double completion_weight = 0.5;

struct PrefixExpansion {
    const std::vector<Posting>* exact;
    std::vector<TermPostings> completions;
};

struct PrefixTally {
    int doc_id;
    int exact_tf = 0;
    int completion_tf = 0;
};

struct PostingCursor {
    const Posting* at;
    const Posting* end;
    bool exact;
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

PrefixExpansion expand(const InvertedIndex& index, const std::string& prefix) {
    PrefixExpansion expansion{index.get_postings(prefix), {}};

    const std::size_t limit = max_completions(prefix.size());
    if (limit == 0) {
        return expansion;
    }

    expansion.completions = index.terms_with_prefix(prefix);
    std::erase_if(expansion.completions, [&prefix](const TermPostings& match) { return match.term == prefix; });

    if (expansion.completions.size() > limit) {
        std::partial_sort(expansion.completions.begin(), expansion.completions.begin() + limit,
                          expansion.completions.end(), [](const TermPostings& a, const TermPostings& b) {
            if (a.postings->size() != b.postings->size()) {
                return a.postings->size() > b.postings->size();
            }
            return a.term < b.term;
        });
        expansion.completions.resize(limit);
    }
    return expansion;
}

std::vector<PrefixTally> merge_postings(const PrefixExpansion& expansion) {
    std::vector<PostingCursor> cursors;
    std::size_t total_postings = 0;
    auto add_cursor = [&](const std::vector<Posting>& postings, bool exact) {
        if (!postings.empty()) {
            cursors.push_back({postings.data(), postings.data() + postings.size(), exact});
            total_postings += postings.size();
        }
    };
    if (expansion.exact) {
        add_cursor(*expansion.exact, true);
    }
    for (const TermPostings& completion : expansion.completions) {
        add_cursor(*completion.postings, false);
    }

    const auto later = [](const PostingCursor& a, const PostingCursor& b) { return a.at->doc_id > b.at->doc_id; };
    std::make_heap(cursors.begin(), cursors.end(), later);

    std::vector<PrefixTally> tallies;
    tallies.reserve(total_postings);
    while (!cursors.empty()) {
        std::pop_heap(cursors.begin(), cursors.end(), later);
        PostingCursor& cursor = cursors.back();
        if (tallies.empty() || tallies.back().doc_id != cursor.at->doc_id) {
            tallies.push_back({cursor.at->doc_id});
        }
        (cursor.exact ? tallies.back().exact_tf : tallies.back().completion_tf) += cursor.at->term_frequency;
        if (++cursor.at == cursor.end) {
            cursors.pop_back();
        } else {
            std::push_heap(cursors.begin(), cursors.end(), later);
        }
    }
    return tallies;
}

std::vector<SearchHit> prefix_scores(const PrefixExpansion& expansion, int total_docs) {
    const std::vector<PrefixTally> tallies = merge_postings(expansion);
    const double prefix_idf = idf(total_docs, tallies.size());

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

}

Searcher::Searcher(const InvertedIndex& index, const Analyzer& analyzer,const std::unordered_map<int, Document>& store)
    : inverted_index(index), analyzer(analyzer), doc_store(store) {}

std::vector<Suggestion> Searcher::suggest(const std::string& query_string, std::size_t limit) const {
    const AnalyzedQuery query = analyzer.analyze_query(query_string);
    if (!query.prefix) {
        return {};
    }

    std::vector<TermPostings> matches = inverted_index.terms_with_prefix(*query.prefix);
    const auto kept = matches.begin() + std::min(limit, matches.size());
    std::partial_sort(matches.begin(), kept, matches.end(), [](const TermPostings& a, const TermPostings& b) {
        if (a.postings->size() != b.postings->size()) {
            return a.postings->size() > b.postings->size();
        }
        return a.term < b.term;
    });

    std::vector<Suggestion> suggestions;
    suggestions.reserve(kept - matches.begin());
    for (auto it = matches.begin(); it != kept; ++it) {
        suggestions.push_back({std::string(it->term), it->postings->size()});
    }
    return suggestions;
}

std::vector<SearchHit> Searcher::search(const std::string& query_string, int top_k) const {
    AnalyzedQuery query = analyzer.analyze_query(query_string);
    std::unordered_map<int, double> doc_scores;
    int total_docs = inverted_index.get_total_documents();

    for (const auto& term : query.terms) {
        const std::vector<Posting>* postings = inverted_index.get_postings(term);
        if (!postings) continue;

        int doc_freq = postings->size();
        double term_idf = idf(total_docs, doc_freq);

        for (const auto& posting : *postings) {
            double tf = static_cast<double>(posting.term_frequency);
            doc_scores[posting.doc_id] += tf * term_idf;
        }
    }

    std::vector<SearchHit> results;
    if (query.prefix) {
        results = prefix_scores(expand(inverted_index, *query.prefix), total_docs);
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

    size_t keep = std::min(results.size(), static_cast<size_t>(top_k));
    std::partial_sort(results.begin(), results.begin() + keep, results.end(),
                      [](const SearchHit& a, const SearchHit& b) {
        if (a.score != b.score) {
            return a.score > b.score;
        }
        return a.doc_id < b.doc_id;
    });
    results.resize(keep);

    return results;
}