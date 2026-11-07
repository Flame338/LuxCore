#include "DocumentStore.h"

#include "ByteIO.h"

#include <climits>
#include <cstdint>
#include <string>

namespace {

constexpr std::uint64_t free_unused_ids = 65536;
constexpr std::uint64_t unused_ids_per_byte = 16;

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

void DocumentStore::write(ByteWriter& writer) const {
    writer.varint(documents.size());
    writer.varint(live);
    std::size_t previous = 0;
    for (std::size_t id = 0; id < documents.size(); id++) {
        if (!documents[id]) {
            continue;
        }
        writer.varint(id - previous);
        previous = id;
        const std::vector<Field>& fields = documents[id]->get_fields();
        writer.varint(fields.size());
        for (const Field& field : fields) {
            writer.string(field.name);
            writer.string(field.value);
            writer.u8(pack(field.options));
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

    store.documents.resize(static_cast<std::size_t>(ids));
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

        Document document;
        const std::size_t field_count = reader.count("field count", 3);
        for (std::size_t f = 0; f < field_count; f++) {
            Field field;
            field.name = std::string(reader.string("field name length"));
            field.value = std::string(reader.string("field value length"));
            field.options = unpack(reader.u8());
            document.add(std::move(field));
        }
        store.documents[static_cast<std::size_t>(id)] = std::move(document);
    }
    store.live = static_cast<std::size_t>(live_count);
    return store;
}
