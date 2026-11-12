#pragma once

#include "InvertedIndex.h"

#include <map>
#include <string>
#include <vector>

inline std::vector<Posting> combined_postings(const InvertedIndex& index, const std::string& term) {
    std::map<int, int> by_document;
    for (const FieldPostings& entry : index.get_field_postings(term)) {
        for (const Posting posting : entry.postings) {
            by_document[posting.doc_id] += posting.term_frequency;
        }
    }
    std::vector<Posting> merged;
    for (const auto& [doc_id, frequency] : by_document) {
        merged.push_back({doc_id, frequency});
    }
    return merged;
}
