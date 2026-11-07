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

    std::span<const std::uint32_t> positions_of(std::size_t index) const {
        const std::uint32_t offset = offsets[index];
        if (offset == no_positions) {
            return {};
        }
        return {positions.data() + offset, static_cast<std::size_t>(postings[index].term_frequency)};
    }
};

struct IndexMemory {
    std::size_t posting_bytes;
    std::size_t position_bytes;
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
using TermPositions = std::unordered_map<std::string, std::vector<std::uint32_t>>;
using FieldTermPositions = std::unordered_map<std::string, TermPositions>;

class InvertedIndex {
    private:
        using PostingMap = std::map<std::string, std::vector<Posting>, std::less<>>;
        using PostingListMap = std::map<std::string, PostingList, std::less<>>;

        struct FieldIndex {
            PostingListMap terms;
            std::unordered_map<int, int> lengths;
            long long total_length = 0;
            int documents = 0;
        };

        struct IndexedField {
            std::string field;
            std::vector<std::string> terms;
        };

        PostingMap index;
        FuzzyIndex fuzzy;
        std::map<std::string, FieldIndex, std::less<>> fields;
        std::unordered_map<int, std::vector<IndexedField>> doc_fields;
        int total_documents = 0;

        template <typename TermMap>
        void add_fields(int doc_id, const std::unordered_map<std::string, TermMap>& fields_to_add);

    public:
        void add_document(int doc_id, const FieldTermFreqs& field_term_freqs);
        void add_document(int doc_id, const FieldTermPositions& field_term_positions);
        void remove_document(int doc_id);
        const std::vector<Posting>* get_postings(const std::string& term) const;
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
        double get_average_field_length(std::string_view field) const;
        std::size_t get_unique_terms_count() const { return index.size(); }
        IndexMemory memory_usage() const;

        void write(ByteWriter& writer) const;
        static InvertedIndex read(ByteReader& reader, const DocumentStore& store);
};
