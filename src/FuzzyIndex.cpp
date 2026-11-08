#include "FuzzyIndex.h"

#include "EditDistance.h"

#include <algorithm>

FuzzyIndex::FuzzyIndex(const std::vector<std::string>& source_terms) {
    for (const std::string& term : source_terms) {
        add(term);
    }
}

std::uint32_t FuzzyIndex::new_node(char32_t code_point) {
    if (free_nodes.empty()) {
        nodes.push_back(Node{code_point});
        return static_cast<std::uint32_t>(nodes.size() - 1);
    }
    const std::uint32_t node = free_nodes.back();
    free_nodes.pop_back();
    nodes[node] = Node{code_point};
    return node;
}

std::uint32_t FuzzyIndex::find_child(std::uint32_t parent, char32_t code_point) const {
    for (std::uint32_t child = nodes[parent].first_child; child != none; child = nodes[child].next_sibling) {
        if (nodes[child].code_point >= code_point) {
            return nodes[child].code_point == code_point ? child : none;
        }
    }
    return none;
}

std::uint32_t FuzzyIndex::child_or_insert(std::uint32_t parent, char32_t code_point) {
    std::uint32_t previous = none;
    std::uint32_t child = nodes[parent].first_child;
    while (child != none && nodes[child].code_point < code_point) {
        previous = child;
        child = nodes[child].next_sibling;
    }
    if (child != none && nodes[child].code_point == code_point) {
        return child;
    }
    const std::uint32_t created = new_node(code_point);
    nodes[created].next_sibling = child;
    if (previous == none) {
        nodes[parent].first_child = created;
    } else {
        nodes[previous].next_sibling = created;
    }
    return created;
}

void FuzzyIndex::unlink_child(std::uint32_t parent, std::uint32_t child) {
    if (nodes[parent].first_child == child) {
        nodes[parent].first_child = nodes[child].next_sibling;
        return;
    }
    std::uint32_t previous = nodes[parent].first_child;
    while (nodes[previous].next_sibling != child) {
        previous = nodes[previous].next_sibling;
    }
    nodes[previous].next_sibling = nodes[child].next_sibling;
}

std::uint32_t FuzzyIndex::find_node(const std::vector<char32_t>& code_points) const {
    std::uint32_t node = 0;
    for (const char32_t code_point : code_points) {
        node = find_child(node, code_point);
        if (node == none) {
            return none;
        }
    }
    return node;
}

void FuzzyIndex::add(std::string_view term) {
    std::uint32_t node = 0;
    for (const char32_t code_point : to_code_points(term)) {
        node = child_or_insert(node, code_point);
    }

    if (nodes[node].term_id == none) {
        nodes[node].term_id = static_cast<std::uint32_t>(terms.size());
        terms.emplace_back(term);
    }
}

void FuzzyIndex::remove(std::string_view term) {
    std::vector<std::uint32_t> path{0};
    for (const char32_t code_point : to_code_points(term)) {
        const std::uint32_t child = find_child(path.back(), code_point);
        if (child == none) {
            return;
        }
        path.push_back(child);
    }

    const std::uint32_t node = path.back();
    if (nodes[node].term_id == none) {
        return;
    }

    const std::uint32_t id = nodes[node].term_id;
    nodes[node].term_id = none;
    if (id != terms.size() - 1) {
        terms[id] = std::move(terms.back());
        nodes[find_node(to_code_points(terms[id]))].term_id = id;
    }
    terms.pop_back();

    for (std::size_t depth = path.size() - 1; depth > 0; depth--) {
        const std::uint32_t current = path[depth];
        if (nodes[current].first_child != none || nodes[current].term_id != none) {
            break;
        }
        unlink_child(path[depth - 1], current);
        free_nodes.push_back(current);
    }
}

void FuzzyIndex::collect(std::uint32_t node, std::size_t depth, const std::vector<char32_t>& query,
                         std::vector<std::size_t>& rows, std::size_t max_distance,
                         std::vector<FuzzyMatch>& out) const {
    const std::size_t width = query.size() + 1;
    const std::size_t* row = rows.data() + depth * width;
    std::size_t* next = rows.data() + (depth + 1) * width;

    if (nodes[node].term_id != none && row[width - 1] <= max_distance) {
        out.push_back({terms[nodes[node].term_id], row[width - 1]});
    }

    for (std::uint32_t child = nodes[node].first_child; child != none; child = nodes[child].next_sibling) {
        const char32_t code_point = nodes[child].code_point;
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
