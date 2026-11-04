#include "Indexer.h"
#include "Searcher.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::vector<std::filesystem::path> find_text_files(const std::filesystem::path& folder) {
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(folder)) {
        if (entry.is_regular_file() && entry.path().extension() == ".txt") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

int index_folder(Indexer& indexer, const std::filesystem::path& folder) {
    int id = 0;
    for (const std::filesystem::path& path : find_text_files(folder)) {
        indexer.add_document(++id, path.stem().string(), read_file(path));
    }
    return id;
}

void index_samples(Indexer& indexer) {
    indexer.add_document(1, "Apache Lucene Overview", "LuxCore is a text search engine written in C++.");
    indexer.add_document(2, "Writing Search Engines", "A custom search engine needs an inverted index.");
    indexer.add_document(3, "Lorem Epsum", "Edgerunners 2 was a great show. Must watch.");
}

}

int main(int argc, char** argv) {
    Indexer indexer("stopwords.txt");

    if (argc > 1) {
        const std::filesystem::path folder = argv[1];
        if (!std::filesystem::is_directory(folder)) {
            std::cerr << "Not a folder: " << folder.string() << "\n";
            return 1;
        }
        if (index_folder(indexer, folder) == 0) {
            std::cerr << "No .txt files found in " << folder.string() << "\n";
            return 1;
        }
    } else {
        index_samples(indexer);
    }

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
