#include "TermDictionary.h"

#include "Growth.h"

#include <stdexcept>

namespace {

constexpr std::size_t min_slots = 16;
constexpr std::size_t min_dead_text = 64 * 1024;

std::uint64_t slot_of(std::uint32_t tag, TermId id) {
    return (static_cast<std::uint64_t>(tag) << 32) | id;
}

}

TermId TermDictionary::find(std::string_view term, std::uint64_t hash) const {
    if (slots.empty()) {
        return none;
    }
    const std::uint32_t tag = tag_of(hash);
    for (std::size_t at = home_of(tag);; at = after(at)) {
        const std::uint64_t slot = slots[at];
        if (slot == empty_slot) {
            return none;
        }
        if (static_cast<std::uint32_t>(slot >> 32) == tag) {
            const TermId id = static_cast<TermId>(slot);
            if (text_of(id) == term) {
                return id;
            }
        }
    }
}

void TermDictionary::place(std::uint64_t slot) {
    std::size_t at = home_of(static_cast<std::uint32_t>(slot >> 32));
    while (slots[at] != empty_slot) {
        at = after(at);
    }
    slots[at] = slot;
}

void TermDictionary::grow_slots(std::size_t wanted) {
    const std::size_t size = std::max(min_slots, wanted * 5 / 3 + 1);
    if (size <= slots.size()) {
        return;
    }
    std::vector<std::uint64_t> old(size, empty_slot);
    old.swap(slots);
    for (const std::uint64_t slot : old) {
        if (slot != empty_slot) {
            place(slot);
        }
    }
}

void TermDictionary::reserve(std::size_t terms, std::size_t text_bytes) {
    entries.reserve(terms);
    text.reserve(text_bytes);
    grow_slots(terms);
    trie.reserve(terms);
}

TermId TermDictionary::intern(std::string_view term, std::uint64_t hash) {
    const TermId found = find(term, hash);
    if (found != none) {
        return found;
    }
    if (text.size() + term.size() >= dead) {
        throw std::length_error("LuxCore: the distinct terms of one index hold more than 4 GB of text");
    }
    if (free_ids.empty() && entries.size() >= TermTrie::max_terms) {
        throw std::length_error("LuxCore: one index holds fewer than 2^31 distinct terms");
    }
    if ((live + 1) * 4 > slots.size() * 3) {
        grow_slots(live + 1);
    }

    TermId id;
    if (free_ids.empty()) {
        make_room_for(entries, 1);
        id = static_cast<TermId>(entries.size());
        entries.emplace_back();
    } else {
        id = free_ids.back();
        free_ids.pop_back();
    }
    make_room_for(text, term.size());
    entries[id] = Entry{static_cast<std::uint32_t>(text.size()), static_cast<std::uint32_t>(term.size()), 0, none};
    text.insert(text.end(), term.begin(), term.end());
    place(slot_of(tag_of(hash), id));
    live++;
    trie.insert(id, *this);
    return id;
}

void TermDictionary::erase(TermId id) {
    trie.remove(id, *this);

    std::size_t hole = home_of(tag_of(hash_term(text_of(id))));
    while (static_cast<TermId>(slots[hole]) != id) {
        hole = after(hole);
    }
    // Shift the rest of the probe run back so no lookup stops early at the hole.
    for (std::size_t at = after(hole); slots[at] != empty_slot; at = after(at)) {
        const std::size_t home = home_of(static_cast<std::uint32_t>(slots[at] >> 32));
        if (distance(home, at) >= distance(hole, at)) {
            slots[hole] = slots[at];
            hole = at;
        }
    }
    slots[hole] = empty_slot;

    dead_text += entries[id].length;
    entries[id] = Entry{};
    free_ids.push_back(id);
    live--;
    if (dead_text >= min_dead_text && dead_text * 4 >= text.size()) {
        compact_text();
    }
}

void TermDictionary::compact_text() {
    std::vector<char> packed;
    packed.reserve(text.size() - dead_text + (text.size() - dead_text) / 8);
    for (Entry& entry : entries) {
        if (entry.text == dead) {
            continue;
        }
        const std::uint32_t moved = static_cast<std::uint32_t>(packed.size());
        packed.insert(packed.end(), text.begin() + entry.text, text.begin() + entry.text + entry.length);
        entry.text = moved;
    }
    text.swap(packed);
    dead_text = 0;
}

void TermDictionary::memory_stats(MemoryStats& out) const {
    MemoryRow entry_row{"terms.entries"};
    entry_row.items = live;
    entry_row.vector(entries);
    entry_row.vector(free_ids);
    out.push_back(entry_row);

    MemoryRow text_row{"terms.text"};
    text_row.items = text.size() - dead_text;
    text_row.allocation(text.capacity(), text.size() - dead_text);
    out.push_back(text_row);

    MemoryRow slot_row{"terms.hash_slots"};
    slot_row.items = live;
    slot_row.allocation(slots.capacity() * sizeof(std::uint64_t), live * sizeof(std::uint64_t));
    out.push_back(slot_row);

    trie.memory_stats(out);
}
