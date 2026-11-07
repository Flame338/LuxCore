#include "TestFramework.h"

#include "Indexer.h"
#include "Searcher.h"

#include <cmath>

namespace {

std::string ids_of(const std::vector<SearchHit>& hits) {
    std::string text;
    for (const SearchHit& hit : hits) {
        text += "[" + std::to_string(hit.doc_id) + "]";
    }
    return text;
}

std::string suggestions_text(const std::vector<Suggestion>& suggestions) {
    std::string text;
    for (const Suggestion& suggestion : suggestions) {
        text += suggestion.term + ":" + std::to_string(suggestion.doc_freq) + ",";
    }
    return text;
}

std::string postings_of(const InvertedIndex& index, std::string_view field, const std::string& term) {
    const std::vector<Posting>* postings = index.get_postings(field, term);
    if (postings == nullptr) {
        return "-";
    }

    std::string text;
    for (const Posting& posting : *postings) {
        text += "(" + std::to_string(posting.doc_id) + ":" + std::to_string(posting.term_frequency) + ")";
    }
    return text;
}

struct Engine {
    Indexer indexer{"stopwords.txt"};

    void add(const std::string& title, const std::string& body) {
        indexer.add_document(make_text_document(title, body));
    }

    Searcher searcher(FieldBoosts boosts = FieldBoosts::standard()) const {
        return Searcher(indexer.get_index(), indexer.get_analyzer(), {.boosts = std::move(boosts)});
    }
};

}

TEST(scoped_search_hits_only_the_chosen_field) {
    Engine engine;
    engine.add("Rocket launch", "garden notes");
    engine.add("Garden notes", "rocket launch");
    engine.add("Plain", "rocket");

    const Searcher searcher = engine.searcher();
    CHECK_EQ(ids_of(searcher.search_in("title", "rocket")), "[0]");
    CHECK_EQ(ids_of(searcher.search_in("body", "rocket")), "[1][2]");
    CHECK_EQ(ids_of(searcher.search_in("title", "plain")), "[2]");
    CHECK(searcher.search_in("body", "plain").empty());
    CHECK(searcher.search_in("missing", "rocket").empty());
}

TEST(unscoped_search_hits_every_indexed_field) {
    Engine engine;
    engine.add("Rocket launch", "garden notes");
    engine.add("Garden notes", "rocket launch");
    engine.add("Plain", "nothing");

    CHECK_EQ(ids_of(engine.searcher().search("rocket")), "[0][1]");
    CHECK_EQ(ids_of(engine.searcher().search("garden")), "[1][0]");
}

TEST(keyword_field_indexes_its_raw_value_as_one_term_under_its_field) {
    Indexer indexer("stopwords.txt");
    Document document = make_text_document("Widget", "A small widget");
    document.add({"sku", "AB-123 x", FieldOptions::keyword()});
    const int id = indexer.add_document(document);
    const InvertedIndex& index = indexer.get_index();

    CHECK_EQ(postings_of(index, "sku", "AB-123 x"), "(" + std::to_string(id) + ":1)");
    CHECK_EQ(postings_of(index, "sku", "ab-123 x"), "-");
    CHECK_EQ(postings_of(index, "sku", "AB-123"), "-");
    CHECK_EQ(postings_of(index, "title", "AB-123 x"), "-");
    CHECK_EQ(postings_of(index, "title", "widget"), "(" + std::to_string(id) + ":1)");
    CHECK(index.get_postings("AB-123 x") != nullptr);
}

TEST(keyword_field_matches_only_the_exact_value_in_a_scoped_search) {
    Indexer indexer("stopwords.txt");
    Document first = make_text_document("First", "");
    first.add({"code", "abc", FieldOptions::keyword()});
    Document second = make_text_document("Second", "");
    second.add({"code", "abcd", FieldOptions::keyword()});
    indexer.add_document(first);
    indexer.add_document(second);

    const Searcher searcher(indexer.get_index(), indexer.get_analyzer());
    CHECK_EQ(ids_of(searcher.search_in("code", "abc ")), "[0]");
    CHECK_EQ(ids_of(searcher.search_in("code", "abcd ")), "[1]");
    CHECK(searcher.search_in("title", "abc ").empty());
}

TEST(removing_a_document_clears_its_postings_in_every_field) {
    InvertedIndex index;
    index.add_document(1, FieldTermFreqs{{"title", {{"cat", 1}}}, {"body", {{"cat", 2}, {"dog", 1}}}});
    index.add_document(2, FieldTermFreqs{{"body", {{"cat", 1}}}});

    CHECK_EQ(index.get_field_documents("title"), 1);
    CHECK_EQ(index.get_field_documents("body"), 2);
    CHECK_EQ(postings_of(index, "body", "cat"), "(1:2)(2:1)");

    index.remove_document(1);

    CHECK_EQ(postings_of(index, "title", "cat"), "-");
    CHECK_EQ(postings_of(index, "body", "cat"), "(2:1)");
    CHECK_EQ(postings_of(index, "body", "dog"), "-");
    CHECK(index.get_postings("dog") == nullptr);
    CHECK_EQ(index.get_field_documents("title"), 0);
    CHECK_EQ(index.get_field_documents("body"), 1);
    CHECK(index.terms_with_prefix("title", "c").empty());
    CHECK_EQ(index.get_unique_terms_count(), static_cast<std::size_t>(1));
    CHECK_EQ(index.get_total_documents(), 1);
}

