#include "TestFramework.h"

#include "CombinedPostings.h"
#include "Indexer.h"
#include "Searcher.h"

#include <stdexcept>

namespace {

std::string keys_of(const Indexer& indexer, const std::string& query) {
    const Searcher searcher(indexer);
    std::string text;
    for (const SearchHit& hit : searcher.search(query)) {
        text += "[" + hit.key + "]";
    }
    return text;
}

std::string postings_text(const Indexer& indexer, const std::string& term) {
    const std::vector<Posting> postings = combined_postings(indexer.get_index(), term);
    if (postings.empty()) {
        return "-";
    }
    std::string text;
    for (const Posting& posting : postings) {
        text += "(" + std::to_string(posting.doc_id) + ":" + std::to_string(posting.term_frequency) + ")";
    }
    return text;
}

}

TEST(keyed_add_maps_the_key_to_an_internal_id_and_hits_return_the_key) {
    Indexer indexer("stopwords.txt");
    const int first = indexer.add_document("file:a.txt", make_text_document("Rocket", "launch pad"));
    const int second = indexer.add_document("file:b.txt", make_text_document("Garden", "rocket notes"));

    CHECK_EQ(first, 0);
    CHECK_EQ(second, 1);
    CHECK_EQ(indexer.get_key(second), "file:b.txt");
    CHECK(indexer.find_id("file:a.txt") == std::optional<int>(first));
    CHECK(!indexer.find_id("file:c.txt").has_value());
    CHECK_EQ(keys_of(indexer, "rocket"), "[file:a.txt][file:b.txt]");
    CHECK_EQ(keys_of(indexer, "garden"), "[file:b.txt]");
}

TEST(keyed_add_rejects_an_empty_key) {
    Indexer indexer("stopwords.txt");

    bool rejected = false;
    try {
        indexer.add_document("", make_text_document("t", "b"));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected);
    CHECK_EQ(indexer.get_index().get_total_documents(), 0);
}

TEST(adding_under_an_existing_key_updates_in_place) {
    Indexer indexer("stopwords.txt");
    const int id = indexer.add_document("k", make_text_document("Rocket", "launch"));
    const int again = indexer.add_document("k", make_text_document("Garden", "notes"));

    CHECK_EQ(again, id);
    CHECK_EQ(indexer.get_index().get_total_documents(), 1);
    CHECK_EQ(indexer.get_document(id).find("title")->value, "Garden");
    CHECK_EQ(keys_of(indexer, "garden"), "[k]");
    CHECK_EQ(keys_of(indexer, "rocket"), "");
    CHECK_EQ(keys_of(indexer, "launch"), "");
}

TEST(update_leaves_no_stale_postings) {
    Indexer indexer("stopwords.txt");
    indexer.add_document("a", make_text_document("Cat", "dog dog"));
    indexer.add_document("b", make_text_document("Dog", "bird"));
    const std::size_t terms_before = indexer.get_index().get_unique_terms_count();

    CHECK_EQ(postings_text(indexer, "dog"), "(0:2)(1:1)");
    CHECK(indexer.update_document(0, make_text_document("Fish", "dog")));

    CHECK_EQ(postings_text(indexer, "cat"), "-");
    CHECK_EQ(postings_text(indexer, "dog"), "(0:1)(1:1)");
    CHECK_EQ(postings_text(indexer, "fish"), "(0:1)");
    CHECK(!indexer.get_index().get_postings("title", "cat"));
    CHECK_EQ(indexer.get_index().get_total_documents(), 2);
    CHECK_EQ(indexer.get_index().get_unique_terms_count(), terms_before);
    CHECK_EQ(indexer.get_index().get_field_documents("title"), 2);
}

