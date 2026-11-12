#include "TestFramework.h"

#include "CombinedPostings.h"
#include "Indexer.h"
#include "InvertedIndex.h"

#include <random>
#include <string>

namespace {

std::size_t df(const InvertedIndex& index, const std::string& term) {
    return index.get_document_frequency(term);
}

std::string random_words(std::mt19937& rng, int count) {
    static const char* vocabulary[] = {"alpha", "beta", "gamma", "delta", "epsilon", "zeta", "eta", "theta",
                                       "iota", "kappa", "lambda", "mu", "nu", "xi", "omicron", "pi"};
    std::string text;
    for (int i = 0; i < count; i++) {
        text += std::string(vocabulary[rng() % 16]) + " ";
    }
    return text;
}

void check_frequencies_match_the_field_lists(const InvertedIndex& index) {
    std::size_t terms = 0;
    for (const TermPostings& match : index.terms_with_prefix("")) {
        terms++;
        const std::size_t expected = combined_postings(index, std::string(match.term)).size();
        CHECK(expected > 0);
        CHECK_EQ(match.document_frequency, expected);
        CHECK_EQ(index.get_document_frequency(std::string(match.term)), expected);
    }
    CHECK_EQ(index.get_unique_terms_count(), terms);
}

}

TEST(document_frequency_counts_a_document_once_across_its_fields) {
    InvertedIndex index;
    index.add_document(1, FieldTermFreqs{{"title", {{"a", 1}}}, {"body", {{"a", 2}, {"b", 1}}}, {"tag", {{"a", 1}}}});
    index.add_document(2, FieldTermFreqs{{"body", {{"a", 1}}}});
    index.add_document(3, FieldTermFreqs{{"tag", {{"a", 1}, {"c", 1}}}});

    CHECK_EQ(df(index, "a"), static_cast<std::size_t>(3));
    CHECK_EQ(df(index, "b"), static_cast<std::size_t>(1));
    CHECK_EQ(df(index, "c"), static_cast<std::size_t>(1));
    CHECK_EQ(df(index, "missing"), static_cast<std::size_t>(0));
    CHECK_EQ(index.get_unique_terms_count(), static_cast<std::size_t>(3));

    index.add_document(1, FieldTermFreqs{{"title", {{"a", 1}}}, {"body", {{"a", 5}}}});
    CHECK_EQ(df(index, "a"), static_cast<std::size_t>(3));
    CHECK_EQ(df(index, "b"), static_cast<std::size_t>(0));
    CHECK(index.terms_with_prefix("b").empty());
    CHECK(index.fuzzy_terms("b", 0).empty());

    index.remove_document(2);
    CHECK_EQ(df(index, "a"), static_cast<std::size_t>(2));
    index.remove_document(1);
    CHECK_EQ(df(index, "a"), static_cast<std::size_t>(1));
    index.remove_document(3);
    CHECK_EQ(df(index, "a"), static_cast<std::size_t>(0));
    CHECK_EQ(index.get_unique_terms_count(), static_cast<std::size_t>(0));
    CHECK(index.terms_with_prefix("").empty());
    CHECK(index.fuzzy_terms("a", 1).empty());
}

TEST(prefix_listing_reports_the_document_frequency_of_each_term) {
    InvertedIndex index;
    index.add_document(1, FieldTermFreqs{{"title", {{"sea", 1}}}, {"body", {{"sea", 1}, {"seal", 1}}}});
    index.add_document(2, FieldTermFreqs{{"body", {{"sea", 1}}}});

    std::string text;
    for (const TermPostings& match : index.terms_with_prefix("sea")) {
        text += std::string(match.term) + "=" + std::to_string(match.document_frequency) + ",";
    }
    CHECK_EQ(text, std::string("sea=2,seal=1,"));

    text.clear();
    for (const TermPostings& match : index.terms_with_prefix("title", "sea")) {
        text += std::string(match.term) + "=" + std::to_string(match.document_frequency) + ",";
    }
    CHECK_EQ(text, std::string("sea=1,"));
}

TEST(document_frequency_survives_churn_and_a_save_and_load) {
    std::mt19937 rng(21);
    Indexer indexer;
    for (int i = 0; i < 300; i++) {
        Document document;
        document.add({"title", random_words(rng, 2)});
        document.add({"body", random_words(rng, 6)});
        document.add({"note", random_words(rng, 3)});
        indexer.add_document("doc-" + std::to_string(i), document);
    }
    for (int i = 0; i < 300; i += 3) {
        indexer.remove_document("doc-" + std::to_string(i));
    }
    for (int i = 1; i < 300; i += 7) {
        Document document;
        document.add({"title", random_words(rng, 1)});
        document.add({"body", random_words(rng, 4)});
        indexer.add_document("doc-" + std::to_string(i), document);
    }

    check_frequencies_match_the_field_lists(indexer.get_index());

    const Indexer reloaded = Indexer::deserialize(indexer.serialize());
    check_frequencies_match_the_field_lists(reloaded.get_index());
    CHECK_EQ(reloaded.get_index().get_unique_terms_count(), indexer.get_index().get_unique_terms_count());
    for (const TermPostings& match : indexer.get_index().terms_with_prefix("")) {
        CHECK_EQ(reloaded.get_index().get_document_frequency(std::string(match.term)), match.document_frequency);
    }
    CHECK_EQ(reloaded.get_index().fuzzy_terms("alfa", 2).size(), indexer.get_index().fuzzy_terms("alfa", 2).size());
}

TEST(a_long_posting_list_wastes_at_most_a_quarter_of_its_capacity) {
    InvertedIndex index;
    const int documents = 6000;
    for (int id = 0; id < documents; id++) {
        index.add_document(id, FieldTermFreqs{{"body", {{"common", 1}}}});
    }

    CHECK_EQ(df(index, "common"), static_cast<std::size_t>(documents));
    // A doc id takes four bytes and a frequency below 256 takes one.
    const std::size_t packed = static_cast<std::size_t>(documents) * 5;
    const std::size_t bytes = index.memory_usage().posting_bytes;
    CHECK(bytes >= packed);
    CHECK(bytes <= packed * 5 / 4 + 64);

    const std::optional<PostingList> list = index.get_posting_list("body", "common");
    for (int id = 0; id < documents; id++) {
        CHECK_EQ(list->postings[static_cast<std::size_t>(id)].doc_id, id);
    }
}

TEST(posting_lists_stay_sorted_when_documents_arrive_out_of_order) {
    InvertedIndex index;
    for (const int id : {40, 7, 23, 3, 99, 15, 61}) {
        index.add_document(id, FieldTermFreqs{{"body", {{"t", id}}}});
    }

    const std::optional<PostingList> list = index.get_posting_list("body", "t");
    std::string ids;
    for (const Posting posting : list->postings) {
        ids += std::to_string(posting.doc_id) + ":" + std::to_string(posting.term_frequency) + ",";
    }
    CHECK_EQ(ids, std::string("3:3,7:7,15:15,23:23,40:40,61:61,99:99,"));
    CHECK_EQ(df(index, "t"), static_cast<std::size_t>(7));
}
