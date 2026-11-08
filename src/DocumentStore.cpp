#include "DocumentStore.h"

#include "ByteIO.h"

#include <climits>
#include <cstdint>
#include <string>

namespace {

constexpr std::uint64_t free_unused_ids = 65536;
constexpr std::uint64_t unused_ids_per_byte = 16;

constexpr std::size_t compact_floor_text = 65536;
constexpr std::size_t compact_floor_records = 4096;

constexpr std::uint8_t stored_bit = 1;
constexpr std::uint8_t indexed_bit = 2;
constexpr std::uint8_t analyzed_bit = 4;

std::uint8_t pack(const FieldOptions& options) {
    return static_cast<std::uint8_t>((options.stored ? stored_bit : 0) | (options.indexed ? indexed_bit : 0) |
                                     (options.analyzed ? analyzed_bit : 0));
}

FieldOptions unpack(std::uint8_t bits) {
    if ((bits & ~(stored_bit | indexed_bit | analyzed_bit)) != 0) {
        fail_format("unknown field option bits " + std::to_string(bits));
    }
    return {(bits & stored_bit) != 0, (bits & indexed_bit) != 0, (bits & analyzed_bit) != 0};
}

}

std::uint32_t DocumentStore::name_id(std::string_view name) {
    if (const std::optional<std::uint32_t> known = find_name_id(name)) {
        return *known;
    }
    const std::uint32_t id = static_cast<std::uint32_t>(names.size());
    names.emplace_back(name);
    name_ids.emplace(names.back(), id);
    return id;
}

std::optional<std::uint32_t> DocumentStore::find_name_id(std::string_view name) const {
    const auto found = name_ids.find(std::string(name));
    if (found == name_ids.end()) {
        return std::nullopt;
    }
    return found->second;
}

void DocumentStore::append(Slot& slot, const Document& document) {
    const std::vector<Field>& fields = document.get_fields();
    std::size_t bytes = 0;
    for (const Field& field : fields) {
        bytes += field.value.size();
    }
    if (text.size() + bytes > UINT32_MAX || records.size() + fields.size() >= absent) {
        throw std::length_error("DocumentStore: stored text is too large");
    }

    slot.first = static_cast<std::uint32_t>(records.size());
    for (const Field& field : fields) {
        records.push_back({name_id(field.name), static_cast<std::uint32_t>(text.size()),
                           static_cast<std::uint32_t>(field.value.size()), field.options});
        text.append(field.value);
    }
    slot.count = static_cast<std::uint32_t>(fields.size());
}

void DocumentStore::release(const Slot& slot) {
    for (std::uint32_t i = 0; i < slot.count; i++) {
        dead_text += records[slot.first + i].length;
    }
    dead_records += slot.count;
}

void DocumentStore::compact_if_needed() {
    const bool text_is_mostly_dead = dead_text > compact_floor_text && dead_text > text.size() / 2;
    const bool records_are_mostly_dead = dead_records > compact_floor_records && dead_records > records.size() / 2;
    if (text_is_mostly_dead || records_are_mostly_dead) {
        compact();
    }
}

void DocumentStore::compact() {
    std::string kept_text;
    kept_text.reserve(text.size() - dead_text);
    std::vector<FieldRecord> kept_records;
    kept_records.reserve(records.size() - dead_records);

    for (Slot& slot : slots) {
        if (slot.count == absent) {
            continue;
        }
        const std::uint32_t first = static_cast<std::uint32_t>(kept_records.size());
        for (std::uint32_t i = 0; i < slot.count; i++) {
            const FieldRecord& record = records[slot.first + i];
            kept_records.push_back({record.name, static_cast<std::uint32_t>(kept_text.size()), record.length,
                                    record.options});
            kept_text.append(text, record.offset, record.length);
        }
        slot.first = first;
    }

    text = std::move(kept_text);
    records = std::move(kept_records);
    dead_text = 0;
    dead_records = 0;
}

int DocumentStore::add(const Document& document) {
    slots.emplace_back();
    try {
        append(slots.back(), document);
    } catch (...) {
        slots.pop_back();
        throw;
    }
    live++;
    return static_cast<int>(slots.size() - 1);
}

bool DocumentStore::contains(int id) const {
    return id >= 0 && static_cast<std::size_t>(id) < slots.size() && slots[static_cast<std::size_t>(id)].count != absent;
}

