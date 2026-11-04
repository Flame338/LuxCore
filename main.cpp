#include <fstream>
#include <iomanip>
#include <string>
#include <iostream>
#include <unordered_map>
#include <vector>
#include "Analyzer.h"
#include "Searcher.h"
#include "InvertedIndex.h"

class Indexer {
    private: 
        Analyzer analyzer;
        InvertedIndex inverted_index;
        std::unordered_map<int, Document> doc_store;

    public:
        Indexer(const std::string& stop_words_path = "") : analyzer(make_standard_analyzer(stop_words_path)) {}

        void add_document(int id, const std::string& title, const std::string& body) {
            doc_store[id] = {id, title, body};

            std::string full_text = title + " " + body;
            std::vector<std::string> tokens = analyzer.analyze(full_text);


            std::unordered_map<std::string, int> term_freqs;
            for (const auto& token : tokens) {
                term_freqs[token]++;
            }

            inverted_index.add_document(id, term_freqs);
        }

        const InvertedIndex& get_index() const { return inverted_index; }
        const Document& get_document(int id) const { return doc_store.at(id); }
        const Analyzer& get_analyzer() const { return analyzer; }
        const std::unordered_map<int, Document>& get_doc_store() const { return doc_store; }
};

int main() {
    Indexer indexer("stopwords.txt");

    indexer.add_document(1, "Apache Lucene Overview", "LuxCore is a text search engine written in C++.");
    indexer.add_document(2, "Writing Search Engines", "A custom search engine needs an inverted index written.");
    indexer.add_document(3, "Lorem Epsum", "Edgerunners 2 was a great show. Must watch.");

    const InvertedIndex& index = indexer.get_index();
    std::cout << "Documents: " << index.get_total_documents()
              << ", unique terms: " << index.get_unique_terms_count() << "\n";

    Searcher searcher(index, indexer.get_analyzer(), indexer.get_doc_store());

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Type a query and press Enter (empty line to quit)\n> ";

    std::string query;
    while (std::getline(std::cin, query) && !query.empty()) {
        std::vector<SearchHit> hits = searcher.search(query);
        if (hits.empty()) {
            std::cout << "No results\n";
        }
        for (const SearchHit& hit : hits) {
            std::cout << "  doc " << hit.doc_id << "  score " << hit.score << "  "
                      << indexer.get_document(hit.doc_id).title << "\n";
        }
        std::cout << "> ";
    }
    return 0;
}