TEST(index_keeps_the_same_term_in_two_fields_as_one_combined_posting) {
    InvertedIndex index;
    index.add_document(1, FieldTermFreqs{{"title", {{"cat", 1}}}, {"body", {{"cat", 2}}}});

    const std::vector<Posting>* combined = index.get_postings("cat");
    CHECK(combined != nullptr);
    CHECK_EQ(combined->size(), static_cast<std::size_t>(1));
    CHECK_EQ(combined->front().term_frequency, 3);
    CHECK_EQ(index.get_field_postings("cat").size(), static_cast<std::size_t>(2));
}

TEST(a_title_match_outranks_a_body_match) {
    Engine engine;
    engine.add("Notes", "rocket");
    engine.add("Rocket", "notes");

    CHECK_EQ(ids_of(engine.searcher().search("rocket")), "[1][0]");
}

TEST(a_title_prefix_match_outranks_a_body_prefix_match) {
    Engine engine;
    engine.add("Notes", "searching");
    engine.add("Searching", "notes");

    CHECK_EQ(ids_of(engine.searcher().search("search")), "[1][0]");
}

TEST(changing_a_boost_changes_the_order) {
    Engine engine;
    engine.add("Notes", "rocket");
    engine.add("Rocket", "notes");

    CHECK_EQ(ids_of(engine.searcher().search("rocket")), "[1][0]");
    CHECK_EQ(ids_of(engine.searcher(FieldBoosts().set("body", 5.0)).search("rocket")), "[0][1]");
    CHECK_EQ(ids_of(engine.searcher(FieldBoosts().set("title", 1.0)).search("rocket")), "[0][1]");
    CHECK_EQ(ids_of(engine.searcher(FieldBoosts().set("title", 3.0).set("body", 3.0)).search("rocket")), "[0][1]");
}

TEST(a_higher_boost_scales_a_field_s_contribution) {
    Engine engine;
    engine.add("Rocket", "");

    const double plain = engine.searcher(FieldBoosts()).search("rocket").front().score;
    const double boosted = engine.searcher(FieldBoosts().set("title", 3.0)).search("rocket").front().score;
    CHECK(std::abs(boosted - 3.0 * plain) < 1e-9);
}

TEST(documents_with_a_single_field_rank_the_same_whatever_the_boosts) {
    Indexer indexer("stopwords.txt");
    for (const char* body : {"rocket", "rocket rocket launch", "launch pad", "rocket launch pad"}) {
        Document document;
        document.add({"body", body});
        indexer.add_document(document);
    }

    const Searcher standard(indexer.get_index(), indexer.get_analyzer());
    const Searcher flat(indexer.get_index(), indexer.get_analyzer(), {.boosts = FieldBoosts()});
    const std::vector<SearchHit> expected = flat.search("rocket launch");
    const std::vector<SearchHit> actual = standard.search("rocket launch");

    CHECK_EQ(ids_of(actual), ids_of(expected));
    CHECK_EQ(ids_of(actual), "[1][3][0][2]");
    for (std::size_t i = 0; i < actual.size(); ++i) {
        CHECK(std::abs(actual[i].score - expected[i].score) < 1e-12);
    }
}

TEST(suggestions_can_be_limited_to_one_field) {
    Engine engine;
    engine.add("Searching", "notes");
    engine.add("Notes", "search search");

    const Searcher searcher = engine.searcher();
    CHECK_EQ(suggestions_text(searcher.suggest_in("title", "sear")), "searching:1,");
    CHECK_EQ(suggestions_text(searcher.suggest_in("body", "sear")), "search:1,");
    CHECK_EQ(suggestions_text(searcher.suggest("sear")), "search:1,searching:1,");
    CHECK(searcher.suggest_in("missing", "sear").empty());
}

TEST(prefix_search_can_be_limited_to_one_field) {
    Engine engine;
    engine.add("Searching", "notes");
    engine.add("Notes", "search");

    const Searcher searcher = engine.searcher();
    CHECK_EQ(ids_of(searcher.search_in("title", "sear")), "[0]");
    CHECK_EQ(ids_of(searcher.search_in("body", "sear")), "[1]");
    CHECK_EQ(ids_of(searcher.search("sear")), "[0][1]");
}
