#include "Indexer.h"
#include "Searcher.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
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
    int count = 0;
    for (const std::filesystem::path& path : find_text_files(folder)) {
        indexer.add_document(path.filename().string(), make_text_document(path.stem().string(), read_file(path)));
        count++;
    }
    return count;
}

void index_samples(Indexer& indexer) {
    indexer.add_document("sample-1", make_text_document("Apache Lucene Overview", "LuxCore is a text search engine written in C++."));
    indexer.add_document("sample-2", make_text_document("Writing Search Engines", "A custom search engine needs an inverted index."));
    indexer.add_document("sample-3", make_text_document("Lorem Epsum", "Edgerunners 2 was a great show. Must watch."));
}

std::string title_of(const Indexer& indexer, int doc_id) {
    const Field* title = indexer.get_document(doc_id).find("title");
    return title == nullptr ? std::string() : title->value;
}

constexpr std::string_view type_command = ":type ";
constexpr int keystroke_hits = 3;

bool is_utf8_continuation(char c) {
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

void print_hits(const Indexer& indexer, const std::vector<SearchHit>& hits) {
    if (hits.empty()) {
        std::cout << "No results\n";
    }
    for (const SearchHit& hit : hits) {
        std::cout << "  " << hit.key << "  score " << hit.score << "  "
                  << title_of(indexer, hit.doc_id) << "\n";
    }
}

void replay_typing(const Indexer& indexer, const Searcher& searcher, const std::string& text) {
    for (std::size_t typed = 1; typed <= text.size(); typed++) {
        if (typed < text.size() && is_utf8_continuation(text[typed])) {
            continue;
        }
        const std::string so_far = text.substr(0, typed);
        std::cout << "  " << so_far << "  ->";
        const std::vector<SearchHit> hits = searcher.search(so_far, keystroke_hits);
        for (std::size_t i = 0; i < hits.size(); i++) {
            std::cout << (i == 0 ? " " : " | ") << title_of(indexer, hits[i].doc_id);
        }
        std::cout << "\n";
    }
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

    Searcher searcher(index, indexer.get_analyzer(), FieldBoosts::standard(), &indexer.get_keys());

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Type a query and press Enter (empty line to quit)\n> ";

    std::string query;
    while (std::getline(std::cin, query) && !query.empty()) {
        if (query.starts_with(type_command)) {
            replay_typing(indexer, searcher, query.substr(type_command.size()));
        } else {
            print_hits(indexer, searcher.search(query));
        }
        std::cout << "> ";
    }
    return 0;
}
