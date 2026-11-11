#include "TestFramework.h"

#include "Indexer.h"

#include <string>
#include <vector>

namespace {

std::string positions_text(const InvertedIndex& index, std::string_view field, const std::string& term) {
    const std::optional<PostingList> list = index.get_posting_list(field, term);
    if (!list) {
        return "-";
    }

    std::string text;
    for (std::size_t i = 0; i < list->postings.size(); i++) {
        text += "(" + std::to_string(list->postings[i].doc_id) + ":";
        for (const std::uint32_t position : list->positions_of(i)) {
            text += std::to_string(position) + ",";
        }
        text += ")";
    }
    return text;
}

FieldTermPositions body(const TermPositions& terms) {
    return FieldTermPositions{{"body", terms}};
}

}

TEST(indexer_stores_positions_with_gaps_for_removed_stop_words) {
    Indexer indexer("stopwords.txt");
    indexer.add_document(make_text_document("", "the cat and the hat"));
    indexer.add_document(make_text_document("", "cat of hat cat"));

    CHECK_EQ(positions_text(indexer.get_index(), "body", "cat"), "(0:1,)(1:0,3,)");
    CHECK_EQ(positions_text(indexer.get_index(), "body", "hat"), "(0:4,)(1:2,)");
    CHECK_EQ(positions_text(indexer.get_index(), "body", "the"), "-");
}

TEST(positions_are_kept_per_field) {
    Indexer indexer("stopwords.txt");
    indexer.add_document(make_text_document("red fox", "fox red fox"));

    CHECK_EQ(positions_text(indexer.get_index(), "title", "fox"), "(0:1,)");
    CHECK_EQ(positions_text(indexer.get_index(), "body", "fox"), "(0:0,2,)");
    CHECK_EQ(positions_text(indexer.get_index(), "title", "red"), "(0:0,)");
}

TEST(a_repeated_field_name_leaves_a_gap_between_its_values) {
    Indexer indexer;
    Document document;
    document.add({"body", "red fox"});
    document.add({"body", "fox den"});
    indexer.add_document(document);

    CHECK_EQ(positions_text(indexer.get_index(), "body", "red"), "(0:0,)");
    CHECK_EQ(positions_text(indexer.get_index(), "body", "fox"), "(0:1,3,)");
    CHECK_EQ(positions_text(indexer.get_index(), "body", "den"), "(0:4,)");
    CHECK_EQ(indexer.get_index().get_field_length("body", 0), 4);
}

TEST(a_keyword_field_has_a_single_position) {
    Indexer indexer;
    Document document;
    document.add({"id", "AB-12", FieldOptions::keyword()});
    indexer.add_document(document);

    CHECK_EQ(positions_text(indexer.get_index(), "id", "AB-12"), "(0:0,)");
}

TEST(removing_a_document_drops_its_positions_and_keeps_the_others_intact) {
    InvertedIndex index;
    index.add_document(1, body({{"fox", {0, 4}}, {"den", {1}}}));
    index.add_document(2, body({{"fox", {2}}}));
    index.add_document(3, body({{"fox", {1, 5, 9}}}));

    index.remove_document(1);
    CHECK_EQ(positions_text(index, "body", "fox"), "(2:2,)(3:1,5,9,)");
    CHECK_EQ(positions_text(index, "body", "den"), "-");

    index.remove_document(3);
    CHECK_EQ(positions_text(index, "body", "fox"), "(2:2,)");

    index.remove_document(2);
    CHECK_EQ(positions_text(index, "body", "fox"), "-");
    CHECK_EQ(index.memory_usage().position_bytes, static_cast<std::size_t>(0));
}

TEST(replacing_a_document_swaps_its_positions) {
    InvertedIndex index;
    index.add_document(1, body({{"fox", {0, 3}}}));
    index.add_document(2, body({{"fox", {7}}}));
    index.add_document(1, body({{"fox", {5}}, {"den", {6}}}));

    CHECK_EQ(positions_text(index, "body", "fox"), "(1:5,)(2:7,)");
    CHECK_EQ(positions_text(index, "body", "den"), "(1:6,)");
}

TEST(positions_are_stored_in_ascending_order) {
    InvertedIndex index;
    index.add_document(1, body({{"fox", {9, 2, 5}}}));

    CHECK_EQ(positions_text(index, "body", "fox"), "(1:2,5,9,)");
}

TEST(a_document_added_without_positions_has_term_frequencies_but_no_positions) {
    InvertedIndex index;
    index.add_document(1, FieldTermFreqs{{"body", {{"fox", 2}}}});

    const std::optional<PostingList> list = index.get_posting_list("body", "fox");
    CHECK(list.has_value());
    CHECK_EQ(list->postings[0].term_frequency, 2);
    CHECK(list->positions_of(0).empty());
}

TEST(prefix_fuzzy_and_global_lookups_still_work_with_positions) {
    Indexer indexer;
    indexer.add_document(make_text_document("Searching", "search engines seal"));
    indexer.add_document(make_text_document("", "sea level"));
    indexer.remove_document(1);

    const InvertedIndex& index = indexer.get_index();
    std::string prefixed;
    for (const TermPostings& match : index.terms_with_prefix("sea")) {
        prefixed += std::string(match.term) + ",";
    }
    CHECK_EQ(prefixed, "seal,search,searching,");

    std::string scoped;
    for (const TermPostings& match : index.terms_with_prefix("title", "sea")) {
        scoped += std::string(match.term) + ",";
    }
    CHECK_EQ(scoped, "searching,");

    bool found = false;
    for (const FuzzyMatch& match : index.fuzzy_terms("serch", 1)) {
        found = found || match.term == "search";
    }
    CHECK(found);
    CHECK_EQ(index.get_document_frequency("level"), static_cast<std::size_t>(0));
    CHECK_EQ(index.get_document_frequency("search"), static_cast<std::size_t>(1));
}

TEST(memory_usage_counts_positions_separately_from_postings) {
    InvertedIndex positional;
    positional.add_document(1, body({{"fox", {0, 1, 2}}}));
    CHECK(positional.memory_usage().position_bytes >= 4 * sizeof(std::uint32_t));
    CHECK(positional.memory_usage().posting_bytes >= sizeof(Posting));
}
