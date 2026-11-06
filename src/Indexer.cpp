#include "Indexer.h"

#include <unordered_map>
#include <utility>

Indexer::Indexer(const std::string& stop_words_path)
    : analyzer(make_standard_analyzer(stop_words_path)) {}

int Indexer::add_document(const Document& document) {
    std::unordered_map<std::string, int> term_freqs;
    Document stored;

    for (const Field& field : document.get_fields()) {
        if (field.options.indexed) {
            if (field.options.analyzed) {
                for (const std::string& token : analyzer.analyze(field.value)) {
                    term_freqs[token]++;
                }
            } else if (!field.value.empty()) {
                term_freqs[field.value]++;
            }
        }
        if (field.options.stored) {
            stored.add(field);
        }
    }

    const int id = doc_store.add(std::move(stored));
    inverted_index.add_document(id, term_freqs);
    return id;
}
