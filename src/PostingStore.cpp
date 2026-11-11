#include "PostingStore.h"

#include "Growth.h"

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <new>
#include <stdexcept>
#include <utility>

namespace {

constexpr std::uint8_t widths[] = {1, 2, 4};

std::uint8_t code_for(std::uint32_t frequency) {
    return frequency <= UINT8_MAX ? 0 : frequency <= UINT16_MAX ? 1 : 2;
}

unsigned char* allocate(std::size_t bytes) {
    void* block = std::malloc(bytes);
    if (block == nullptr) {
        throw std::bad_alloc();
    }
    return static_cast<unsigned char*>(block);
}

void put_frequency(unsigned char* frequencies, std::uint8_t width, std::size_t index, std::uint32_t value) {
    if (width == 1) {
        frequencies[index] = static_cast<unsigned char>(value);
    } else if (width == 2) {
        const std::uint16_t narrow = static_cast<std::uint16_t>(value);
        std::memcpy(frequencies + index * 2, &narrow, 2);
    } else {
        std::memcpy(frequencies + index * 4, &value, 4);
    }
}

}

std::uint32_t PostingStore::capacity_for(std::uint32_t size) {
    // Up to 4 and up to 8 postings fill one small heap block each. Larger lists keep three
    // significant bits of the size, which wastes under a quarter.
    if (size <= 8) {
        return size <= 4 ? 4 : 8;
    }
    const int shift = std::bit_width(size) - 3;
    const std::uint64_t rounded = ((static_cast<std::uint64_t>(size) + (1u << shift) - 1) >> shift) << shift;
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(rounded, UINT32_MAX));
}

std::size_t PostingStore::block_bytes(std::uint32_t capacity, std::uint8_t width) {
    return static_cast<std::size_t>(capacity) * (sizeof(std::uint32_t) + width);
}

PostingStore::PostingStore(PostingStore&& other) noexcept
    : lists(std::move(other.lists)), free_lists(std::move(other.free_lists)),
      position_lists(std::move(other.position_lists)), live(std::exchange(other.live, 0)) {
    other.lists.clear();
}

PostingStore& PostingStore::operator=(PostingStore&& other) noexcept {
    if (this != &other) {
        release();
        lists = std::move(other.lists);
        free_lists = std::move(other.free_lists);
        position_lists = std::move(other.position_lists);
        live = std::exchange(other.live, 0);
        other.lists.clear();
    }
    return *this;
}

void PostingStore::release() {
    for (const List& list : lists) {
        if (list.size >= 2) {
            std::free(list.storage.block);
        }
    }
    lists.clear();
}

Postings PostingStore::view(const List& list) const {
    if (list.size == 1) {
        return {&list.storage.single.doc, reinterpret_cast<const unsigned char*>(&list.storage.single.frequency), 1,
                4};
    }
    if (list.size == 0) {
        return {};
    }
    const std::uint8_t width = widths[list.width_code];
    const std::uint32_t capacity = capacity_for(list.size);
    return {reinterpret_cast<const std::uint32_t*>(list.storage.block),
            list.storage.block + static_cast<std::size_t>(capacity) * sizeof(std::uint32_t), list.size, width};
}

PostingList PostingStore::posting_list(ListId id) const {
    return {view(lists[id]), id < position_lists.size() ? &position_lists[id] : nullptr};
}

void PostingStore::prefetch_tail(ListId id) const {
    const List& list = lists[id];
    if (list.size >= 2) {
        const std::size_t capacity = capacity_for(list.size);
        __builtin_prefetch(list.storage.block + static_cast<std::size_t>(list.size) * sizeof(std::uint32_t));
        __builtin_prefetch(list.storage.block + capacity * sizeof(std::uint32_t) +
                           static_cast<std::size_t>(list.size) * widths[list.width_code]);
    }
}

ListId PostingStore::create(TermId term, std::uint32_t field) {
    if (field >= max_fields) {
        throw std::length_error("LuxCore: one index holds at most 2^30 fields");
    }
    ListId id;
    if (free_lists.empty()) {
        if (lists.size() >= none) {
            throw std::length_error("LuxCore: one index holds at most 2^32 posting lists");
        }
        make_room_for(lists, 1);
        id = static_cast<ListId>(lists.size());
        lists.emplace_back();
    } else {
        id = free_lists.back();
        free_lists.pop_back();
    }
    List& list = lists[id];
    list.term = term;
    list.next = none;
    list.size = 0;
    list.field = field;
    list.width_code = 0;
    list.storage.block = nullptr;
    live++;
    return id;
}

void PostingStore::destroy(ListId id) {
    if (id < position_lists.size()) {
        position_lists[id] = PositionList{};
    }
    free_lists.push_back(id);
    live--;
}

