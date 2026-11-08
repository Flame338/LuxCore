#include "TestFramework.h"

#include "Indexer.h"
#include "Searcher.h"

#include <string>
#include <utility>

namespace {

std::string candidates_of(const Indexer& indexer, const std::string& query) {
    const Searcher searcher(indexer, {.fuzziness = Fuzziness::On});
    const TextPlan plan = searcher.plan(query);
    std::string text;
    for (const TextPlan::Word& word : plan.words()) {
        text += word.term + "[";
        for (const FuzzyMatch& match : word.fuzzy) {
            text += match.term + ":" + std::to_string(match.distance) + ",";
        }
        text += "]";
    }
    return text;
}

}

TEST(repeated_fuzzy_lookups_return_the_same_candidates) {
    Indexer indexer;
    indexer.add_document("a", make_text_document("Search engine", "searching seal sea"));

    const std::string cold = candidates_of(indexer, "serch ");
    const std::string warm = candidates_of(indexer, "serch ");
    CHECK_EQ(cold, warm);
    CHECK(cold.find("search:1") != std::string::npos);
}

TEST(a_changed_index_does_not_serve_stale_fuzzy_candidates) {
    Indexer indexer;
    indexer.add_document("a", make_text_document("Search engine", "plain body"));
    const std::string before = candidates_of(indexer, "serch ");
    CHECK(before.find("serach") == std::string::npos);

    indexer.add_document("b", make_text_document("", "serach typo"));
    const std::string added = candidates_of(indexer, "serch ");
    CHECK(added.find("serach:1") != std::string::npos);

    indexer.remove_document("b");
    CHECK_EQ(candidates_of(indexer, "serch "), before);

    indexer.add_document("a", make_text_document("Other", "serch"));
    CHECK(candidates_of(indexer, "serch ").find("search") == std::string::npos);
}

TEST(two_indexes_with_the_same_query_keep_their_own_candidates) {
    Indexer first;
    Indexer second;
    first.add_document("a", make_text_document("", "search"));
    second.add_document("a", make_text_document("", "serich"));

    for (int round = 0; round < 4; round++) {
        CHECK_EQ(candidates_of(first, "serch "), std::string("serch[search:1,]"));
        CHECK_EQ(candidates_of(second, "serch "), std::string("serch[serich:1,]"));
    }
}

TEST(a_moved_index_keeps_working_and_the_source_does_not_share_its_cache) {
    Indexer source;
    source.add_document("a", make_text_document("", "search"));
    CHECK_EQ(candidates_of(source, "serch "), std::string("serch[search:1,]"));

    Indexer moved = std::move(source);
    CHECK_EQ(candidates_of(moved, "serch "), std::string("serch[search:1,]"));

    moved.add_document("b", make_text_document("", "serach"));
    CHECK(candidates_of(moved, "serch ").find("serach:1") != std::string::npos);

    Indexer replacement;
    replacement.add_document("a", make_text_document("", "serich"));
    moved = std::move(replacement);
    CHECK_EQ(candidates_of(moved, "serch "), std::string("serch[serich:1,]"));
}

TEST(a_loaded_index_does_not_reuse_candidates_of_the_index_it_was_saved_from) {
    Indexer original;
    original.add_document("a", make_text_document("", "search"));
    CHECK_EQ(candidates_of(original, "serch "), std::string("serch[search:1,]"));

    const Indexer loaded = Indexer::deserialize(original.serialize());
    CHECK_EQ(candidates_of(loaded, "serch "), std::string("serch[search:1,]"));

    original.add_document("b", make_text_document("", "serach"));
    CHECK(candidates_of(original, "serch ").find("serach:1") != std::string::npos);
    CHECK_EQ(candidates_of(loaded, "serch "), std::string("serch[search:1,]"));
}
