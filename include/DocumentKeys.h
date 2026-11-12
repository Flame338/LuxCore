#pragma once

#include "MemoryStats.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class ByteWriter;
class ByteReader;
class DocumentStore;

class DocumentKeys {
private:
    static constexpr std::uint32_t empty_slot = UINT32_MAX;
    static constexpr std::uint32_t tombstone = UINT32_MAX - 1;
    static constexpr std::size_t minimum_slots = 16;

    std::vector<std::string> keys;
    std::vector<std::uint32_t> table;
    std::size_t used = 0;
    std::size_t live = 0;

    static std::size_t hash_of(std::string_view key) { return std::hash<std::string_view>{}(key); }

    void rehash(std::size_t slots);

public:
    std::optional<int> find(std::string_view key) const {
        if (table.empty()) {
            return std::nullopt;
        }
        const std::size_t mask = table.size() - 1;
        for (std::size_t i = hash_of(key) & mask;; i = (i + 1) & mask) {
            const std::uint32_t id = table[i];
            if (id == empty_slot) {
                return std::nullopt;
            }
            if (id != tombstone && keys[id] == key) {
                return static_cast<int>(id);
            }
        }
    }

    const std::string& key_of(int id) const {
        static const std::string none;
        if (id < 0 || static_cast<std::size_t>(id) >= keys.size()) {
            return none;
        }
        return keys[static_cast<std::size_t>(id)];
    }

    void reserve(std::size_t count);
    void bind(const std::string& key, int id);
    void unbind(int id);

    void memory_stats(MemoryStats& out) const;
    void write(ByteWriter& writer) const;
    static DocumentKeys read(ByteReader& reader, const DocumentStore& store);
};
