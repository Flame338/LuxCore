#include "TermTrie.h"

#include "EditDistance.h"
#include "Growth.h"
#include "TermDictionary.h"
#include "Utf8.h"

#include <algorithm>
#include <bit>
#include <stdexcept>
#include <utility>

// Bytes of a code point that is split across node labels. Decoding matches
// to_code_points: an invalid or cut-off sequence yields one replacement
// character for its first byte and decoding resumes at the next byte.
struct TermTrie::PendingBytes {
    char bytes[4];
    std::uint8_t size = 0;

    void drop(std::size_t count) {
        for (std::size_t i = count; i < size; i++) {
            bytes[i - count] = bytes[i];
        }
        size = static_cast<std::uint8_t>(size - count);
    }

    template <typename Emit>
    void decode_complete(Emit&& emit) {
        while (size > 0) {
            const unsigned char lead = static_cast<unsigned char>(bytes[0]);
            if (lead < 0x80) {
                emit(char32_t{lead});
                drop(1);
                continue;
            }
            const std::size_t length = std::countl_one(lead);
            bool invalid = length < 2 || length > 4;
            for (std::size_t i = 1; !invalid && i < std::min<std::size_t>(size, length); i++) {
                invalid = (static_cast<unsigned char>(bytes[i]) & 0xC0) != 0x80;
            }
            if (invalid) {
                emit(utf8::replacement_character);
                drop(1);
                continue;
            }
            if (size < length) {
                return;
            }
            const utf8::Decoded decoded = utf8::decode(std::string_view(bytes, length), 0);
            emit(decoded.code_point);
            drop(decoded.length);
        }
    }

    template <typename Emit>
    void feed(char byte, Emit&& emit) {
        if (size == 0 && static_cast<unsigned char>(byte) < 0x80) {
            emit(char32_t{static_cast<unsigned char>(byte)});
            return;
        }
        bytes[size++] = byte;
        decode_complete(emit);
    }

    template <typename Emit>
    void finish(Emit&& emit) {
        decode_complete(emit);
        while (size > 0) {
            emit(utf8::replacement_character);
            drop(1);
            decode_complete(emit);
        }
    }
};

struct TermTrie::FuzzySearch {
    const TermDictionary& terms;
    std::vector<char32_t> query;
    std::size_t max_distance;
    std::size_t width;
    std::vector<std::size_t> rows;
    std::vector<std::pair<TermId, std::size_t>> found;

    FuzzySearch(const TermDictionary& terms, std::string_view term, std::size_t max_distance)
        : terms(terms), query(to_code_points(term)), max_distance(max_distance), width(query.size() + 1),
          rows((query.size() + max_distance + 2) * width) {
        for (std::size_t j = 0; j < width; j++) {
            rows[j] = j;
        }
    }

    // Fills the row for depth + 1 and says whether any term below can still match.
    bool step(std::size_t depth, char32_t code_point) {
        const std::size_t length = query.size();
        const std::size_t* row = rows.data() + depth * width;
        std::size_t* next = rows.data() + (depth + 1) * width;
        const std::size_t cap = max_distance + 1;
        const std::size_t next_depth = depth + 1;
        const std::size_t first = next_depth > max_distance ? next_depth - max_distance : 0;
        const std::size_t last = std::min(length, next_depth + max_distance);
        if (first > length) {
            return false;
        }

        std::size_t left = cap;
        std::size_t row_minimum = cap;
        std::size_t j = first;
        if (first == 0) {
            left = std::min(row[0] + 1, cap);
            next[0] = left;
            row_minimum = left;
            j = 1;
        }
        for (; j <= last; j++) {
            const std::size_t substitution = row[j - 1] + (query[j - 1] == code_point ? 0 : 1);
            const std::size_t value = std::min({row[j] + 1, left + 1, substitution, cap});
            next[j] = value;
            left = value;
            row_minimum = std::min(row_minimum, value);
        }
        if (last < length) {
            next[last + 1] = cap;
        }
        return row_minimum <= max_distance;
    }

    void offer(TermId id, std::size_t depth) {
        const std::size_t length = query.size();
        const std::size_t distance = rows[depth * width + length];
        if (depth <= length + max_distance && length <= depth + max_distance && distance <= max_distance) {
            found.emplace_back(id, distance);
        }
    }
};

std::uint32_t TermTrie::new_node() {
    if (free_nodes.empty()) {
        if (nodes.size() >= none - 1) {
            throw std::length_error("LuxCore: the term index of one index outgrew 2^32 nodes");
        }
        make_room_for(nodes, 1);
        nodes.emplace_back();
        return static_cast<std::uint32_t>(nodes.size() - 1);
    }
    const std::uint32_t node = free_nodes.back();
    free_nodes.pop_back();
    nodes[node] = Node{};
    return node;
}

