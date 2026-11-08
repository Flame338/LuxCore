#include "FuzzyIndex.h"

#include "EditDistance.h"

#include <algorithm>

FuzzyIndex::FuzzyIndex(const std::vector<std::string>& source_terms) {
    for (const std::string& term : source_terms) {
        add(term);
    }
}

std::size_t FuzzyIndex::new_node() {
    if (free_nodes.empty()) {
        nodes.emplace_back();
        return nodes.size() - 1;
    }
    const std::size_t node = free_nodes.back();
    free_nodes.pop_back();
    nodes[node] = Node{};
    return node;
}

std::size_t FuzzyIndex::find_node(const std::vector<char32_t>& code_points) const {
    std::size_t node = 0;
    for (const char32_t code_point : code_points) {
        const auto child = nodes[node].children.find(code_point);
        if (child == nodes[node].children.end()) {
            return no_term;
        }
        node = child->second;
    }
    return node;
}

void FuzzyIndex::add(std::string_view term) {
    std::size_t node = 0;
    for (const char32_t code_point : to_code_points(term)) {
        auto child = nodes[node].children.find(code_point);
        if (child == nodes[node].children.end()) {
            const std::size_t created = new_node();
            child = nodes[node].children.emplace(code_point, created).first;
        }
        node = child->second;
    }

    if (nodes[node].term_id == no_term) {
        nodes[node].term_id = terms.size();
        terms.emplace_back(term);
    }
}

void FuzzyIndex::remove(std::string_view term) {
    const std::vector<char32_t> code_points = to_code_points(term);
    const std::size_t node = find_node(code_points);
    if (node == no_term || nodes[node].term_id == no_term) {
        return;
    }

    const std::size_t id = nodes[node].term_id;
    nodes[node].term_id = no_term;
    if (id != terms.size() - 1) {
        terms[id] = std::move(terms.back());
        nodes[find_node(to_code_points(terms[id]))].term_id = id;
    }
    terms.pop_back();

    std::size_t current = node;
    for (std::size_t depth = code_points.size(); depth > 0; depth--) {
        if (!nodes[current].children.empty() || nodes[current].term_id != no_term) {
            break;
        }
        const std::size_t parent = find_node({code_points.begin(), code_points.begin() + (depth - 1)});
        nodes[parent].children.erase(code_points[depth - 1]);
        free_nodes.push_back(current);
        current = parent;
    }
}

void FuzzyIndex::collect(std::size_t node, std::size_t depth, const std::vector<char32_t>& query,
                         std::vector<std::size_t>& rows, std::size_t max_distance,
                         std::vector<FuzzyMatch>& out) const {
    const std::size_t width = query.size() + 1;
    const std::size_t* row = rows.data() + depth * width;
    std::size_t* next = rows.data() + (depth + 1) * width;

    if (nodes[node].term_id != no_term && row[width - 1] <= max_distance) {
        out.push_back({terms[nodes[node].term_id], row[width - 1]});
    }

    for (const auto& [code_point, child] : nodes[node].children) {
        next[0] = row[0] + 1;
        std::size_t row_minimum = next[0];
        for (std::size_t j = 1; j < width; j++) {
            const std::size_t substitution = row[j - 1] + (query[j - 1] == code_point ? 0 : 1);
            next[j] = std::min({row[j] + 1, next[j - 1] + 1, substitution});
            row_minimum = std::min(row_minimum, next[j]);
        }
        if (row_minimum <= max_distance) {
            collect(child, depth + 1, query, rows, max_distance, out);
        }
    }
}

std::vector<FuzzyMatch> FuzzyIndex::candidates(std::string_view term, std::size_t max_distance) const {
    const std::vector<char32_t> query = to_code_points(term);
    const std::size_t width = query.size() + 1;
    std::vector<std::size_t> rows((query.size() + max_distance + 2) * width);
    for (std::size_t j = 0; j < width; j++) {
        rows[j] = j;
    }

    std::vector<FuzzyMatch> matches;
    collect(0, 0, query, rows, max_distance, matches);
    std::ranges::sort(matches, [](const FuzzyMatch& a, const FuzzyMatch& b) {
        return a.distance != b.distance ? a.distance < b.distance : a.term < b.term;
    });
    return matches;
}
