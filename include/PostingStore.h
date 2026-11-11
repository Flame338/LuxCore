#pragma once

#include "MemoryStats.h"
#include "TermTrie.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <span>
#include <vector>

using ListId = std::uint32_t;

struct Posting {
    int doc_id;
    int term_frequency;
};

// The documents of one term in one field, in ascending doc id order. Doc ids
// and frequencies sit in two parallel arrays, and a list stores every
// frequency in the narrowest of 1, 2 or 4 bytes that fits its largest one.
class Postings {
    private:
        const std::uint32_t* docs = nullptr;
        const unsigned char* frequencies = nullptr;
        std::uint32_t count = 0;
        std::uint8_t width = 4;

    public:
        class Iterator {
            private:
                const Postings* list;
                std::size_t index;

            public:
                using value_type = Posting;
                using difference_type = std::ptrdiff_t;

                Iterator(const Postings* list, std::size_t index) : list(list), index(index) {}
                Posting operator*() const { return (*list)[index]; }
                Iterator& operator++() {
                    index++;
                    return *this;
                }
                bool operator==(const Iterator& other) const { return index == other.index; }
        };

        Postings() = default;
        Postings(const std::uint32_t* docs, const unsigned char* frequencies, std::uint32_t count, std::uint8_t width)
            : docs(docs), frequencies(frequencies), count(count), width(width) {}

        std::size_t size() const { return count; }
        bool empty() const { return count == 0; }
        int doc_id(std::size_t index) const { return static_cast<int>(docs[index]); }

        int term_frequency(std::size_t index) const {
            if (width == 1) {
                return frequencies[index];
            }
            if (width == 2) {
                std::uint16_t value;
                std::memcpy(&value, frequencies + index * 2, 2);
                return value;
            }
            std::uint32_t value;
            std::memcpy(&value, frequencies + index * 4, 4);
            return static_cast<int>(value);
        }

        Posting operator[](std::size_t index) const { return {doc_id(index), term_frequency(index)}; }
        Iterator begin() const { return {this, 0}; }
        Iterator end() const { return {this, count}; }

        // The first index at or after from whose doc id is not below doc_id.
        std::size_t lower_bound(int doc_id, std::size_t from = 0) const {
            const std::uint32_t wanted = static_cast<std::uint32_t>(doc_id);
            std::size_t low = from;
            std::size_t high = count;
            while (low < high) {
                const std::size_t middle = low + (high - low) / 2;
                if (docs[middle] < wanted) {
                    low = middle + 1;
                } else {
                    high = middle;
                }
            }
            return low;
        }

        bool contains(int doc_id) const {
            const std::size_t at = lower_bound(doc_id);
            return at < count && docs[at] == static_cast<std::uint32_t>(doc_id);
        }

        std::span<const std::uint32_t> doc_ids() const { return {docs, count}; }

        // Frequencies of one list as the narrowest integer type that holds them.
        template <typename Stored>
        class FrequencyRun {
            private:
                const unsigned char* bytes;

            public:
                explicit FrequencyRun(const unsigned char* bytes) : bytes(bytes) {}

                int operator[](std::size_t index) const {
                    Stored value;
                    std::memcpy(&value, bytes + index * sizeof(Stored), sizeof(Stored));
                    return static_cast<int>(value);
                }
        };

        // Calls visit(doc ids, frequencies, count) once for postings first up to last, with the
        // frequencies typed by the list's width. The consumer loops over both without a call per
        // posting, which GCC would not inline.
        template <typename Visit>
        void for_each_run(std::size_t first, std::size_t last, Visit&& visit) const {
            if (first >= last) {
                return;
            }
            if (width == 1) {
                visit(docs + first, FrequencyRun<std::uint8_t>(frequencies + first), last - first);
            } else if (width == 2) {
                visit(docs + first, FrequencyRun<std::uint16_t>(frequencies + first * 2), last - first);
            } else {
                visit(docs + first, FrequencyRun<std::uint32_t>(frequencies + first * 4), last - first);
            }
        }
};

