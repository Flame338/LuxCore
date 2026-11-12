#pragma once

#include "DocumentTerms.h"
#include "MemoryStats.h"
#include "PostingStore.h"
#include "TermDictionary.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class ByteWriter;
class ByteReader;
class DocumentStore;

struct IndexMemory {
    std::size_t posting_bytes;
    std::size_t position_bytes;
};

struct TermPostings {
    std::string_view term;
    std::size_t document_frequency;
};

struct FieldPostings {
    std::string_view field;
    Postings postings;
};

using TermFreqs = std::unordered_map<std::string, int>;
using FieldTermFreqs = std::unordered_map<std::string, TermFreqs>;
using TermPositions = std::unordered_map<std::string, std::vector<std::uint32_t>>;
using FieldTermPositions = std::unordered_map<std::string, TermPositions>;

// Terms live once in the dictionary. Each term chains one posting list per
// field it occurs in, and each document keeps the ids of its posting lists so
// it can be removed without the text it was built from.
class InvertedIndex {
    private:
        struct FieldIndex {
            std::string name;
            std::vector<int> lengths;
            long long total_length = 0;
            int documents = 0;
            std::size_t terms = 0;
        };

        struct StateStamp {
            std::uint64_t value;

            StateStamp() : value(next()) {}
            StateStamp(StateStamp&& other) noexcept : value(other.value) { other.value = next(); }
            StateStamp& operator=(StateStamp&& other) noexcept {
                if (this != &other) {
                    value = other.value;
                    other.value = next();
                }
                return *this;
            }

            void touch() { value = next(); }
            static std::uint64_t next();
        };

        TermDictionary terms;
        PostingStore lists;
        StateStamp stamp;
        std::vector<FieldIndex> fields;
        std::vector<std::uint32_t> free_fields;
        std::map<std::string, std::uint32_t, std::less<>> field_ids;
        // Per document, the ascending ids of its posting lists as varint gaps. A document
        // without terms holds an empty string, and an absent one holds nothing.
        std::vector<std::optional<std::string>> doc_lists;
        std::vector<ListId> scratch_lists;
        std::vector<TermId> scratch_terms;
        int total_documents = 0;

        std::uint32_t field_for(std::string_view name);
        std::optional<std::uint32_t> field_id(std::string_view name) const;
        ListId list_of(TermId term, std::uint32_t field) const;
        ListId list_for(TermId term, std::uint32_t field);
        ListId find_list(std::string_view field, std::string_view term) const;
        bool in_another_list(TermId term, ListId list, int doc_id) const;
        void drop_list(ListId list);
        void retire_field(std::uint32_t field);
        void remember_lists(int doc_id);

    public:
        InvertedIndex() = default;
        InvertedIndex(const InvertedIndex&) = delete;
        InvertedIndex& operator=(const InvertedIndex&) = delete;
        InvertedIndex(InvertedIndex&&) = default;
        InvertedIndex& operator=(InvertedIndex&&) = default;

        void add_document(int doc_id, const DocumentTerms& document);
        void add_document(int doc_id, const FieldTermFreqs& field_term_freqs);
        void add_document(int doc_id, const FieldTermPositions& field_term_positions);
        void remove_document(int doc_id);
        std::size_t get_document_frequency(std::string_view term) const;
        std::optional<Postings> get_postings(std::string_view field, std::string_view term) const;
        std::optional<PostingList> get_posting_list(std::string_view field, std::string_view term) const;
        std::vector<std::string_view> field_names() const;
        // In ascending order of field name.
        std::vector<FieldPostings> get_field_postings(std::string_view term) const;
        std::vector<TermPostings> terms_with_prefix(std::string_view prefix) const;
        std::vector<TermPostings> terms_with_prefix(std::string_view field, std::string_view prefix) const;

        std::vector<FuzzyMatch> fuzzy_terms(std::string_view term, std::size_t max_distance) const {
            return terms.fuzzy(term, max_distance);
        }

        std::uint64_t state() const { return stamp.value; }
        int get_total_documents() const { return total_documents; }
        int get_field_documents(std::string_view field) const;
        int get_field_length(std::string_view field, int doc_id) const;
        std::span<const int> get_field_lengths(std::string_view field) const;
        double get_average_field_length(std::string_view field) const;
        std::size_t get_unique_terms_count() const { return terms.size(); }
        IndexMemory memory_usage() const;
        void memory_stats(MemoryStats& out) const;

        void write(ByteWriter& writer) const;
        static InvertedIndex read(ByteReader& reader, const DocumentStore& store, bool keep_positions = true);
};
