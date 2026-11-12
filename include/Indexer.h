#pragma once

#include "Analyzer.h"
#include "Document.h"
#include "DocumentKeys.h"
#include "DocumentStore.h"
#include "InvertedIndex.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>

class ByteSink;

struct IndexOptions {
    bool positions = true;
};

struct FieldText {
    std::string_view name;
    std::string_view value;
    FieldOptions options = FieldOptions::text();
};

struct PreparedDocument {
    DocumentTerms terms;
    Document stored;
};

class Indexer {
private:
    IndexOptions options;
    Analyzer analyzer;
    InvertedIndex inverted_index;
    DocumentStore doc_store;
    DocumentKeys doc_keys;

    int insert(const PreparedDocument& prepared);
    void overwrite(int id, const PreparedDocument& prepared);

public:
    explicit Indexer(const std::string& stop_words_path = "", IndexOptions options = {});

    PreparedDocument prepare(std::span<const FieldText> fields) const;
    PreparedDocument prepare(const Document& document) const;
    int add_prepared(const std::string& key, const PreparedDocument& prepared);

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
    Document get_document(int id) const { return doc_store.get(id); }
    std::optional<std::string_view> stored_value(int id, std::string_view field) const {
        return doc_store.value_of(id, field);
    }
    const Analyzer& get_analyzer() const { return analyzer; }

    MemoryStats memory_stats() const;
    void serialize(ByteSink& sink) const;
    std::string serialize() const;
    static Indexer deserialize(std::string_view data, const std::string& stop_words_path = "",
                               IndexOptions options = {});
};