void PostingStore::store(List& list, std::span<const std::uint32_t> docs,
                         std::span<const std::uint32_t> frequencies) {
    list.size = static_cast<std::uint32_t>(docs.size());
    if (docs.size() == 1) {
        list.storage.single = {docs[0], frequencies[0]};
        return;
    }
    std::uint8_t code = 0;
    for (const std::uint32_t frequency : frequencies) {
        code = std::max(code, code_for(frequency));
    }
    const std::uint8_t width = widths[code];
    const std::uint32_t capacity = capacity_for(list.size);
    unsigned char* block = allocate(block_bytes(capacity, width));
    std::memcpy(block, docs.data(), docs.size_bytes());
    unsigned char* stored = block + static_cast<std::size_t>(capacity) * sizeof(std::uint32_t);
    for (std::size_t i = 0; i < frequencies.size(); i++) {
        put_frequency(stored, width, i, frequencies[i]);
    }
    list.width_code = code;
    list.storage.block = block;
}

void PostingStore::assign(ListId id, std::span<const std::uint32_t> docs, std::span<const std::uint32_t> frequencies,
                          PositionList positions) {
    store(lists[id], docs, frequencies);
    if (!positions.offsets.empty()) {
        if (id >= position_lists.size()) {
            position_lists.resize(lists.size());
        }
        position_lists[id] = std::move(positions);
    }
}

void PostingStore::insert(ListId id, int doc_id, std::uint32_t frequency) {
    List& list = lists[id];
    const std::uint32_t doc = static_cast<std::uint32_t>(doc_id);
    const std::uint32_t size = list.size;
    if (size == 0) {
        list.storage.single = {doc, frequency};
        list.size = 1;
        insert_positions(id, 0, 1, {}, false);
        return;
    }

    const Postings old = view(list);
    const std::size_t index = old.doc_ids().back() < doc ? size : old.lower_bound(doc_id);
    std::uint8_t code = code_for(frequency);
    code = std::max(code, size == 1 ? code_for(list.storage.single.frequency) : static_cast<std::uint8_t>(list.width_code));
    const std::uint8_t width = widths[code];
    const std::uint32_t capacity = capacity_for(size + 1);
    const std::size_t after = size - index;

    if (size >= 2 && capacity == capacity_for(size) && code == list.width_code) {
        std::uint32_t* docs = reinterpret_cast<std::uint32_t*>(list.storage.block);
        unsigned char* stored = list.storage.block + static_cast<std::size_t>(capacity) * sizeof(std::uint32_t);
        std::memmove(docs + index + 1, docs + index, after * sizeof(std::uint32_t));
        std::memmove(stored + (index + 1) * width, stored + index * width, after * width);
        docs[index] = doc;
        put_frequency(stored, width, index, frequency);
    } else {
        unsigned char* block = allocate(block_bytes(capacity, width));
        std::uint32_t* docs = reinterpret_cast<std::uint32_t*>(block);
        unsigned char* stored = block + static_cast<std::size_t>(capacity) * sizeof(std::uint32_t);
        const std::uint32_t* old_docs = old.doc_ids().data();
        std::memcpy(docs, old_docs, index * sizeof(std::uint32_t));
        docs[index] = doc;
        std::memcpy(docs + index + 1, old_docs + index, after * sizeof(std::uint32_t));
        for (std::size_t i = 0; i < index; i++) {
            put_frequency(stored, width, i, static_cast<std::uint32_t>(old.term_frequency(i)));
        }
        put_frequency(stored, width, index, frequency);
        for (std::size_t i = index; i < size; i++) {
            put_frequency(stored, width, i + 1, static_cast<std::uint32_t>(old.term_frequency(i)));
        }
        if (size >= 2) {
            std::free(list.storage.block);
        }
        list.storage.block = block;
        list.width_code = code;
    }
    list.size = size + 1;
    insert_positions(id, index, size + 1, {}, false);
}

void PostingStore::insert(ListId id, int doc_id, std::span<const std::uint32_t> positions) {
    const std::size_t stored = id < position_lists.size() ? position_lists[id].positions.size() : 0;
    if (stored + positions.size() >= PositionList::no_positions) {
        throw std::length_error("LuxCore: one posting list holds at most 2^32 positions");
    }
    // The plain insert has already made room in the offsets when the list keeps positions.
    insert(id, doc_id, static_cast<std::uint32_t>(positions.size()));
    const std::size_t index = view(lists[id]).lower_bound(doc_id);
    insert_positions(id, index, lists[id].size, positions, true);
}

void PostingStore::insert_positions(ListId id, std::size_t index, std::size_t size,
                                    std::span<const std::uint32_t> positions, bool positioned) {
    const bool keeps_offsets = id < position_lists.size() && !position_lists[id].offsets.empty();
    if (!positioned) {
        if (keeps_offsets) {
            PositionList& list = position_lists[id];
            make_room_for(list.offsets, 1);
            list.offsets.insert(list.offsets.begin() + index, PositionList::no_positions);
        }
        return;
    }
    if (id >= position_lists.size()) {
        position_lists.resize(lists.size());
    }
    PositionList& list = position_lists[id];
    if (!keeps_offsets) {
        list.offsets.assign(size, PositionList::no_positions);
    }
    list.offsets[index] = static_cast<std::uint32_t>(list.positions.size());
    make_room_for(list.positions, positions.size());
    list.positions.insert(list.positions.end(), positions.begin(), positions.end());
}

