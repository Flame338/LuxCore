#pragma once

#include "InvertedIndex.h"
#include "Analyzer.h"
#include "DocumentKeys.h"
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

struct SearchHit {
    int doc_id;
    double score;
    std::string key = {};
};

struct Suggestion {
    std::string term;
    std::size_t doc_freq;
};

class FieldBoosts {
    private:
        std::map<std::string, double, std::less<>> boosts;
        double fallback = 1.0;

    public:
        static FieldBoosts standard() {
            FieldBoosts result;
            result.set("title", 2.0);
            return result;
        }

        FieldBoosts& set(const std::string& field, double boost) {
            boosts[field] = boost;
            return *this;
        }

        double get(std::string_view field) const {
            auto it = boosts.find(field);
            return it == boosts.end() ? fallback : it->second;
        }
};

class Searcher {
    private:
        const InvertedIndex& inverted_index;
        const Analyzer& analyzer;
        FieldBoosts boosts;
        const DocumentKeys* keys;

        std::vector<SearchHit> search_scoped(const std::string& query_string, std::optional<std::string_view> field,
                                             int top_k) const;
        std::vector<Suggestion> suggest_scoped(const std::string& query_string, std::optional<std::string_view> field,
                                               std::size_t limit) const;

    public:
        explicit Searcher(const InvertedIndex& index, const Analyzer& analyzer,
                          FieldBoosts boosts = FieldBoosts::standard(), const DocumentKeys* keys = nullptr);

        std::vector<SearchHit> search(const std::string& query_string, int top_k = 10) const;
        std::vector<SearchHit> search_in(std::string_view field, const std::string& query_string, int top_k = 10) const;
        std::vector<Suggestion> suggest(const std::string& query_string, std::size_t limit = 5) const;
        std::vector<Suggestion> suggest_in(std::string_view field, const std::string& query_string,
                                           std::size_t limit = 5) const;
};
