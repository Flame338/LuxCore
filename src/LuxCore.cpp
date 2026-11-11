#include "LuxCore.h"

#include "ByteIO.h"
#include "EditDistance.h"
#include "FairSharedMutex.h"
#include "Highlighter.h"
#include "Indexer.h"
#include "MappedFile.h"
#include "MatchPlan.h"
#include "MemoryStats.h"
#include "Query.h"
#include "QueryParser.h"
#include "Scoring.h"
#include "Searcher.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <system_error>

namespace luxcore {

QueryError::QueryError(const std::string& message, std::size_t offset)
    : std::invalid_argument(message), error_offset(offset) {}

std::size_t QueryError::offset() const {
    return error_offset;
}

struct Index::Impl {
    Indexer indexer;
    bool positions;
    mutable FairSharedMutex mutex;

    explicit Impl(IndexConfig config)
        : indexer(std::string(), IndexOptions{.positions = config.positions}), positions(config.positions) {}

    Searcher searcher() const {
        return Searcher(indexer, {.scoring = Scoring::bm25_with(), .fuzziness = Fuzziness::On});
    }

    std::unique_ptr<Query> parse(const std::string& text) const {
        ParseResult parsed = QueryParser(indexer.get_analyzer()).parse(text);
        if (!parsed.ok()) {
            throw QueryError(parsed.error->message, parsed.error->offset);
        }
        if (!positions) {
            QueryLeaves leaves;
            parsed.query->add_leaves(leaves);
            if (!leaves.phrases.empty()) {
                throw QueryError(
                    "phrase queries need an index built with IndexConfig{.positions = true}, and words joined by "
                    "a hyphen or similar punctuation count as phrases",
                    0);
            }
        }
        return std::move(parsed.query);
    }

