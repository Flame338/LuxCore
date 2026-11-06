#include "FuzzyIndex.h"

#include "EditDistance.h"

#include <algorithm>

FuzzyIndex::FuzzyIndex(const std::vector<std::string>& source_terms) {
    for (const std::string& term : source_terms) {
        add(term);
    }
}

void FuzzyIndex::add(std::string_view term) {
    std::size_t node = 0;
    for (const char32_t code_point : to_code_points(term)) {
        auto child = nodes[node].children.find(code_point);
        if (child == nodes[node].children.end()) {
            nodes.emplace_back();
            child = nodes[node].children.emplace(code_point, nodes.size() - 1).first;
        }
        node = child->second;
    }

    if (nodes[node].term_id == no_term) {
        nodes[node].term_id = terms.size();
        terms.emplace_back(term);
    }
}

void FuzzyIndex::collect(std::size_t node, const std::vector<char32_t>& query,
                         const std::vector<std::size_t>& row, std::size_t max_distance,
                         std::vector<FuzzyMatch>& out) const {
    if (nodes[node].term_id != no_term && row.back() <= max_distance) {
        out.push_back({terms[nodes[node].term_id], row.back()});
    }

    std::vector<std::size_t> next(row.size());
    for (const auto& [code_point, child] : nodes[node].children) {
        next[0] = row[0] + 1;
        std::size_t row_minimum = next[0];
        for (std::size_t j = 1; j < row.size(); j++) {
            const std::size_t substitution = row[j - 1] + (query[j - 1] == code_point ? 0 : 1);
            next[j] = std::min({row[j] + 1, next[j - 1] + 1, substitution});
            row_minimum = std::min(row_minimum, next[j]);
        }
        if (row_minimum <= max_distance) {
            collect(child, query, next, max_distance, out);
        }
    }
}

std::vector<FuzzyMatch> FuzzyIndex::candidates(std::string_view term, std::size_t max_distance) const {
    const std::vector<char32_t> query = to_code_points(term);
    std::vector<std::size_t> first_row(query.size() + 1);
    for (std::size_t j = 0; j < first_row.size(); j++) {
        first_row[j] = j;
    }

    std::vector<FuzzyMatch> matches;
    collect(0, query, first_row, max_distance, matches);
    std::ranges::sort(matches, [](const FuzzyMatch& a, const FuzzyMatch& b) {
        return a.distance != b.distance ? a.distance < b.distance : a.term < b.term;
    });
    return matches;
}
