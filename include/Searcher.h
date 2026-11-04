#pragma once

#include "InvertedIndex.h"
#include "Tokenizer.h"
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>

struct SearchHit {
    int doc_id;
    double score;
};

struct Document {
    int id;
    std::string title;
    std::string body;
};

class Searcher {
    private:
        const InvertedIndex& inverted_index;
        const Tokenizer& tokenizer;
        const std::unordered_map<int, Document>& doc_store;

    public:
        explicit Searcher(const InvertedIndex& index, const Tokenizer& tokenizer, const std::unordered_map<int, Document>& store);

        std::vector<SearchHit> search(const std::string& query_string, int top_k = 10) const;
};