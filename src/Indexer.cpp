#include "Indexer.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace {

struct Prepared {
    FieldTermPositions field_term_positions;
    Document stored;
};

Prepared prepare(const Analyzer& analyzer, const Document& document) {
    Prepared prepared;
    std::unordered_map<std::string, std::uint32_t> next_base;
    for (const Field& field : document.get_fields()) {
        if (field.options.indexed) {
            const std::uint32_t base = next_base[field.name];
            if (field.options.analyzed) {
                std::uint32_t end = base;
                for (const Token& token : analyzer.analyze_tokens(field.value)) {
                    const std::uint32_t position = base + static_cast<std::uint32_t>(token.position);
                    prepared.field_term_positions[field.name][token.term].push_back(position);
                    end = position + 2;
                }
                next_base[field.name] = end;
            } else if (!field.value.empty()) {
                prepared.field_term_positions[field.name][field.value].push_back(base);
                next_base[field.name] = base + 2;
            }
        }
        if (field.options.stored) {
            prepared.stored.add(field);
        }
    }
    return prepared;
}

}

Indexer::Indexer(const std::string& stop_words_path)
    : analyzer(make_standard_analyzer(stop_words_path)) {}

int Indexer::add_document(const Document& document) {
    Prepared prepared = prepare(analyzer, document);
    const int id = doc_store.add(std::move(prepared.stored));
    inverted_index.add_document(id, prepared.field_term_positions);
    return id;
}

int Indexer::add_document(const std::string& key, const Document& document) {
    if (key.empty()) {
        throw std::invalid_argument("Indexer::add_document: empty key");
    }
    if (const std::optional<int> existing = doc_keys.find(key)) {
        update_document(*existing, document);
        return *existing;
    }
    const int id = add_document(document);
    doc_keys.bind(key, id);
    return id;
}

bool Indexer::update_document(int id, const Document& document) {
    if (!doc_store.contains(id)) {
        return false;
    }
    Prepared prepared = prepare(analyzer, document);
    doc_store.replace(id, std::move(prepared.stored));
    inverted_index.add_document(id, prepared.field_term_positions);
    return true;
}

bool Indexer::remove_document(int id) {
    if (!doc_store.remove(id)) {
        return false;
    }
    inverted_index.remove_document(id);
    doc_keys.unbind(id);
    return true;
}

bool Indexer::remove_document(const std::string& key) {
    const std::optional<int> id = doc_keys.find(key);
    return id && remove_document(*id);
}
