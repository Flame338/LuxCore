#pragma once

#include "Analyzer.h"
#include "Document.h"
#include "DocumentStore.h"
#include "InvertedIndex.h"

#include <string>

class Indexer {
private:
    Analyzer analyzer;
    InvertedIndex inverted_index;
    DocumentStore doc_store;

public:
    explicit Indexer(const std::string& stop_words_path = "");

    int add_document(const Document& document);

    const InvertedIndex& get_index() const { return inverted_index; }
    const Document& get_document(int id) const { return doc_store.get(id); }
    const Analyzer& get_analyzer() const { return analyzer; }
};
