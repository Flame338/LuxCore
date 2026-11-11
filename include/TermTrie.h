#pragma once

#include "MemoryStats.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

using TermId = std::uint32_t;

class TermDictionary;

struct FuzzyMatch {
    std::string term;
    std::size_t distance;
};

// A radix tree over the bytes of every live term. It owns no text: a node's
// label is a slice of the text of one term below it, read from the dictionary.
// Children are kept in ascending order of their first byte, so a walk yields
// terms in the same order as comparing them as strings.
class TermTrie {
    private:
        static constexpr std::uint32_t none = UINT32_MAX;
        static constexpr std::uint32_t terminal_bit = std::uint32_t{1} << 31;
        static constexpr std::uint32_t max_label = (std::uint32_t{1} << 24) - 1;

        struct Node {
            std::uint32_t first_child = none;
            std::uint32_t next_sibling = none;
            // The term that ends here, with terminal_bit set, or else any term below this node.
            std::uint32_t term = none;
            std::uint32_t label = 0;

            std::uint32_t length() const { return label >> 8; }
            std::uint8_t first_byte() const { return static_cast<std::uint8_t>(label); }
            bool is_terminal() const { return term != none && (term & terminal_bit) != 0; }
            TermId term_id() const { return term & ~terminal_bit; }
            void set_label(std::uint32_t length, std::uint8_t first_byte) { label = (length << 8) | first_byte; }
        };

        struct FuzzySearch;
        struct PendingBytes;

        std::vector<Node> nodes{Node{}};
        std::vector<std::uint32_t> free_nodes;
        // The root has a child for most first bytes, and walking its sibling chain is the longest
        // dependent-load chain of a lookup, so the root's children are also kept by first byte.
        std::array<std::uint32_t, 256> root_children;

        std::uint32_t new_node();
        void release(std::uint32_t node);
        std::uint32_t find_child(std::uint32_t parent, std::uint8_t byte) const;
        std::uint32_t scan_children(std::uint32_t parent, std::uint8_t byte) const;
        void replace_child(std::uint32_t parent, std::uint32_t child, std::uint32_t replacement);
        void merge_into_only_child(std::uint32_t parent, std::uint32_t node);
        std::string_view label_of(const Node& node, std::size_t depth, const TermDictionary& terms) const;
        void collect(std::uint32_t node, std::size_t byte_depth, std::size_t depth, const PendingBytes& pending,
                     FuzzySearch& search) const;

        // Ids waiting for a visit. A leaf chain is walked without touching it, so it stays small.
        struct PendingNodes {
            std::uint32_t inline_ids[48];
            std::size_t inline_size = 0;
            std::vector<std::uint32_t> spilled;

            void push(std::uint32_t node) {
                if (inline_size < std::size(inline_ids)) {
                    inline_ids[inline_size++] = node;
                } else {
                    spilled.push_back(node);
                }
            }
            std::uint32_t pop() {
                if (!spilled.empty()) {
                    const std::uint32_t node = spilled.back();
                    spilled.pop_back();
                    return node;
                }
                return inline_ids[--inline_size];
            }
            bool is_empty() const { return inline_size == 0 && spilled.empty(); }
        };

    public:
        TermTrie() { root_children.fill(none); }

        static constexpr TermId max_terms = terminal_bit - 1;
        static constexpr std::uint32_t no_node = none;

        void insert(TermId id, const TermDictionary& terms);
        void remove(TermId id, const TermDictionary& terms);
        void reserve(std::size_t terms);

        // The node whose subtree holds exactly the terms that start with prefix, or no_node.
        std::uint32_t locate(std::string_view prefix, const TermDictionary& terms) const;

        // Calls visit(id) for every term at or below top, in ascending term order.
        template <typename Visit>
        void for_each_term_below(std::uint32_t top, Visit&& visit) const {
            PendingNodes pending;
            std::uint32_t node = top;
            for (;;) {
                const Node& current = nodes[node];
                if (current.is_terminal()) {
                    visit(current.term_id());
                }
                const bool below_top = node != top;
                if (current.first_child != none) {
                    if (below_top && current.next_sibling != none) {
                        pending.push(current.next_sibling);
                    }
                    node = current.first_child;
                } else if (below_top && current.next_sibling != none) {
                    node = current.next_sibling;
                } else if (!pending.is_empty()) {
                    node = pending.pop();
                } else {
                    return;
                }
            }
        }
        std::vector<FuzzyMatch> candidates(std::string_view term, std::size_t max_distance,
                                           const TermDictionary& terms) const;

        void memory_stats(MemoryStats& out) const;
};
