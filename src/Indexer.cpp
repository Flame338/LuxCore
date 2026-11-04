#include "Indexer.h"

Indexer::Indexer(const std::string& stop_words_path)
    : analyzer(make_standard_analyzer(stop_words_path)) {}

void Indexer::add_document(int id, const std::string& title, const std::string& body) {
    doc_store[id] = {id, title, body};

    std::unordered_map<std::string, int> term_freqs;
    for (const std::string& token : analyzer.analyze(title + " " + body)) {
        term_freqs[token]++;
    }

    inverted_index.add_document(id, term_freqs);
}
