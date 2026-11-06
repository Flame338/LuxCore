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

struct FieldPostings {
    std::string_view field;
    const std::vector<Posting>* postings;
};

using TermFreqs =std::unordered_map<std::string, int>;
using FieldTermFreqs = std::unordered_map<std::string, TermFreqs>;

class InvertedIndex {
    private:
        using PostingMap = std::map<std::string, std::vector<Posting>, std::less<>>;

        struct FieldIndex {
            PostingMap terms;
            int documents = 0;
        };

        struct IndexedField {
            std::string field;
            std::vector<std::string> terms;
        };

        PostingMap index;
        std::map<std::string, FieldIndex, std::less<>> fields;
        std::unordered_map<int, std::vector<IndexedField>> doc_fields;
        int total_documents = 0;

    public:
        void add_document(int doc_id, const FieldTermFreqs& field_term_freqs);
        void remove_document(int doc_id);
        const std::vector<Posting>* get_postings(const std::string& term) const;
        const std::vector<Posting>* get_postings(std::string_view field, const std::string& term) const;
        std::vector<FieldPostings> get_field_postings(const std::string& term) const;
        std::vector<TermPostings> terms_with_prefix(std::string_view prefix) const;
        std::vector<TermPostings> terms_with_prefix(std::string_view field, std::string_view prefix) const;

        int get_total_documents() const { return total_documents; }
        int get_field_documents(std::string_view field) const;
        std::size_t get_unique_terms_count() const { return index.size(); }
};
