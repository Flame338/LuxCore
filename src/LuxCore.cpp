#include "LuxCore.h"

#include "EditDistance.h"
#include "FairSharedMutex.h"
#include "Highlighter.h"
#include "Indexer.h"
#include "MatchPlan.h"
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

    std::optional<std::string> stored_text(const std::string& key, const std::string& field) const {
        const std::optional<int> id = indexer.find_id(key);
        if (!id) {
            return std::nullopt;
        }
        const std::optional<std::string_view> found = indexer.stored_value(*id, field);
        if (!found) {
            return std::nullopt;
        }
        return std::string(*found);
    }
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

}

Index::Index() : Index(IndexConfig{}) {}

Index::Index(IndexConfig config) : impl(std::make_unique<Impl>(config)) {}

Index::~Index() = default;

Index::Index(Index&&) noexcept = default;

Index& Index::operator=(Index&&) noexcept = default;

void Index::add(const std::string& key, const std::string& title, const std::string& body) {
    const Document document = make_text_document(title, body);
    std::unique_lock lock(impl->mutex);
    impl->indexer.add_document(key, document);
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
    return impl->stored_text(key, field);
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
    const std::optional<std::string> stored = impl->stored_text(key, field);
    if (!stored) {
        return std::nullopt;
    }
    const TextPlan plan = impl->searcher().plan(text);
    return Highlighter(highlight_options(options)).snippet(*stored, plan);
}

std::optional<std::string> Index::snippet_query(const std::string& key, const std::string& field,
                                                const std::string& text, const SnippetOptions& options) const {
    std::shared_lock lock(impl->mutex);
    const std::unique_ptr<Query> query = impl->parse(text);
    const std::optional<std::string> stored = impl->stored_text(key, field);
    if (!stored) {
        return std::nullopt;
    }
    const MatchPlan plan = make_match_plan(impl->indexer.get_analyzer(), *query, field);
    return Highlighter(highlight_options(options)).snippet(*stored, plan);
}

void Index::save(const std::string& path) const {
    std::string bytes;
    {
        std::shared_lock lock(impl->mutex);
        bytes = impl->indexer.serialize();
    }
    std::ofstream out(path_from_utf8(path), std::ios::binary | std::ios::trunc);
    if (!out) {
        throw IoError("LuxCore: cannot open " + path + " for writing");
    }
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out) {
        throw IoError("LuxCore: failed to write " + path);
    }
}

Index Index::load(const std::string& path, IndexConfig config) {
    const std::filesystem::path file = path_from_utf8(path);
    std::error_code error;
    if (!std::filesystem::is_regular_file(file, error)) {
        throw IoError("LuxCore: " + path + " is not a readable file");
    }
    const std::uintmax_t size = std::filesystem::file_size(file, error);
    if (error) {
        throw IoError("LuxCore: cannot read the size of " + path);
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        throw IoError("LuxCore: cannot open " + path + " for reading");
    }
    std::string bytes(static_cast<std::size_t>(size), '\0');
    in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (static_cast<std::size_t>(in.gcount()) != bytes.size()) {
        throw IoError("LuxCore: failed to read " + path);
    }

    Index index(config);
    try {
        index.impl->indexer = Indexer::deserialize(bytes, std::string(), IndexOptions{.positions = config.positions});
    } catch (const std::runtime_error& error) {
        throw FormatError(error.what());
    }
    return index;
}

}