void DocumentStore::replace(int id, const Document& document) {
    if (!contains(id)) {
        throw std::out_of_range("DocumentStore::replace");
    }
    Slot fresh;
    append(fresh, document);
    Slot& slot = slots[static_cast<std::size_t>(id)];
    release(slot);
    slot = fresh;
    compact_if_needed();
}

bool DocumentStore::remove(int id) {
    if (!contains(id)) {
        return false;
    }
    Slot& slot = slots[static_cast<std::size_t>(id)];
    release(slot);
    slot.count = absent;
    live--;
    compact_if_needed();
    return true;
}

Document DocumentStore::get(int id) const {
    if (!contains(id)) {
        throw std::out_of_range("DocumentStore::get");
    }
    const Slot& slot = slots[static_cast<std::size_t>(id)];
    Document document;
    for (std::uint32_t i = 0; i < slot.count; i++) {
        const FieldRecord& record = records[slot.first + i];
        document.add({names[record.name], text.substr(record.offset, record.length), record.options});
    }
    return document;
}

std::optional<std::string_view> DocumentStore::value_of(int id, std::string_view field) const {
    if (!contains(id)) {
        return std::nullopt;
    }
    const std::optional<std::uint32_t> wanted = find_name_id(field);
    if (!wanted) {
        return std::nullopt;
    }
    const Slot& slot = slots[static_cast<std::size_t>(id)];
    for (std::uint32_t i = 0; i < slot.count; i++) {
        const FieldRecord& record = records[slot.first + i];
        if (record.name == *wanted) {
            return std::string_view(text).substr(record.offset, record.length);
        }
    }
    return std::nullopt;
}

void DocumentStore::write(ByteWriter& writer) const {
    writer.varint(slots.size());
    writer.varint(live);
    std::size_t previous = 0;
    for (std::size_t id = 0; id < slots.size(); id++) {
        const Slot& slot = slots[id];
        if (slot.count == absent) {
            continue;
        }
        writer.varint(id - previous);
        previous = id;
        writer.varint(slot.count);
        for (std::uint32_t i = 0; i < slot.count; i++) {
            const FieldRecord& record = records[slot.first + i];
            writer.string(names[record.name]);
            writer.string(std::string_view(text).substr(record.offset, record.length));
            writer.u8(pack(record.options));
        }
    }
}

DocumentStore DocumentStore::read(ByteReader& reader) {
    DocumentStore store;
    const std::uint64_t ids = reader.varint_at_most("document id count", INT_MAX);
    const std::uint64_t live_count = reader.varint_at_most("document count", ids);
    if (live_count > reader.remaining() / 2) {
        fail_format("document count " + std::to_string(live_count) + " does not fit in the remaining " +
                    std::to_string(reader.remaining()) + " bytes");
    }
    const std::uint64_t unused = ids - live_count;
    if (unused > free_unused_ids + unused_ids_per_byte * reader.total_size()) {
        fail_format("document id count " + std::to_string(ids) + " is implausible for a file of " +
                    std::to_string(reader.total_size()) + " bytes");
    }

    store.slots.resize(static_cast<std::size_t>(ids));
    std::uint64_t id = 0;
    for (std::uint64_t i = 0; i < live_count; i++) {
        const std::uint64_t gap = reader.varint();
        if (i > 0 && gap == 0) {
            fail_format("document ids are not ascending");
        }
        if (gap >= ids) {
            fail_format("document id is out of range");
        }
        id += gap;
        if (id >= ids) {
            fail_format("document id is out of range");
        }

        const std::size_t field_count = reader.count("field count", 3);
        Slot& slot = store.slots[static_cast<std::size_t>(id)];
        slot.first = static_cast<std::uint32_t>(store.records.size());
        for (std::size_t f = 0; f < field_count; f++) {
            const std::string_view name = reader.string("field name length");
            const std::string_view value = reader.string("field value length");
            const FieldOptions options = unpack(reader.u8());
            if (store.text.size() + value.size() > UINT32_MAX || store.records.size() + 1 >= absent) {
                fail_format("stored text is too large");
            }
            store.records.push_back({store.name_id(name), static_cast<std::uint32_t>(store.text.size()),
                                     static_cast<std::uint32_t>(value.size()), options});
            store.text.append(value);
        }
        slot.count = static_cast<std::uint32_t>(field_count);
    }
    store.live = static_cast<std::size_t>(live_count);
    return store;
}
