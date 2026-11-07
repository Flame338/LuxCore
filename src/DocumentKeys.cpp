#include "DocumentKeys.h"

#include "ByteIO.h"
#include "DocumentStore.h"

#include <cstdint>
#include <string>

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
    result.ids.reserve(count);
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
        if (result.ids.contains(key)) {
            fail_format("duplicate key");
        }
        result.bind(key, static_cast<int>(id));
    }
    return result;
}
