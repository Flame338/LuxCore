#pragma once

#include "MemoryStats.h"
#include "TermTrie.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

inline std::uint64_t hash_term(std::string_view term) {
    constexpr std::uint64_t multiplier = 0x9FB21C651E98DF25ULL;
    std::uint64_t hash = 0x9E3779B97F4A7C15ULL ^ (term.size() * 0xFF51AFD7ED558CCDULL);
    const char* bytes = term.data();
    std::size_t left = term.size();
    while (left >= 8) {
        std::uint64_t word;
        std::memcpy(&word, bytes, 8);
        hash = (hash ^ word) * multiplier;
        hash ^= hash >> 32;
        bytes += 8;
        left -= 8;
    }
    if (left > 0) {
        std::uint64_t word = 0;
        std::memcpy(&word, bytes, left);
        hash = (hash ^ word) * multiplier;
        hash ^= hash >> 32;
    }
    hash *= 0xFF51AFD7ED558CCDULL;
    return hash ^ (hash >> 33);
}

// Every distinct term of the index, stored once. A term has a dense id, its
// document frequency, and the head of its chain of per-field posting lists.
class TermDictionary {
    public:
        static constexpr std::uint32_t none = UINT32_MAX;

    private:
        static constexpr std::uint32_t dead = UINT32_MAX;
        static constexpr std::uint64_t empty_slot = UINT64_MAX;

        struct Entry {
            std::uint32_t text = dead;
            std::uint32_t length = 0;
            std::uint32_t document_frequency = 0;
            std::uint32_t first_list = none;
        };

        std::vector<Entry> entries;
        std::vector<TermId> free_ids;
        std::vector<char> text;
        std::size_t dead_text = 0;
        // Each slot is the upper half of the term's hash above its id. The table is not a power of
        // two, so a hash maps to a slot by multiplying, and growth stays within a quarter of use.
        std::vector<std::uint64_t> slots;
        std::size_t live = 0;
        TermTrie trie;

        static std::uint32_t tag_of(std::uint64_t hash) { return static_cast<std::uint32_t>(hash >> 32); }
        std::size_t home_of(std::uint32_t tag) const { return (static_cast<std::uint64_t>(tag) * slots.size()) >> 32; }
        std::size_t after(std::size_t at) const { return at + 1 == slots.size() ? 0 : at + 1; }
        // How far a slot lies past the slot a probe run starts from.
        std::size_t distance(std::size_t from, std::size_t to) const { return to >= from ? to - from : to + slots.size() - from; }
        void grow_slots(std::size_t wanted);
        void place(std::uint64_t slot);
        void compact_text();

    public:
        std::size_t size() const { return live; }

        std::string_view text_of(TermId id) const {
            const Entry& entry = entries[id];
            return {text.data() + entry.text, entry.length};
        }

        std::uint32_t document_frequency(TermId id) const { return entries[id].document_frequency; }
        std::uint32_t& document_frequency(TermId id) { return entries[id].document_frequency; }
        std::uint32_t first_list(TermId id) const { return entries[id].first_list; }
        std::uint32_t& first_list(TermId id) { return entries[id].first_list; }

        // A lookup reads a slot, then the entry it names, then that entry's text. Fetching each
        // step for a whole batch before the next step lets the cache misses overlap.
        void prefetch_slot(std::uint64_t hash) const {
            if (!slots.empty()) {
                __builtin_prefetch(&slots[home_of(tag_of(hash))]);
            }
        }
        void prefetch_entry(std::uint64_t hash) const {
            if (!slots.empty()) {
                const std::uint64_t slot = slots[home_of(tag_of(hash))];
                if (slot != empty_slot) {
                    __builtin_prefetch(&entries[static_cast<std::uint32_t>(slot)]);
                }
            }
        }
        void prefetch_text(std::uint64_t hash) const {
            if (!slots.empty()) {
                const std::uint64_t slot = slots[home_of(tag_of(hash))];
                if (slot != empty_slot) {
                    __builtin_prefetch(text.data() + entries[static_cast<std::uint32_t>(slot)].text);
                }
            }
        }

        TermId find(std::string_view term, std::uint64_t hash) const;
        TermId find(std::string_view term) const { return find(term, hash_term(term)); }
        TermId intern(std::string_view term, std::uint64_t hash);
        TermId intern(std::string_view term) { return intern(term, hash_term(term)); }
        void erase(TermId id);
        void reserve(std::size_t terms, std::size_t text_bytes);

        // Calls visit(id) for the live terms that start with prefix, in ascending term order.
        template <typename Visit>
        void for_each_with_prefix(std::string_view prefix, Visit&& visit) const {
            const std::uint32_t top = trie.locate(prefix, *this);
            if (top != TermTrie::no_node) {
                trie.for_each_term_below(top, visit);
            }
        }

        void with_prefix(std::string_view prefix, std::vector<TermId>& out) const {
            for_each_with_prefix(prefix, [&out](TermId id) { out.push_back(id); });
        }

        std::vector<FuzzyMatch> fuzzy(std::string_view term, std::size_t max_distance) const {
            return trie.candidates(term, max_distance, *this);
        }

        void memory_stats(MemoryStats& out) const;
};