void TermTrie::release(std::uint32_t node) {
    free_nodes.push_back(node);
}

void TermTrie::reserve(std::size_t terms) {
    nodes.reserve(terms + terms / 2 + 1);
}

std::uint32_t TermTrie::find_child(std::uint32_t parent, std::uint8_t byte) const {
    if (parent == 0) {
        return root_children[byte];
    }
    return scan_children(parent, byte);
}

std::uint32_t TermTrie::scan_children(std::uint32_t parent, std::uint8_t byte) const {
    for (std::uint32_t child = nodes[parent].first_child; child != none; child = nodes[child].next_sibling) {
        if (nodes[child].first_byte() >= byte) {
            return nodes[child].first_byte() == byte ? child : none;
        }
    }
    return none;
}

void TermTrie::replace_child(std::uint32_t parent, std::uint32_t child, std::uint32_t replacement) {
    if (nodes[parent].first_child == child) {
        nodes[parent].first_child = replacement;
        return;
    }
    std::uint32_t previous = nodes[parent].first_child;
    while (nodes[previous].next_sibling != child) {
        previous = nodes[previous].next_sibling;
    }
    nodes[previous].next_sibling = replacement;
}

std::string_view TermTrie::label_of(const Node& node, std::size_t depth, const TermDictionary& terms) const {
    return terms.text_of(node.term_id()).substr(depth, node.length());
}

void TermTrie::insert(TermId id, const TermDictionary& terms) {
    const std::string_view text = terms.text_of(id);
    std::uint32_t node = 0;
    std::size_t depth = 0;
    while (depth < text.size()) {
        const std::uint8_t byte = static_cast<std::uint8_t>(text[depth]);
        std::uint32_t previous = none;
        std::uint32_t child = nodes[node].first_child;
        while (child != none && nodes[child].first_byte() < byte) {
            previous = child;
            child = nodes[child].next_sibling;
        }

        if (child == none || nodes[child].first_byte() != byte) {
            const std::uint32_t leaf = new_node();
            const std::uint32_t length =
                static_cast<std::uint32_t>(std::min<std::size_t>(text.size() - depth, max_label));
            nodes[leaf].set_label(length, byte);
            nodes[leaf].term = id;
            nodes[leaf].next_sibling = child;
            if (previous == none) {
                nodes[node].first_child = leaf;
            } else {
                nodes[previous].next_sibling = leaf;
            }
            if (node == 0) {
                root_children[byte] = leaf;
            }
            node = leaf;
            depth += length;
            continue;
        }

        const std::string_view label = label_of(nodes[child], depth, terms);
        const std::string_view rest = text.substr(depth);
        const std::size_t limit = std::min(label.size(), rest.size());
        std::size_t common = 1;
        while (common < limit && label[common] == rest[common]) {
            common++;
        }
        if (common < label.size()) {
            const std::uint8_t next_byte = static_cast<std::uint8_t>(label[common]);
            const std::uint32_t middle = new_node();
            nodes[middle].first_child = child;
            nodes[middle].next_sibling = nodes[child].next_sibling;
            nodes[middle].term = id;
            nodes[middle].set_label(static_cast<std::uint32_t>(common), byte);
            if (previous == none) {
                nodes[node].first_child = middle;
            } else {
                nodes[previous].next_sibling = middle;
            }
            nodes[child].next_sibling = none;
            nodes[child].set_label(nodes[child].length() - static_cast<std::uint32_t>(common), next_byte);
            child = middle;
        }
        if (node == 0) {
            root_children[byte] = child;
        }
        node = child;
        depth += common;
    }
    nodes[node].term = id | terminal_bit;
}

void TermTrie::merge_into_only_child(std::uint32_t parent, std::uint32_t node) {
    const std::uint32_t child = nodes[node].first_child;
    if (nodes[node].is_terminal() || child == none || nodes[child].next_sibling != none ||
        nodes[node].length() + nodes[child].length() > max_label) {
        return;
    }
    nodes[child].set_label(nodes[node].length() + nodes[child].length(), nodes[node].first_byte());
    nodes[child].next_sibling = nodes[node].next_sibling;
    replace_child(parent, node, child);
    release(node);
}

