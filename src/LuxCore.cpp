#include "LuxCore.h"

#include "EditDistance.h"
#include "Indexer.h"
#include "Scoring.h"
#include "Searcher.h"

#include <algorithm>
#include <climits>

namespace luxcore {

struct Index::Impl {
    Indexer indexer;

    Searcher searcher() const {
        return Searcher(indexer.get_index(), indexer.get_analyzer(), FieldBoosts::standard(), &indexer.get_keys(),
                        Scoring::bm25_with(), Fuzziness::On);
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

}
