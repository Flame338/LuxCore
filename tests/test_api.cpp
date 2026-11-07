#include "TestFramework.h"

#include "LuxCore.h"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

std::string keys_of(const std::vector<luxcore::Hit>& hits) {
    std::string text;
    for (const luxcore::Hit& hit : hits) {
        text += hit.key + ",";
    }
    return text;
}

luxcore::Index notes() {
    luxcore::Index index;
    index.add("shopping", "Shopping list", "milk, eggs, bread and butter");
    index.add("reading", "Reading list", "a book about how Lucene ranks search results");
    index.add("trip", "Trip planning", "book the train to Edinburgh and pack a raincoat");
    return index;
}

}

TEST(api_finds_a_document_by_its_key) {
    const luxcore::Index index = notes();

    CHECK_EQ(keys_of(index.search("raincoat")), "trip,");
    CHECK_EQ(index.size(), static_cast<std::size_t>(3));
}

TEST(api_matches_the_unfinished_last_word_as_a_prefix) {
    const luxcore::Index index = notes();

    CHECK_EQ(keys_of(index.search("brea")), "shopping,");
}

TEST(api_tolerates_a_typo_in_a_finished_word) {
    const luxcore::Index index = notes();

    CHECK_EQ(keys_of(index.search("shoping lis")), "shopping,reading,");
}

TEST(api_ranks_a_title_match_above_a_body_match) {
    luxcore::Index index;
    index.add("body", "Notes", "the garden needs water");
    index.add("title", "Garden", "notes about plants");

    CHECK_EQ(keys_of(index.search("garden")), "title,body,");
}

TEST(api_limits_the_number_of_hits) {
    const luxcore::Index index = notes();

    CHECK_EQ(index.search("book", 1).size(), static_cast<std::size_t>(1));
    CHECK_EQ(index.search("book", 10).size(), static_cast<std::size_t>(2));
}

TEST(api_adding_an_existing_key_replaces_the_document) {
    luxcore::Index index = notes();
    index.add("trip", "Trip planning", "pack a umbrella");

    CHECK_EQ(index.size(), static_cast<std::size_t>(3));
    CHECK_EQ(keys_of(index.search("raincoat")), "");
    CHECK_EQ(keys_of(index.search("umbrella")), "trip,");
}

TEST(api_removing_a_key_drops_the_document) {
    luxcore::Index index = notes();

    CHECK(index.remove("trip"));
    CHECK(!index.remove("trip"));
    CHECK_EQ(index.size(), static_cast<std::size_t>(2));
    CHECK_EQ(keys_of(index.search("raincoat")), "");
}

TEST(api_returns_stored_fields_by_key) {
    const luxcore::Index index = notes();

    CHECK_EQ(index.stored("trip", "title").value_or("-"), "Trip planning");
    CHECK_EQ(index.stored("trip", "body").value_or("-"), "book the train to Edinburgh and pack a raincoat");
    CHECK(!index.stored("trip", "author").has_value());
    CHECK(!index.stored("missing", "title").has_value());
}

TEST(api_suggests_completions_of_the_last_word) {
    const luxcore::Index index = notes();

    CHECK_EQ(index.suggest("rea"), (std::vector<std::string>{"reading"}));
    CHECK(index.suggest("zzz").empty());
}

TEST(api_rejects_an_empty_key) {
    luxcore::Index index;
    bool thrown = false;
    try {
        index.add("", "title", "body");
    } catch (const std::invalid_argument&) {
        thrown = true;
    }
    CHECK(thrown);
    CHECK_EQ(index.size(), static_cast<std::size_t>(0));
}

TEST(api_an_empty_index_and_a_blank_query_return_nothing) {
    const luxcore::Index empty;
    CHECK(empty.search("anything").empty());

    const luxcore::Index index = notes();
    CHECK(index.search("").empty());
    CHECK(index.search("   ").empty());
}

TEST(api_a_moved_index_keeps_its_documents) {
    luxcore::Index original = notes();
    luxcore::Index moved = std::move(original);

    CHECK_EQ(keys_of(moved.search("raincoat")), "trip,");

    luxcore::Index assigned;
    assigned = std::move(moved);
    CHECK_EQ(assigned.size(), static_cast<std::size_t>(3));
}

TEST(api_does_not_drop_common_words_without_a_stop_word_file) {
    luxcore::Index index;
    index.add("a", "Plans", "to be or not to be");

    CHECK_EQ(keys_of(index.search("to be ")), "a,");
}