void PostingStore::erase_positions(ListId id, std::size_t index, std::uint32_t frequency) {
    if (id >= position_lists.size() || position_lists[id].offsets.empty()) {
        return;
    }
    PositionList& list = position_lists[id];
    const std::uint32_t offset = list.offsets[index];
    list.offsets.erase(list.offsets.begin() + index);
    if (offset == PositionList::no_positions) {
        return;
    }
    list.positions.erase(list.positions.begin() + offset, list.positions.begin() + offset + frequency);
    for (std::uint32_t& other : list.offsets) {
        if (other != PositionList::no_positions && other > offset) {
            other -= frequency;
        }
    }
}

void PostingStore::erase(ListId id, int doc_id) {
    List& list = lists[id];
    const Postings old = view(list);
    const std::uint32_t size = list.size;
    const std::size_t index = old.lower_bound(doc_id);
    if (index == size || old.doc_id(index) != doc_id) {
        return;
    }
    erase_positions(id, index, static_cast<std::uint32_t>(old.term_frequency(index)));

    const std::size_t after = size - index - 1;
    if (size == 1) {
        list.size = 0;
        return;
    }
    if (size == 2) {
        const Posting kept = old[1 - index];
        std::free(list.storage.block);
        list.storage.single = {static_cast<std::uint32_t>(kept.doc_id), static_cast<std::uint32_t>(kept.term_frequency)};
        list.size = 1;
        return;
    }

    const std::uint8_t width = widths[list.width_code];
    const std::uint32_t old_capacity = capacity_for(size);
    const std::uint32_t capacity = capacity_for(size - 1);
    std::uint32_t* old_docs = reinterpret_cast<std::uint32_t*>(list.storage.block);
    unsigned char* old_stored = list.storage.block + static_cast<std::size_t>(old_capacity) * sizeof(std::uint32_t);
    if (capacity == old_capacity) {
        std::memmove(old_docs + index, old_docs + index + 1, after * sizeof(std::uint32_t));
        std::memmove(old_stored + index * width, old_stored + (index + 1) * width, after * width);
    } else {
        unsigned char* block = allocate(block_bytes(capacity, width));
        std::uint32_t* docs = reinterpret_cast<std::uint32_t*>(block);
        unsigned char* stored = block + static_cast<std::size_t>(capacity) * sizeof(std::uint32_t);
        std::memcpy(docs, old_docs, index * sizeof(std::uint32_t));
        std::memcpy(docs + index, old_docs + index + 1, after * sizeof(std::uint32_t));
        std::memcpy(stored, old_stored, index * width);
        std::memcpy(stored + index * width, old_stored + (index + 1) * width, after * width);
        std::free(list.storage.block);
        list.storage.block = block;
    }
    list.size = size - 1;
}

std::size_t PostingStore::posting_bytes() const {
    std::size_t bytes = live * sizeof(List);
    for (const List& list : lists) {
        if (list.size >= 2) {
            bytes += block_bytes(capacity_for(list.size), widths[list.width_code]);
        }
    }
    return bytes;
}

std::size_t PostingStore::position_bytes() const {
    std::size_t bytes = 0;
    for (const PositionList& list : position_lists) {
        bytes += (list.offsets.capacity() + list.positions.capacity()) * sizeof(std::uint32_t);
    }
    return bytes;
}

void PostingStore::memory_stats(MemoryStats& out) const {
    MemoryRow list_row{"postings.lists"};
    list_row.items = live;
    list_row.allocation(lists.capacity() * sizeof(List), live * sizeof(List));
    list_row.vector(free_lists);
    out.push_back(list_row);

    MemoryRow inline_row{"postings.inline"};
    MemoryRow block_row{"postings.blocks"};
    for (const List& list : lists) {
        if (list.size == 1) {
            inline_row.items++;
        } else if (list.size >= 2) {
            const std::uint8_t width = widths[list.width_code];
            block_row.items += list.size;
            block_row.allocation(block_bytes(capacity_for(list.size), width),
                                 static_cast<std::size_t>(list.size) * (sizeof(std::uint32_t) + width));
        }
    }
    out.push_back(inline_row);
    out.push_back(block_row);

    MemoryRow position_row{"postings.positions"};
    position_row.vector(position_lists);
    for (const PositionList& list : position_lists) {
        position_row.items += list.positions.size();
        position_row.vector(list.offsets);
        position_row.vector(list.positions);
    }
    out.push_back(position_row);
}
