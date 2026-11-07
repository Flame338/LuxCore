#include "LuxCore.h"

#include "EditDistance.h"
#include "Indexer.h"
#include "Scoring.h"
#include "Searcher.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace luxcore {

struct Index::Impl {
    Indexer indexer;

    Searcher searcher() const {
        return Searcher(indexer, {.scoring = Scoring::bm25_with(), .fuzziness = Fuzziness::On});
    }
};

namespace {

int clamp_to_int(std::size_t value) {
    return static_cast<int>(std::min<std::size_t>(value, INT_MAX));
}

}

Index::Index() : impl(std::make_unique<Impl>()) {}

Index::~Index() = default;

Index::Index(Index&&) noexcept = default;

Index& Index::operator=(Index&&) noexcept = default;

void Index::add(const std::string& key, const std::string& title, const std::string& body) {
    impl->indexer.add_document(key, make_text_document(title, body));
}

bool Index::remove(const std::string& key) {
    return impl->indexer.remove_document(key);
}

std::size_t Index::size() const {
    return static_cast<std::size_t>(impl->indexer.get_index().get_total_documents());
}

std::optional<std::string> Index::stored(const std::string& key, const std::string& field) const {
    const std::optional<int> id = impl->indexer.find_id(key);
    if (!id) {
        return std::nullopt;
    }
    const Field* found = impl->indexer.get_document(*id).find(field);
    if (found == nullptr) {
        return std::nullopt;
    }
    return found->value;
}

std::vector<Hit> Index::search(const std::string& query, std::size_t limit) const {
    std::vector<Hit> hits;
    for (const SearchHit& hit : impl->searcher().search(query, clamp_to_int(limit))) {
        hits.push_back({hit.key, hit.score});
    }
    return hits;
}

std::vector<std::string> Index::suggest(const std::string& query, std::size_t limit) const {
    std::vector<std::string> terms;
    for (const Suggestion& suggestion : impl->searcher().suggest(query, limit)) {
        terms.push_back(suggestion.term);
    }
    return terms;
}

void Index::save(const std::string& path) const {
    const std::string bytes = impl->indexer.serialize();
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("LuxCore: cannot open " + path + " for writing");
    }
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out) {
        throw std::runtime_error("LuxCore: failed to write " + path);
    }
}

Index Index::load(const std::string& path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        throw std::runtime_error("LuxCore: " + path + " is not a readable file");
    }
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error) {
        throw std::runtime_error("LuxCore: cannot read the size of " + path);
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("LuxCore: cannot open " + path + " for reading");
    }
    std::string bytes(static_cast<std::size_t>(size), '\0');
    in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (static_cast<std::size_t>(in.gcount()) != bytes.size()) {
        throw std::runtime_error("LuxCore: failed to read " + path);
    }

    Index index;
    index.impl->indexer = Indexer::deserialize(bytes);
    return index;
}

}