TEST(update_of_an_unknown_id_changes_nothing) {
    Indexer indexer("stopwords.txt");
    indexer.add_document("a", make_text_document("Cat", ""));

    CHECK(!indexer.update_document(5, make_text_document("Dog", "")));
    CHECK(!indexer.update_document(-1, make_text_document("Dog", "")));
    CHECK_EQ(postings_text(indexer, "dog"), "-");
    CHECK_EQ(indexer.get_index().get_total_documents(), 1);
}

TEST(remove_by_key_clears_the_index_the_store_and_the_key) {
    Indexer indexer("stopwords.txt");
    const int id = indexer.add_document("a", make_text_document("Cat", "dog"));
    indexer.add_document("b", make_text_document("Dog", ""));

    CHECK(indexer.remove_document("a"));

    CHECK(!indexer.contains(id));
    CHECK(!indexer.find_id("a").has_value());
    CHECK_EQ(indexer.get_key(id), "");
    CHECK_EQ(postings_text(indexer, "cat"), "-");
    CHECK_EQ(postings_text(indexer, "dog"), "(1:1)");
    CHECK_EQ(indexer.get_index().get_total_documents(), 1);

    bool rejected = false;
    try {
        indexer.get_document(id);
    } catch (const std::out_of_range&) {
        rejected = true;
    }
    CHECK(rejected);
}

TEST(remove_by_internal_id_works_for_documents_without_a_key) {
    Indexer indexer("stopwords.txt");
    const int anonymous = indexer.add_document(make_text_document("Cat", ""));
    indexer.add_document("b", make_text_document("Cat", ""));

    CHECK(indexer.remove_document(anonymous));
    CHECK_EQ(keys_of(indexer, "cat"), "[b]");
    CHECK_EQ(indexer.get_index().get_total_documents(), 1);
}

TEST(removing_an_unknown_or_already_removed_document_returns_false) {
    Indexer indexer("stopwords.txt");
    const int id = indexer.add_document("a", make_text_document("Cat", ""));

    CHECK(!indexer.remove_document("missing"));
    CHECK(!indexer.remove_document(99));
    CHECK(!indexer.remove_document(-1));
    CHECK(indexer.remove_document(id));
    CHECK(!indexer.remove_document(id));
    CHECK(!indexer.remove_document("a"));
    CHECK_EQ(indexer.get_index().get_total_documents(), 0);
}

TEST(a_removed_key_can_be_added_again_under_a_fresh_internal_id) {
    Indexer indexer("stopwords.txt");
    const int first = indexer.add_document("a", make_text_document("Cat", ""));
    indexer.remove_document("a");
    const int second = indexer.add_document("a", make_text_document("Dog", ""));

    CHECK(second != first);
    CHECK_EQ(indexer.get_key(second), "a");
    CHECK_EQ(indexer.get_key(first), "");
    CHECK_EQ(keys_of(indexer, "dog"), "[a]");
    CHECK_EQ(keys_of(indexer, "cat"), "");
    CHECK_EQ(indexer.get_index().get_total_documents(), 1);
}

TEST(search_never_returns_a_removed_document) {
    Indexer indexer("stopwords.txt");
    for (int n = 0; n < 6; n++) {
        indexer.add_document("doc" + std::to_string(n), make_text_document("Rocket " + std::to_string(n), "searching launch"));
    }
    indexer.remove_document("doc1");
    indexer.remove_document("doc4");

    const Searcher searcher(indexer);
    for (const char* query : {"rocket", "searching", "search", "launch", "rocket launch", "roc"}) {
        for (const SearchHit& hit : searcher.search(query, 100)) {
            CHECK(hit.key != "doc1" && hit.key != "doc4");
            CHECK(indexer.contains(hit.doc_id));
        }
    }
    CHECK_EQ(searcher.search("rocket", 100).size(), static_cast<std::size_t>(4));
    CHECK_EQ(searcher.suggest("sear").size(), static_cast<std::size_t>(1));
    CHECK_EQ(searcher.suggest("sear").front().doc_freq, static_cast<std::size_t>(4));
}
