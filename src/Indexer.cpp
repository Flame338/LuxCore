#include "Indexer.h"

#include "ByteIO.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace {

constexpr std::uint32_t format_magic = 0x4358554C;
constexpr std::uint32_t format_version = 1;
constexpr std::size_t header_size = 16;

struct Prepared {
    FieldTermPositions field_term_positions;
    Document stored;
};

Prepared prepare(const Analyzer& analyzer, const Document& document) {
    Prepared prepared;
    std::unordered_map<std::string, std::uint32_t> next_base;
    for (const Field& field : document.get_fields()) {
        if (field.options.indexed) {
            const std::uint32_t base = next_base[field.name];
            if (field.options.analyzed) {
                std::uint32_t end = base;
                for (const Token& token : analyzer.analyze_tokens(field.value)) {
                    const std::uint32_t position = base + static_cast<std::uint32_t>(token.position);
                    prepared.field_term_positions[field.name][token.term].push_back(position);
                    end = position + 2;
                }
                next_base[field.name] = end;
            } else if (!field.value.empty()) {
                prepared.field_term_positions[field.name][field.value].push_back(base);
                next_base[field.name] = base + 2;
            }
        }
        if (field.options.stored) {
            prepared.stored.add(field);
        }
    }
    return prepared;
}

}

Indexer::Indexer(const std::string& stop_words_path)
    : analyzer(make_standard_analyzer(stop_words_path)) {}

int Indexer::add_document(const Document& document) {
    Prepared prepared = prepare(analyzer, document);
    const int id = doc_store.add(std::move(prepared.stored));
    inverted_index.add_document(id, prepared.field_term_positions);
    return id;
}

int Indexer::add_document(const std::string& key, const Document& document) {
    if (key.empty()) {
        throw std::invalid_argument("Indexer::add_document: empty key");
    }
    if (const std::optional<int> existing = doc_keys.find(key)) {
        update_document(*existing, document);
        return *existing;
    }
    const int id = add_document(document);
    doc_keys.bind(key, id);
    return id;
}

bool Indexer::update_document(int id, const Document& document) {
    if (!doc_store.contains(id)) {
        return false;
    }
    Prepared prepared = prepare(analyzer, document);
    doc_store.replace(id, std::move(prepared.stored));
    inverted_index.add_document(id, prepared.field_term_positions);
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

std::string Indexer::serialize() const {
    ByteWriter body;
    doc_store.write(body);
    doc_keys.write(body);
    inverted_index.write(body);

    ByteWriter file;
    file.reserve(header_size + body.size());
    file.u32(format_magic);
    file.u32(format_version);
    file.u64(body.size());
    file.append(body.bytes());
    return file.take();
}

Indexer Indexer::deserialize(std::string_view data, const std::string& stop_words_path) {
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

    Indexer indexer(stop_words_path);
    indexer.doc_store = DocumentStore::read(reader);
    indexer.doc_keys = DocumentKeys::read(reader, indexer.doc_store);
    indexer.inverted_index = InvertedIndex::read(reader, indexer.doc_store);
    if (!reader.at_end()) {
        fail_format("unexpected bytes after the index");
    }
    return indexer;
}
