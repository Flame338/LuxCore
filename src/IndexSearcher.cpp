#include "IndexSearcher.h"

#include "TermScorer.h"

#include <utility>

IndexSearcher::IndexSearcher(const InvertedIndex& index, FieldBoosts boosts, const DocumentKeys* keys,
                             Scoring scoring)
    : inverted_index(index), boosts(std::move(boosts)), keys(keys), scoring(scoring) {}

std::vector<SearchHit> IndexSearcher::search(const Query& query, int top_k) const {
    const QueryContext context{inverted_index, boosts, scoring};

    TopKCollector collector(top_k > 0 ? static_cast<std::size_t>(top_k) : 0);
    for (const ScoredDoc& doc : query.execute(context)) {
        collector.offer({doc.doc_id, doc.score});
    }
    std::vector<SearchHit> results = collector.take();

    if (keys) {
        for (SearchHit& hit : results) {
            hit.key = keys->key_of(hit.doc_id);
        }
    }
    return results;
}

std::vector<int> IndexSearcher::matching_ids(const Query& query) const {
    const QueryContext context{inverted_index, boosts, scoring};

    std::vector<int> ids;
    for (const ScoredDoc& doc : query.execute(context)) {
        ids.push_back(doc.doc_id);
    }
    return ids;
}
