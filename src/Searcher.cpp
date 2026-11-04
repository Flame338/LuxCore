#include "Searcher.h"
#include <cmath>
#include <algorithm>

Searcher::Searcher(const InvertedIndex& index, const Analyzer& analyzer,const std::unordered_map<int, Document>& store)
    : inverted_index(index), analyzer(analyzer), doc_store(store) {}

std::vector<SearchHit> Searcher::search(const std::string& query_string, int top_k) const {
    std::vector<std::string> query_tokens = analyzer.analyze(query_string);
    std::unordered_map<int, double> doc_scores;
    int total_docs = inverted_index.get_total_documents();

    for (const auto& term : query_tokens) {
        const std::vector<Posting>* postings = inverted_index.get_postings(term);
        if (!postings) continue;

        int doc_freq = postings->size();
        double idf = std::log(static_cast<double>(total_docs + 1) / (doc_freq + 1)) + 1.0;

        for (const auto& posting : *postings) {
            double tf = static_cast<double>(posting.term_frequency);
            doc_scores[posting.doc_id] += tf * idf;
        }
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