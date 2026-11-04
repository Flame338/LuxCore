#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>

struct Posting {
    int doc_id;
    int term_frequency;
};

class InvertedIndex {
    private:
        std::unordered_map<std::string, std::vector<Posting>> index;
        int total_documents = 0;

    public:
        void add_document(int doc_id, const std::unordered_map<std::string, int>& term_freqs) {
            for (const auto& [term, freq] : term_freqs) {
                index[term].push_back({doc_id, freq});
            }
            total_documents++;
        }

        const std::vector<Posting>* get_postings(const std::string& term) const {
            auto it = index.find(term);
            if (it != index.end()) {
                return &(it->second);
            }
            return nullptr;
        }

        int get_total_documents() const {
            return total_documents;
        }

        size_t get_unique_terms_count() const {
            return index.size();
        }
};