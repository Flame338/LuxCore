#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct Posting {
    int doc_id;
    int term_frequency;
};

struct TermPostings {
    std::string_view term;
    const std::vector<Posting>* postings;
};

class InvertedIndex {
    private:
        std::map<std::string, std::vector<Posting>, std::less<>> index;
        std::unordered_map<int, std::vector<std::string>> doc_terms;
        int total_documents = 0;

    public:
        void add_document(int doc_id, const std::unordered_map<std::string, int>& term_freqs);
        void remove_document(int doc_id);
        const std::vector<Posting>* get_postings(const std::string& term) const;
        std::vector<TermPostings> terms_with_prefix(std::string_view prefix) const;

        int get_total_documents() const { return total_documents; }
        std::size_t get_unique_terms_count() const { return index.size(); }
};
