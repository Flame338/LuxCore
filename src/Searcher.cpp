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
    int exact_tf = 0;
    int completion_tf = 0;
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

void add_prefix_scores(const PrefixExpansion& expansion, int total_docs,
                       std::unordered_map<int, double>& doc_scores) {
    std::unordered_map<int, PrefixTally> tallies;
    if (expansion.exact) {
        for (const auto& posting : *expansion.exact) {
            tallies[posting.doc_id].exact_tf += posting.term_frequency;
        }
    }
    for (const TermPostings& completion : expansion.completions) {
        for (const auto& posting : *completion.postings) {
            tallies[posting.doc_id].completion_tf += posting.term_frequency;
        }
    }

    const double prefix_idf = idf(total_docs, tallies.size());
    for (const auto& [doc_id, tally] : tallies) {
        if (tally.exact_tf > 0) {
            doc_scores[doc_id] += tally.exact_tf * prefix_idf;
        } else {
            const double tf = tally.completion_tf;
            doc_scores[doc_id] += completion_weight * tf / (tf + 1.0) * prefix_idf;
        }
    }
}

}

Searcher::Searcher(const InvertedIndex& index, const Analyzer& analyzer,const std::unordered_map<int, Document>& store)
    : inverted_index(index), analyzer(analyzer), doc_store(store) {}

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

    if (query.prefix) {
        add_prefix_scores(expand(inverted_index, *query.prefix), total_docs, doc_scores);
    }

    std::vector<SearchHit> results;
    for (const auto& [doc_id, score] : doc_scores) {
        results.push_back({doc_id, score});
    }

    std::sort(results.begin(), results.end(), [](const SearchHit& a, const SearchHit& b) {
        if (a.score != b.score) {
            return a.score > b.score;
        }
        return a.doc_id < b.doc_id;
    });

    if (results.size() > static_cast<size_t>(top_k)) {
        results.resize(top_k);
    }

    return results;
}