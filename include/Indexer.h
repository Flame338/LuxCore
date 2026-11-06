#pragma once

#include "Analyzer.h"
#include "Document.h"
#include "DocumentKeys.h"
#include "DocumentStore.h"
#include "InvertedIndex.h"

#include <optional>
#include <string>

class Indexer {
private:
    Analyzer analyzer;
    InvertedIndex inverted_index;
    DocumentStore doc_store;
    DocumentKeys doc_keys;

public:
    explicit Indexer(const std::string& stop_words_path = "");

    int add_document(const Document& document);
    int add_document(const std::string& key, const Document& document);
    bool update_document(int id, const Document& document);
    bool remove_document(int id);
    bool remove_document(const std::string& key);

    std::optional<int> find_id(const std::string& key) const { return doc_keys.find(key); }
    const std::string& get_key(int id) const { return doc_keys.key_of(id); }
    bool contains(int id) const { return doc_store.contains(id); }

    const InvertedIndex& get_index() const { return inverted_index; }
    const DocumentKeys& get_keys() const { return doc_keys; }
    const Document& get_document(int id) const { return doc_store.get(id); }
    const Analyzer& get_analyzer() const { return analyzer; }
};