void TermTrie::remove(TermId id, const TermDictionary& terms) {
    const std::string_view text = terms.text_of(id);
    std::vector<std::uint32_t> path{0};
    for (std::size_t depth = 0; depth < text.size(); depth += nodes[path.back()].length()) {
        path.push_back(find_child(path.back(), static_cast<std::uint8_t>(text[depth])));
    }

    // Nodes above the removed term may borrow its text for their label and need another term.
    std::size_t kept = path.size() - 1;
    std::uint32_t substitute = none;
    const std::uint32_t last = path.back();
    if (nodes[last].first_child != none) {
        substitute = nodes[nodes[last].first_child].term_id();
        nodes[last].term = substitute;
        if (kept > 0) {
            merge_into_only_child(path[kept - 1], last);
        }
    } else if (kept == 0) {
        nodes[last].term = none;
    } else {
        do {
            replace_child(path[kept - 1], path[kept], nodes[path[kept]].next_sibling);
            release(path[kept]);
            kept--;
        } while (kept > 0 && nodes[path[kept]].first_child == none && !nodes[path[kept]].is_terminal());

        const std::uint32_t top = path[kept];
        if (kept > 0 && nodes[top].is_terminal()) {
            substitute = nodes[top].term_id();
        } else if (kept > 0) {
            substitute = nodes[nodes[top].first_child].term_id();
            nodes[top].term = substitute;
            merge_into_only_child(path[kept - 1], top);
        }
    }
    for (std::size_t i = 1; i < kept; i++) {
        if (nodes[path[i]].term == id) {
            nodes[path[i]].term = substitute;
        }
    }
    if (!text.empty()) {
        const std::uint8_t first = static_cast<std::uint8_t>(text[0]);
        root_children[first] = scan_children(0, first);
    }
}

std::uint32_t TermTrie::locate(std::string_view prefix, const TermDictionary& terms) const {
    std::uint32_t top = 0;
    for (std::size_t depth = 0; depth < prefix.size(); depth += nodes[top].length()) {
        top = find_child(top, static_cast<std::uint8_t>(prefix[depth]));
        if (top == none) {
            return none;
        }
        const std::string_view label = label_of(nodes[top], depth, terms);
        const std::string_view rest = prefix.substr(depth);
        const std::size_t shared = std::min(label.size(), rest.size());
        if (label.substr(0, shared) != rest.substr(0, shared)) {
            return none;
        }
    }
    return top;
}

void TermTrie::collect(std::uint32_t node, std::size_t byte_depth, std::size_t depth, const PendingBytes& pending,
                       FuzzySearch& search) const {
    for (std::uint32_t child = nodes[node].first_child; child != none; child = nodes[child].next_sibling) {
        const Node& entry = nodes[child];
        std::size_t reached = depth;
        PendingBytes bytes = pending;
        bool alive = true;
        const auto advance = [&](char32_t code_point) {
            if (alive) {
                alive = search.step(reached, code_point);
                reached += alive ? 1 : 0;
            }
        };

        bytes.feed(static_cast<char>(entry.first_byte()), advance);
        if (alive && entry.length() > 1) {
            const std::string_view label = label_of(entry, byte_depth, search.terms);
            for (std::size_t i = 1; alive && i < label.size(); i++) {
                bytes.feed(label[i], advance);
            }
        }
        if (!alive) {
            continue;
        }

        if (entry.is_terminal()) {
            if (bytes.size == 0) {
                search.offer(entry.term_id(), reached);
            } else {
                const std::size_t before = reached;
                PendingBytes tail = bytes;
                tail.finish(advance);
                if (alive) {
                    search.offer(entry.term_id(), reached);
                }
                reached = before;
            }
        }
        if (entry.first_child != none) {
            collect(child, byte_depth + entry.length(), reached, bytes, search);
        }
    }
}

std::vector<FuzzyMatch> TermTrie::candidates(std::string_view term, std::size_t max_distance,
                                             const TermDictionary& terms) const {
    FuzzySearch search(terms, term, max_distance);
    if (nodes[0].is_terminal()) {
        search.offer(nodes[0].term_id(), 0);
    }
    collect(0, 0, 0, PendingBytes{}, search);

    std::vector<FuzzyMatch> matches;
    matches.reserve(search.found.size());
    for (const auto& [id, distance] : search.found) {
        matches.push_back({std::string(terms.text_of(id)), distance});
    }
    std::ranges::sort(matches, [](const FuzzyMatch& a, const FuzzyMatch& b) {
        return a.distance != b.distance ? a.distance < b.distance : a.term < b.term;
    });
    return matches;
}

void TermTrie::memory_stats(MemoryStats& out) const {
    MemoryRow row{"terms.trie_nodes"};
    row.items = nodes.size() - free_nodes.size();
    row.allocation(nodes.capacity() * sizeof(Node), row.items * sizeof(Node));
    row.vector(free_nodes);
    out.push_back(row);
}
