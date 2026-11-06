#include "Indexer.h"

#include <utility>

Indexer::Indexer(const std::string& stop_words_path)
    : analyzer(make_standard_analyzer(stop_words_path)) {}

int Indexer::add_document(const Document& document) {
    FieldTermFreqs field_term_freqs;
    Document stored;

    for (const Field& field : document.get_fields()) {
        if (field.options.indexed) {
            if (field.options.analyzed) {
                for (const std::string& token : analyzer.analyze(field.value)) {
                    field_term_freqs[field.name][token]++;
                }
            } else if (!field.value.empty()) {
                field_term_freqs[field.name][field.value]++;
            }
        }
        if (field.options.stored) {
            stored.add(field);
        }
    }

    const int id = doc_store.add(std::move(stored));
    inverted_index.add_document(id, field_term_freqs);
    return id;
}
