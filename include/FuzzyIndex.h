#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct FuzzyMatch {
    std::string term;
    std::size_t distance;
};

class FuzzyIndex {
    private:
        static constexpr std::uint32_t none = UINT32_MAX;

        struct Node {
            char32_t code_point = 0;
            std::uint32_t first_child = none;
            std::uint32_t next_sibling = none;
            std::uint32_t term_id = none;
        };

        std::vector<Node> nodes{Node{}};
        std::vector<std::string> terms;
        std::vector<std::uint32_t> free_nodes;

        std::uint32_t new_node(char32_t code_point);
        std::uint32_t find_child(std::uint32_t parent, char32_t code_point) const;
        std::uint32_t child_or_insert(std::uint32_t parent, char32_t code_point);
        void unlink_child(std::uint32_t parent, std::uint32_t child);
        std::uint32_t find_node(const std::vector<char32_t>& code_points) const;
        void collect(std::uint32_t node, std::size_t depth, const std::vector<char32_t>& query,
                     std::vector<std::size_t>& rows, std::size_t max_distance,
                     std::vector<FuzzyMatch>& out) const;

    public:
        FuzzyIndex() = default;
        explicit FuzzyIndex(const std::vector<std::string>& source_terms);

        void add(std::string_view term);
        void remove(std::string_view term);

        std::vector<FuzzyMatch> candidates(std::string_view term, std::size_t max_distance) const;

        std::size_t size() const { return terms.size(); }
};
