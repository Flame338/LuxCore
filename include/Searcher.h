#pragma once

#include "InvertedIndex.h"
#include "Analyzer.h"
#include "DocumentKeys.h"
#include "EditDistance.h"
#include "MatchPlan.h"
#include "Query.h"
#include "Scoring.h"
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

class Indexer;

struct Suggestion {
    std::string term;
    std::size_t doc_freq;
};

struct SearchOptions {
    FieldBoosts boosts = FieldBoosts::standard();
    Scoring scoring = Scoring::tf_idf();
    Fuzziness fuzziness = Fuzziness::Off;
};

class Searcher {
    private:
        const InvertedIndex& inverted_index;
        const Analyzer& analyzer;
        const DocumentKeys* keys;
        SearchOptions options;

        QueryContext context() const;
        std::vector<SearchHit> finish(TopKCollector& collector) const;
        std::vector<Suggestion> suggest_scoped(const std::string& query_string, std::optional<std::string_view> field,
                                               std::size_t limit) const;

    public:
        explicit Searcher(const Indexer& indexer, SearchOptions options = {});
        Searcher(const InvertedIndex& index, const Analyzer& analyzer, SearchOptions options = {});

        TextPlan plan(const std::string& query_string, std::optional<std::string_view> field = std::nullopt) const;
        std::vector<SearchHit> search(const TextPlan& plan, int top_k = 10) const;
        std::vector<SearchHit> search(const std::string& query_string, int top_k = 10) const;
        std::vector<SearchHit> search_in(std::string_view field, const std::string& query_string, int top_k = 10) const;
        std::vector<SearchHit> search(const Query& query, int top_k = 10) const;
        std::vector<int> matching_ids(const Query& query) const;
        std::vector<Suggestion> suggest(const std::string& query_string, std::size_t limit = 5) const;
        std::vector<Suggestion> suggest_in(std::string_view field, const std::string& query_string,
                                           std::size_t limit = 5) const;
};
