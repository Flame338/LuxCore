#include "TermScorer.h"

#include <cmath>

namespace {

double idf(int total_docs, std::size_t doc_freq) {
    return std::log(static_cast<double>(total_docs + 1) / (doc_freq + 1)) + 1.0;
}

double bm25_idf(int total_docs, std::size_t doc_freq) {
    const double n = static_cast<double>(doc_freq);
    return std::log(1.0 + (total_docs - n + 0.5) / (n + 0.5));
}

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

double TermScorer::idf(const std::string& term) const {
    const std::vector<Posting>* postings = scope.postings(term);
    return postings ? idf_for(scoring, total_docs, postings->size()) : 0.0;
}

double TermScorer::average_length(std::string_view field) const {
    return scoring.scorer == Scorer::Bm25 ? index.get_average_field_length(field) : 0.0;
}

double TermScorer::weighted_tf(std::string_view field, int doc_id, double frequency, double boost,
                               double average_length) const {
    double tf = frequency * boost;
    if (scoring.scorer == Scorer::Bm25 && average_length > 0.0) {
        const double length = index.get_field_length(field, doc_id);
        tf /= 1.0 - scoring.bm25.b + scoring.bm25.b * length / average_length;
    }
    return tf;
}

double TermScorer::combine(double term_idf, double tf) const {
    return scoring.scorer == Scorer::Bm25 ? term_idf * tf / (scoring.bm25.k1 + tf) : tf * term_idf;
}

DocScores TermScorer::score(const std::string& term) const {
    DocScores scores;
    const std::vector<Posting>* postings = scope.postings(term);
    if (!postings) {
        return scores;
    }

    const double term_idf = idf_for(scoring, total_docs, postings->size());

    DocScores weighted_tfs;
    scope.for_each_field_postings(term, [&](std::string_view field_name, const std::vector<Posting>& field_postings,
                                            double weight) {
        const double field_average = average_length(field_name);
        for (const auto& posting : field_postings) {
            weighted_tfs[posting.doc_id] +=
                weighted_tf(field_name, posting.doc_id, static_cast<double>(posting.term_frequency), weight, field_average);
        }
    });
    for (const auto& [doc_id, tf] : weighted_tfs) {
        scores[doc_id] = combine(term_idf, tf);
    }
    return scores;
}