struct PositionList {
    static constexpr std::uint32_t no_positions = UINT32_MAX;

    std::vector<std::uint32_t> offsets;
    std::vector<std::uint32_t> positions;
};

struct PostingList {
    Postings postings;
    const PositionList* stored_positions = nullptr;

    bool has_positions(std::size_t index) const {
        return stored_positions != nullptr && !stored_positions->offsets.empty() &&
               stored_positions->offsets[index] != PositionList::no_positions;
    }

    std::span<const std::uint32_t> positions_of(std::size_t index) const {
        if (!has_positions(index)) {
            return {};
        }
        return {stored_positions->positions.data() + stored_positions->offsets[index],
                static_cast<std::size_t>(postings.term_frequency(index))};
    }
};

// Owns every posting list. A list belongs to one term and one field, and the
// lists of a term are chained through next.
class PostingStore {
    private:
        struct Single {
            std::uint32_t doc;
            std::uint32_t frequency;
        };

        struct List {
            TermId term;
            ListId next;
            std::uint32_t size;
            std::uint32_t field : 30;
            std::uint32_t width_code : 2;
            // One posting lives here. More live in a block of capacity doc ids followed by
            // capacity frequencies, where the capacity is a function of the size alone.
            union {
                unsigned char* block;
                Single single;
            } storage;
        };

        std::vector<List> lists;
        std::vector<ListId> free_lists;
        std::vector<PositionList> position_lists;
        std::size_t live = 0;

        static std::uint32_t capacity_for(std::uint32_t size);
        static std::size_t block_bytes(std::uint32_t capacity, std::uint8_t width);
        Postings view(const List& list) const;
        void store(List& list, std::span<const std::uint32_t> docs, std::span<const std::uint32_t> frequencies);
        void insert_positions(ListId id, std::size_t index, std::size_t size, std::span<const std::uint32_t> positions,
                              bool positioned);
        void erase_positions(ListId id, std::size_t index, std::uint32_t frequency);
        void release();

    public:
        static constexpr std::uint32_t none = UINT32_MAX;
        static constexpr std::uint32_t max_fields = std::uint32_t{1} << 30;

        PostingStore() = default;
        ~PostingStore() { release(); }
        PostingStore(const PostingStore&) = delete;
        PostingStore& operator=(const PostingStore&) = delete;
        PostingStore(PostingStore&& other) noexcept;
        PostingStore& operator=(PostingStore&& other) noexcept;

        ListId create(TermId term, std::uint32_t field);
        void destroy(ListId id);
        void reserve(std::size_t count) { lists.reserve(count); }

        std::size_t size() const { return live; }
        TermId term_of(ListId id) const { return lists[id].term; }
        std::uint32_t field_of(ListId id) const { return lists[id].field; }
        ListId next_of(ListId id) const { return lists[id].next; }
        void set_next(ListId id, ListId next) { lists[id].next = next; }
        std::size_t size_of(ListId id) const { return lists[id].size; }
        void prefetch(ListId id) const { __builtin_prefetch(&lists[id]); }
        void prefetch_tail(ListId id) const;

        Postings postings(ListId id) const { return view(lists[id]); }
        PostingList posting_list(ListId id) const;

        // The document must not be in the list. Positions are ascending and as many as frequency.
        void insert(ListId id, int doc_id, std::uint32_t frequency);
        void insert(ListId id, int doc_id, std::span<const std::uint32_t> positions);
        void erase(ListId id, int doc_id);
        // Fills an empty list with postings already in ascending doc id order.
        void assign(ListId id, std::span<const std::uint32_t> docs, std::span<const std::uint32_t> frequencies,
                    PositionList positions);

        std::size_t posting_bytes() const;
        std::size_t position_bytes() const;
        void memory_stats(MemoryStats& out) const;
};
