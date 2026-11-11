#include "DocumentKeys.h"

#include "ByteIO.h"
#include "DocumentStore.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <string>

void DocumentKeys::rehash(std::size_t slots) {
    std::vector<std::uint32_t> grown(slots, empty_slot);
    const std::size_t mask = slots - 1;
    for (const std::uint32_t id : table) {
        if (id == empty_slot || id == tombstone) {
            continue;
        }
        std::size_t i = hash_of(keys[id]) & mask;
        while (grown[i] != empty_slot) {
            i = (i + 1) & mask;
        }
        grown[i] = id;
    }
    table = std::move(grown);
    used = live;
}

void DocumentKeys::reserve(std::size_t count) {
    const std::size_t wanted = std::max(minimum_slots, std::bit_ceil(count * 4 + 4));
    if (wanted > table.size()) {
        rehash(wanted);
    }
}

void DocumentKeys::bind(const std::string& key, int id) {
    if (static_cast<std::size_t>(id) >= keys.size()) {
        keys.resize(static_cast<std::size_t>(id) + 1);
    }
    if ((used + 1) * 2 > table.size()) {
        rehash(std::max(minimum_slots, std::bit_ceil((live + 1) * 4)));
    }

    const std::size_t mask = table.size() - 1;
    std::size_t free_slot = table.size();
    std::size_t i = hash_of(key) & mask;
    for (; table[i] != empty_slot; i = (i + 1) & mask) {
        if (table[i] == tombstone) {
            free_slot = std::min(free_slot, i);
        } else if (keys[table[i]] == key) {
            table[i] = static_cast<std::uint32_t>(id);
            keys[static_cast<std::size_t>(id)] = key;
            return;
        }
    }

    if (free_slot == table.size()) {
        free_slot = i;
        used++;
    }
    table[free_slot] = static_cast<std::uint32_t>(id);
    live++;
    keys[static_cast<std::size_t>(id)] = key;
}

void DocumentKeys::unbind(int id) {
    if (id < 0 || static_cast<std::size_t>(id) >= keys.size() || keys[static_cast<std::size_t>(id)].empty()) {
        return;
    }
    std::string& key = keys[static_cast<std::size_t>(id)];
    if (!table.empty()) {
        const std::size_t mask = table.size() - 1;
        for (std::size_t i = hash_of(key) & mask; table[i] != empty_slot; i = (i + 1) & mask) {
            if (table[i] == static_cast<std::uint32_t>(id)) {
                table[i] = tombstone;
                live--;
                break;
            }
        }
    }
    key.clear();
}

void DocumentKeys::write(ByteWriter& writer) const {
    std::size_t bound = 0;
    for (const std::string& key : keys) {
        bound += key.empty() ? 0 : 1;
    }
    writer.varint(bound);
    std::size_t previous = 0;
    for (std::size_t id = 0; id < keys.size(); id++) {
        if (keys[id].empty()) {
            continue;
        }
        writer.varint(id - previous);
        previous = id;
        writer.string(keys[id]);
    }
}

DocumentKeys DocumentKeys::read(ByteReader& reader, const DocumentStore& store) {
    DocumentKeys result;
    const std::size_t count = reader.count("key count", 2);
    result.reserve(count);
    std::uint64_t id = 0;
    for (std::size_t i = 0; i < count; i++) {
        const std::uint64_t gap = reader.varint();
        if (i > 0 && gap == 0) {
            fail_format("key ids are not ascending");
        }
        if (gap >= store.id_limit()) {
            fail_format("key is bound to a missing document");
        }
        id += gap;
        if (id >= store.id_limit() || !store.contains(static_cast<int>(id))) {
            fail_format("key is bound to a missing document");
        }
        const std::string key(reader.string("key length"));
        if (key.empty()) {
            fail_format("empty key");
        }
        if (result.find(key)) {
            fail_format("duplicate key");
        }
        result.bind(key, static_cast<int>(id));
    }
    return result;
}

void DocumentKeys::memory_stats(MemoryStats& out) const {
    MemoryRow key_row{"keys.strings"};
    key_row.items = live;
    key_row.vector(keys);
    for (const std::string& key : keys) {
        key_row.string(key);
    }
    out.push_back(key_row);

    MemoryRow table_row{"keys.table"};
    table_row.items = table.size();
    table_row.vector(table);
    out.push_back(table_row);
}
