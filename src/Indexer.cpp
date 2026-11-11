#include "Indexer.h"

#include "ByteIO.h"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace {

constexpr std::uint32_t format_magic = 0x4358554C;
constexpr std::uint32_t format_version = 1;
constexpr std::size_t header_size = 16;

}

Indexer::Indexer(const std::string& stop_words_path, IndexOptions options)
    : options(options), analyzer(make_standard_analyzer(stop_words_path)) {}

IndexClocks& index_clocks() {
    static IndexClocks clocks;
    return clocks;
}

PreparedDocument Indexer::prepare(std::span<const FieldText> fields) const {
    auto timed = index_clocks().analyze.time();
    PreparedDocument prepared;
    DocumentTermsBuilder builder(options.positions);
    std::unordered_map<std::string_view, std::uint32_t> next_base;
    for (const FieldText& field : fields) {
        if (field.options.indexed) {
            const std::uint32_t base = next_base[field.name];
            if (field.options.analyzed) {
                std::uint32_t end = base;
                std::optional<std::size_t> slot;
                for (const Token& token : analyzer.analyze_tokens(field.value)) {
                    if (!slot) {
                        slot = builder.field(field.name);
                    }
                    const std::uint32_t position = base + static_cast<std::uint32_t>(token.position);
                    builder.add(*slot, token.term, position);
                    end = position + 2;
                }
                next_base[field.name] = end;
            } else if (!field.value.empty()) {
                builder.add(builder.field(field.name), field.value, base);
                next_base[field.name] = base + 2;
            }
        }
        if (field.options.stored) {
            prepared.stored.add({std::string(field.name), std::string(field.value), field.options});
        }
    }
    prepared.terms = builder.finish();
    return prepared;
}

PreparedDocument Indexer::prepare(const Document& document) const {
    std::vector<FieldText> fields;
    fields.reserve(document.get_fields().size());
    for (const Field& field : document.get_fields()) {
        fields.push_back({field.name, field.value, field.options});
    }
    return prepare(fields);
}

int Indexer::insert(const PreparedDocument& prepared) {
    IndexClocks& clocks = index_clocks();
    const int id = [&] {
        auto timed = clocks.store.time();
        return doc_store.add(prepared.stored);
    }();
    auto timed = clocks.invert.time();
    inverted_index.add_document(id, prepared.terms);
    return id;
}

void Indexer::overwrite(int id, const PreparedDocument& prepared) {
    doc_store.replace(id, prepared.stored);
    inverted_index.add_document(id, prepared.terms);
}

int Indexer::add_prepared(const std::string& key, const PreparedDocument& prepared) {
    if (key.empty()) {
        throw std::invalid_argument("Indexer::add_document: empty key");
    }
    if (prepared.terms.positioned != options.positions) {
        throw std::invalid_argument("Indexer::add_prepared: the document was prepared with a different positions setting");
    }
    const std::optional<int> existing = [&] {
        auto timed = index_clocks().keys.time();
        return doc_keys.find(key);
    }();
    if (existing) {
        overwrite(*existing, prepared);
        return *existing;
    }
    const int id = insert(prepared);
    auto timed = index_clocks().keys.time();
    doc_keys.bind(key, id);
    return id;
}

int Indexer::add_document(const Document& document) {
    return insert(prepare(document));
}

int Indexer::add_document(const std::string& key, const Document& document) {
    return add_prepared(key, prepare(document));
}

bool Indexer::update_document(int id, const Document& document) {
    if (!doc_store.contains(id)) {
        return false;
    }
    overwrite(id, prepare(document));
    return true;
}

bool Indexer::remove_document(int id) {
    if (!doc_store.remove(id)) {
        return false;
    }
    inverted_index.remove_document(id);
    doc_keys.unbind(id);
    return true;
}

bool Indexer::remove_document(const std::string& key) {
    const std::optional<int> id = doc_keys.find(key);
    return id && remove_document(*id);
}

void Indexer::serialize(ByteSink& sink) const {
    ByteWriter writer(sink);
    writer.u32(format_magic);
    writer.u32(format_version);
    writer.u64(0);
    doc_store.write(writer);
    doc_keys.write(writer);
    inverted_index.write(writer);
    writer.flush();

    ByteWriter body_size;
    body_size.u64(writer.size() - header_size);
    sink.overwrite(8, body_size.bytes());
}

std::string Indexer::serialize() const {
    StringSink sink;
    serialize(sink);
    return std::move(sink.bytes);
}

Indexer Indexer::deserialize(std::string_view data, const std::string& stop_words_path, IndexOptions options) {
    ByteReader reader(data);
    if (data.size() < 8) {
        fail_format("file is too short to be a LuxCore index");
    }
    if (reader.u32() != format_magic) {
        fail_format("bad magic number, this is not a LuxCore index");
    }
    const std::uint32_t version = reader.u32();
    if (version != format_version) {
        fail_format("unsupported format version " + std::to_string(version) + ", this build reads version " +
                    std::to_string(format_version));
    }
    if (data.size() < header_size) {
        fail_format("file is truncated inside the header");
    }
    const std::uint64_t body_size = reader.u64();
    if (body_size != reader.remaining()) {
        fail_format("file is truncated or has trailing bytes, the header declares " + std::to_string(body_size) +
                    " bytes of content and the file holds " + std::to_string(reader.remaining()));
    }

    Indexer indexer(stop_words_path, options);
    indexer.doc_store = DocumentStore::read(reader);
    indexer.doc_keys = DocumentKeys::read(reader, indexer.doc_store);
    indexer.inverted_index = InvertedIndex::read(reader, indexer.doc_store, options.positions);
    if (!reader.at_end()) {
        fail_format("unexpected bytes after the index");
    }
    return indexer;
}

MemoryStats Indexer::memory_stats() const {
    MemoryStats stats;
    doc_store.memory_stats(stats);
    doc_keys.memory_stats(stats);
    inverted_index.memory_stats(stats);
    return stats;
}
