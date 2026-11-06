#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

struct FuzzyMatch {
    std::string term;
    std::size_t distance;
};

class FuzzyIndex {
    private:
        static constexpr std::size_t no_term = static_cast<std::size_t>(-1);

        struct Node {
            std::map<char32_t, std::size_t> children;
            std::size_t term_id = no_term;
        };

        std::vector<Node> nodes{Node{}};
        std::vector<std::string> terms;

        void collect(std::size_t node, const std::vector<char32_t>& query,
                     const std::vector<std::size_t>& row, std::size_t max_distance,
                     std::vector<FuzzyMatch>& out) const;

    public:
        FuzzyIndex() = default;
        explicit FuzzyIndex(const std::vector<std::string>& source_terms);

        void add(std::string_view term);

        std::vector<FuzzyMatch> candidates(std::string_view term, std::size_t max_distance) const;

        std::size_t size() const { return terms.size(); }
};
