#pragma once

#include "FuzzyIndex.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class ByteWriter;
class ByteReader;
class DocumentStore;

struct Posting {
    int doc_id;
    int term_frequency;
};

struct PostingList {
    static constexpr std::uint32_t no_positions = UINT32_MAX;

    std::vector<Posting> postings;
    std::vector<std::uint32_t> offsets;
    std::vector<std::uint32_t> positions;

    bool has_positions(std::size_t index) const {
        return !offsets.empty() && offsets[index] != no_positions;
    }

    std::span<const std::uint32_t> positions_of(std::size_t index) const {
        if (!has_positions(index)) {
            return {};
        }
        return {positions.data() + offsets[index], static_cast<std::size_t>(postings[index].term_frequency)};
    }
};

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
    const std::vector<Posting>* postings;
};

using TermFreqs =std::unordered_map<std::string, int>;
using FieldTermFreqs = std::unordered_map<std::string, TermFreqs>;
using TermPositions = std::unordered_map<std::string, std::vector<std::uint32_t>>;
using FieldTermPositions = std::unordered_map<std::string, TermPositions>;

class InvertedIndex {
    private:
        using FrequencyMap = std::map<std::string, std::uint32_t, std::less<>>;
        using PostingListMap = std::map<std::string, PostingList, std::less<>>;

        struct FieldIndex {
            PostingListMap terms;
            std::vector<int> lengths;
            long long total_length = 0;
            int documents = 0;
        };

        using FieldMap = std::map<std::string, FieldIndex, std::less<>>;

        struct IndexedField {
            FieldMap::iterator field;
            std::vector<const std::string*> terms;
        };

        FrequencyMap doc_freqs;
        FuzzyIndex fuzzy;
        FieldMap fields;
        std::unordered_map<int, std::vector<IndexedField>> doc_fields;
        int total_documents = 0;

        static std::vector<const std::string*> distinct_terms(const std::vector<IndexedField>& indexed);

        template <typename TermMap>
        void add_fields(int doc_id, const std::unordered_map<std::string, TermMap>& fields_to_add);

    public:
        InvertedIndex() = default;
        InvertedIndex(const InvertedIndex&) = delete;
        InvertedIndex& operator=(const InvertedIndex&) = delete;
        InvertedIndex(InvertedIndex&&) = default;
        InvertedIndex& operator=(InvertedIndex&&) = default;

        void add_document(int doc_id, const FieldTermFreqs& field_term_freqs);
        void add_document(int doc_id, const FieldTermPositions& field_term_positions);
        void remove_document(int doc_id);
        std::size_t get_document_frequency(const std::string& term) const;
        const std::vector<Posting>* get_postings(std::string_view field, const std::string& term) const;
        const PostingList* get_posting_list(std::string_view field, const std::string& term) const;
        std::vector<std::string_view> field_names() const;
        std::vector<FieldPostings> get_field_postings(const std::string& term) const;
        std::vector<TermPostings> terms_with_prefix(std::string_view prefix) const;
        std::vector<TermPostings> terms_with_prefix(std::string_view field, std::string_view prefix) const;

        std::vector<FuzzyMatch> fuzzy_terms(std::string_view term, std::size_t max_distance) const {
            return fuzzy.candidates(term, max_distance);
        }

        int get_total_documents() const { return total_documents; }
        int get_field_documents(std::string_view field) const;
        int get_field_length(std::string_view field, int doc_id) const;
        std::span<const int> get_field_lengths(std::string_view field) const;
        double get_average_field_length(std::string_view field) const;
        std::size_t get_unique_terms_count() const { return doc_freqs.size(); }
        IndexMemory memory_usage() const;

        void write(ByteWriter& writer) const;
        static InvertedIndex read(ByteReader& reader, const DocumentStore& store, bool keep_positions = true);
};
