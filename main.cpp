#include "InvertedIndex.cpp"
#include <unordered_set>
#include <fstream>
#include <string>
#include <iostream>
#include <vector>
#include <cctype>
#include <algorithm>


class Tokenizer {
    private:
        std::unordered_set<std::string> stop_words;

        void load_stop_words(const std::string& filepath) {
            std::ifstream file(filepath);
            if (!file.is_open()) {
                std::cerr << "Warning: Could not open stop word file: " << filepath << "\n";
                return;
            }

            std::string word;
            while (file >> word) {
                std::transform(word.begin(), word.end(), word.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                });
                stop_words.insert(word);
            }
        }

    public:
        Tokenizer(const std::string& stop_words_path = "") {
            if (!stop_words_path.empty()) {
                load_stop_words(stop_words_path);
            }
        }

        std::vector<std::string> tokenize(const std::string& text) const {
            std::vector<std::string> tokens;
            std::string current_token = "";

            for (char c : text) {
                unsigned char u_c = static_cast<unsigned char>(c);
                if (std::isalnum(u_c)) {
                    current_token += static_cast<char>(std::tolower(u_c));
                } else if (!current_token.empty()) {
                    // Assigning non-alphanumeric character to act as delimiters
                    if (stop_words.find(current_token) == stop_words.end()) {
                        tokens.push_back(current_token);
                    }
                    current_token.clear();
                }
            }

            if (!current_token.empty() && stop_words.find(current_token) == stop_words.end()) {
                tokens.push_back(current_token);
            }

            return tokens;
        }
};

struct Document {
    int id;
    std::string title;
    std::string body;
};

class Indexer {
    private: 
        Tokenizer tokenizer;
        InvertedIndex inverted_index;
        std::unordered_map<int, Document> doc_store;

    public:
        Indexer(const std::string& stop_words_path = "") : tokenizer(stop_words_path) {}

        void add_document(int id, const std::string& title, const std::string& body) {
            doc_store[id] = {id, title, body};

            std::string full_text = title + " " + body;
            std::vector<std::string> tokens = tokenizer.tokenize(full_text);


            std::unordered_map<std::string, int> term_freqs;
            for (const auto& token : tokens) {
                term_freqs[token]++;
            }

            inverted_index.add_document(id, term_freqs);
        }

        const InvertedIndex& get_index() const { return inverted_index; }
        const Document& get_document(int id) const { return doc_store.at(id); }
};

int main() {
    Indexer indexer("stopwords.txt");

    indexer.add_document(1, "Apache Lucene Overview", "LuxCore is a text search engine written in C++.");
    indexer.add_document(2, "Writing Search Engines", "A custom search engine needs an inverted index.");
    indexer.add_document(3, "Lorem Epsum", "Edgerunners 2 was a great show. Must watch.");

    const InvertedIndex& index = indexer.get_index();
    std::cout << "Documents: " << index.get_total_documents()
              << ", unique terms: " << index.get_unique_terms_count() << "\n";

    for (const std::string term : {"search", "engine", "lucene", "the", "missing"}) {
        std::cout << term << ":";
        const std::vector<Posting>* postings = index.get_postings(term);
        if (postings == nullptr) {
            std::cout << " (no matches)";
        } else {
            for (const Posting& p : *postings) {
                std::cout << " [doc " << p.doc_id << " \"" << indexer.get_document(p.doc_id).title
                          << "\", tf " << p.term_frequency << "]";
            }
        }
        std::cout << "\n";
    }
    return 0;
}