    std::optional<std::string_view> stored_text(const std::string& key, const std::string& field) const {
        const std::optional<int> id = indexer.find_id(key);
        if (!id) {
            return std::nullopt;
        }
        return indexer.stored_value(*id, field);
    }
};

struct Prepared::Impl {
    PreparedDocument document;
};

namespace {

int clamp_to_int(std::size_t value) {
    return static_cast<int>(std::min<std::size_t>(value, INT_MAX));
}

std::filesystem::path path_from_utf8(const std::string& text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

HighlightOptions highlight_options(const SnippetOptions& options) {
    return {options.length, options.start_marker, options.end_marker, options.ellipsis};
}

const std::string body_field = "body";

class FileSink : public ByteSink {
private:
    std::ofstream out;
    const std::string& path;

    void check() {
        if (!out) {
            throw IoError("LuxCore: failed to write " + path);
        }
    }

public:
    FileSink(const std::filesystem::path& file, const std::string& path)
        : out(file, std::ios::binary | std::ios::trunc), path(path) {
        if (!out) {
            throw IoError("LuxCore: cannot open " + path + " for writing");
        }
    }

    void write(std::string_view bytes) override {
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        check();
    }

    void overwrite(std::uint64_t offset, std::string_view bytes) override {
        out.seekp(static_cast<std::streamoff>(offset));
        write(bytes);
        out.seekp(0, std::ios::end);
        check();
    }

    void close() {
        out.close();
        check();
    }

    void close_quietly() {
        out.close();
    }
};

}

Index::Index() : Index(IndexConfig{}) {}

Index::Index(IndexConfig config) : impl(std::make_unique<Impl>(config)) {}

Index::~Index() = default;

Index::Index(Index&&) noexcept = default;

Index& Index::operator=(Index&&) noexcept = default;

Prepared::Prepared(std::unique_ptr<Impl> impl) : impl(std::move(impl)) {}

Prepared::~Prepared() = default;

Prepared::Prepared(Prepared&&) noexcept = default;

Prepared& Prepared::operator=(Prepared&&) noexcept = default;

void Index::add(const std::string& key, const std::string& title, const std::string& body) {
    commit(key, prepare(title, body));
}

// No lock: the analyzer and the positions setting never change after construction.
Prepared Index::prepare(std::string_view title, std::string_view body, PrepareOptions options) const {
    const FieldText fields[] = {
        {"title", title, FieldOptions::text()},
        {body_field, body, {.stored = options.store_body, .indexed = true, .analyzed = true}},
    };
    return Prepared(std::make_unique<Prepared::Impl>(impl->indexer.prepare(fields)));
}

void Index::commit(const std::string& key, Prepared prepared) {
    if (!prepared.impl) {
        throw std::invalid_argument("Index::commit: the prepared document was already committed");
    }
    std::unique_lock lock(impl->mutex, std::defer_lock);
    {
        auto timed = index_clocks().lock_wait.time();
        lock.lock();
    }
    impl->indexer.add_prepared(key, prepared.impl->document);
}

std::string Index::debug_stats() const {
    std::shared_lock lock(impl->mutex);
    std::string json = "{\"rows\":[";
    bool first = true;
    for (const MemoryRow& row : impl->indexer.memory_stats()) {
        json += first ? "" : ",";
        first = false;
        json += "{\"name\":\"" + row.name + "\",\"items\":" + std::to_string(row.items) +
                ",\"used\":" + std::to_string(row.used) + ",\"reserved\":" + std::to_string(row.reserved) +
                ",\"allocations\":" + std::to_string(row.allocations) + ",\"heap\":" + std::to_string(row.heap) +
                "}";
    }
    json += "],\"clocks\":{";
    const IndexClocks& clocks = index_clocks();
    const std::pair<const char*, const PhaseClock*> named[] = {
        {"lock_wait", &clocks.lock_wait},
        {"analyze", &clocks.analyze},
        {"store", &clocks.store},
        {"invert", &clocks.invert},
        {"invert_remove", &clocks.invert_remove},
        {"invert_terms", &clocks.invert_terms},
        {"invert_doc_lists", &clocks.invert_doc_lists},
        {"keys", &clocks.keys},
        {"serialize", &clocks.serialize},
        {"write", &clocks.write},
        {"read", &clocks.read},
        {"deserialize", &clocks.deserialize},
    };
    first = true;
    for (const auto& [name, clock] : named) {
        json += first ? "" : ",";
        first = false;
        json += "\"" + std::string(name) + "\":{\"calls\":" + std::to_string(clock->calls) +
                ",\"nanos\":" + std::to_string(clock->nanos) + "}";
    }
    return json + "}}";
}

bool Index::remove(const std::string& key) {
    std::unique_lock lock(impl->mutex);
    return impl->indexer.remove_document(key);
}

std::size_t Index::size() const {
    std::shared_lock lock(impl->mutex);
    return static_cast<std::size_t>(impl->indexer.get_index().get_total_documents());
}

std::optional<std::string> Index::stored(const std::string& key, const std::string& field) const {
    std::shared_lock lock(impl->mutex);
    const std::optional<std::string_view> found = impl->stored_text(key, field);
    if (!found) {
        return std::nullopt;
    }
    return std::string(*found);
}

std::vector<Hit> Index::search(const std::string& query, std::size_t limit) const {
    std::shared_lock lock(impl->mutex);
    std::vector<Hit> hits;
    for (const SearchHit& hit : impl->searcher().search(query, clamp_to_int(limit))) {
        hits.push_back({hit.key, hit.score});
    }
    return hits;
}

std::vector<std::string> Index::suggest(const std::string& query, std::size_t limit) const {
    std::shared_lock lock(impl->mutex);
    std::vector<std::string> terms;
    for (const Suggestion& suggestion : impl->searcher().suggest(query, limit)) {
        terms.push_back(suggestion.term);
    }
    return terms;
}

std::vector<Hit> Index::search_query(const std::string& text, std::size_t limit) const {
    std::shared_lock lock(impl->mutex);
    const std::unique_ptr<Query> query = impl->parse(text);
    std::vector<Hit> hits;
    for (const SearchHit& hit : impl->searcher().search(*query, clamp_to_int(limit))) {
        hits.push_back({hit.key, hit.score});
    }
    return hits;
}

std::optional<std::string> Index::snippet(const std::string& key, const std::string& field, const std::string& text,
                                          const SnippetOptions& options) const {
    std::shared_lock lock(impl->mutex);
    const std::optional<std::string_view> stored = impl->stored_text(key, field);
    if (!stored) {
        return std::nullopt;
    }
    const TextPlan plan = impl->searcher().plan(text);
    return Highlighter(highlight_options(options)).snippet(*stored, plan);
}

std::string Index::snippet_text(std::string_view body, const std::string& text, const SnippetOptions& options) const {
    std::shared_lock lock(impl->mutex);
    const TextPlan plan = impl->searcher().plan(text);
    return Highlighter(highlight_options(options)).snippet(body, plan);
}

std::string Index::snippet_text_query(std::string_view body, const std::string& text,
                                      const SnippetOptions& options) const {
    std::shared_lock lock(impl->mutex);
    const std::unique_ptr<Query> query = impl->parse(text);
    const MatchPlan plan = make_match_plan(impl->indexer.get_analyzer(), *query, body_field);
    return Highlighter(highlight_options(options)).snippet(body, plan);
}

std::optional<std::string> Index::snippet_query(const std::string& key, const std::string& field,
                                                const std::string& text, const SnippetOptions& options) const {
    std::shared_lock lock(impl->mutex);
    const std::unique_ptr<Query> query = impl->parse(text);
    const std::optional<std::string_view> stored = impl->stored_text(key, field);
    if (!stored) {
        return std::nullopt;
    }
    const MatchPlan plan = make_match_plan(impl->indexer.get_analyzer(), *query, field);
    return Highlighter(highlight_options(options)).snippet(*stored, plan);
}

void Index::save(const std::string& path) const {
    const std::filesystem::path file = path_from_utf8(path);
    FileSink sink(file, path);
    try {
        {
            std::shared_lock lock(impl->mutex);
            auto timed = index_clocks().serialize.time();
            impl->indexer.serialize(sink);
        }
        auto timed = index_clocks().write.time();
        sink.close();
    } catch (...) {
        sink.close_quietly();
        std::error_code ignored;
        std::filesystem::remove(file, ignored);
        throw;
    }
}

Index Index::load(const std::string& path, IndexConfig config) {
    const std::filesystem::path file = path_from_utf8(path);
    std::error_code error;
    if (!std::filesystem::is_regular_file(file, error)) {
        throw IoError("LuxCore: " + path + " is not a readable file");
    }
    std::optional<MappedFile> mapped;
    {
        auto timed = index_clocks().read.time();
        try {
            mapped.emplace(file);
        } catch (const std::runtime_error& failure) {
            throw IoError("LuxCore: " + std::string(failure.what()) + ": " + path);
        }
    }

    Index index(config);
    auto timed = index_clocks().deserialize.time();
    try {
        index.impl->indexer =
            Indexer::deserialize(mapped->bytes(), std::string(), IndexOptions{.positions = config.positions});
    } catch (const std::runtime_error& failure) {
        throw FormatError(failure.what());
    }
    return index;
}

}
