#pragma once

#include "DocumentKeys.h"
#include "InvertedIndex.h"
#include "Query.h"
#include "Scoring.h"

#include <vector>

class IndexSearcher {
private:
    const InvertedIndex& inverted_index;
    FieldBoosts boosts;
    const DocumentKeys* keys;
    Scoring scoring;

public:
    explicit IndexSearcher(const InvertedIndex& index, FieldBoosts boosts = FieldBoosts::standard(),
                           const DocumentKeys* keys = nullptr, Scoring scoring = Scoring::tf_idf());

    std::vector<SearchHit> search(const Query& query, int top_k = 10) const;
    std::vector<int> matching_ids(const Query& query) const;
};
