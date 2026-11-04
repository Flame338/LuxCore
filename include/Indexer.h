#pragma once

#include "Analyzer.h"
#include "Document.h"
#include "InvertedIndex.h"

#include <string>
#include <unordered_map>

class Indexer {
private:
    Analyzer analyzer;
    InvertedIndex inverted_index;
    std::unordered_map<int, Document> doc_store;

public:
    explicit Indexer(const std::string& stop_words_path = "");

    void add_document(int id, const std::string& title, const std::string& body);

    const InvertedIndex& get_index() const { return inverted_index; }
    const Document& get_document(int id) const { return doc_store.at(id); }
    const Analyzer& get_analyzer() const { return analyzer; }
    const std::unordered_map<int, Document>& get_doc_store() const { return doc_store; }
};
