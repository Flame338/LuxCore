#include "Indexer.h"

#include <stdexcept>
#include <utility>

namespace {

struct Prepared {
    FieldTermFreqs field_term_freqs;
    Document stored;
};

Prepared prepare(const Analyzer& analyzer, const Document& document) {
    Prepared prepared;
    for (const Field& field : document.get_fields()) {
        if (field.options.indexed) {
            if (field.options.analyzed) {
                for (const std::string& token : analyzer.analyze(field.value)) {
                    prepared.field_term_freqs[field.name][token]++;
                }
            } else if (!field.value.empty()) {
                prepared.field_term_freqs[field.name][field.value]++;
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
    inverted_index.add_document(id, prepared.field_term_freqs);
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
    inverted_index.add_document(id, prepared.field_term_freqs);
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
