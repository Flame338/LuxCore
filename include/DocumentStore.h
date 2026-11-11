#pragma once

#include "Document.h"
#include "MemoryStats.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class ByteWriter;
class ByteReader;

class DocumentStore {
private:
    static constexpr std::uint32_t absent = UINT32_MAX;

    struct FieldRecord {
        std::uint32_t name;
        std::uint32_t offset;
        std::uint32_t length;
        FieldOptions options;
    };

    struct Slot {
        std::uint32_t first = 0;
        std::uint32_t count = absent;
    };

    std::vector<Slot> slots;
    std::vector<FieldRecord> records;
    std::string text;
    std::vector<std::string> names;
    std::unordered_map<std::string, std::uint32_t> name_ids;
    std::size_t live = 0;
    std::size_t dead_records = 0;
    std::size_t dead_text = 0;

    std::uint32_t name_id(std::string_view name);
    std::optional<std::uint32_t> find_name_id(std::string_view name) const;
    void append(Slot& slot, const Document& document);
    void release(const Slot& slot);
    void compact_if_needed();
    void compact();
    static void reserve_exactly(DocumentStore& store, ByteReader reader, std::uint64_t live_count);

public:
    int add(const Document& document);
    bool contains(int id) const;
    void replace(int id, const Document& document);
    bool remove(int id);
    Document get(int id) const;
    std::optional<std::string_view> value_of(int id, std::string_view field) const;

    std::size_t size() const { return live; }
    std::size_t id_limit() const { return slots.size(); }

    void memory_stats(MemoryStats& out) const;
    void write(ByteWriter& writer) const;
    static DocumentStore read(ByteReader& reader);
};
