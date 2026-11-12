#include "TestFramework.h"

#include "Indexer.h"
#include "QueryParser.h"
#include "Searcher.h"

#include <memory>
#include <string>
#include <vector>

namespace {

struct QueryEngine {
    Indexer indexer{"stopwords.txt"};
    QueryParser parser{indexer.get_analyzer()};

    int add(const std::string& body, const std::string& title = "") {
        return indexer.add_document(make_text_document(title, body));
    }

    Searcher searcher(Scoring scoring = Scoring::tf_idf(), FieldBoosts boosts = FieldBoosts::standard()) const {
        return Searcher(indexer, {.boosts = std::move(boosts), .scoring = scoring});
    }

    std::string ids(const Query& query) const {
        std::string text;
        for (const int id : searcher().matching_ids(query)) {
            text += "[" + std::to_string(id) + "]";
        }
        return text;
    }

    std::string match(const std::string& text) const {
        const ParseResult parsed = parser.parse(text);
        return parsed.ok() ? ids(*parsed.query) : "error: " + parsed.error->message;
    }
};

std::unique_ptr<Query> term(const std::string& text, std::optional<std::string> field = std::nullopt) {
    return std::make_unique<TermQuery>(text, std::move(field));
}

std::unique_ptr<Query> phrase(std::vector<std::string> terms, std::optional<std::string> field = std::nullopt) {
    return std::make_unique<PhraseQuery>(terms, std::move(field));
}

void fill_animals(QueryEngine& engine) {
    engine.add("the quick brown fox jumps over the lazy dog");
    engine.add("a red fox and a brown bear");
    engine.add("red fox sleeps");
    engine.add("brown bear eats fish");
    engine.add("fox fox fox");
}

std::string ids_of(const std::vector<SearchHit>& hits) {
    std::string text;
    for (const SearchHit& hit : hits) {
        text += "[" + std::to_string(hit.doc_id) + "]";
    }
    return text;
}

}

TEST(term_query_can_be_scoped_to_a_field) {
    QueryEngine engine;
    engine.add("nothing here", "rocket");
    engine.add("rocket launch", "nothing");

    CHECK_EQ(engine.ids(*term("rocket")), "[0][1]");
    CHECK_EQ(engine.ids(*term("rocket", "title")), "[0]");
    CHECK_EQ(engine.ids(*term("rocket", "body")), "[1]");
    CHECK_EQ(engine.ids(*term("rocket", "summary")), "");
}

TEST(boolean_must_clauses_intersect) {
    QueryEngine engine;
    fill_animals(engine);

    BooleanQuery query;
    query.add(Occur::Must, term("fox"));
    query.add(Occur::Must, term("brown"));
    CHECK_EQ(engine.ids(query), "[0][1]");
}

TEST(boolean_should_clauses_beside_a_must_clause_only_add_score) {
    QueryEngine engine;
    fill_animals(engine);

    BooleanQuery query;
    query.add(Occur::Must, term("fox"));
    query.add(Occur::Should, term("red"));
    CHECK_EQ(engine.ids(query), "[0][1][2][4]");

    const std::vector<SearchHit> hits = engine.searcher().search(query, 10);
    double plain = 0.0;
    double with_red = 0.0;
    for (const SearchHit& hit : hits) {
        if (hit.doc_id == 0) plain = hit.score;
        if (hit.doc_id == 2) with_red = hit.score;
    }
    CHECK(with_red > plain);
}

TEST(nested_boolean_queries_combine) {
    QueryEngine engine;
    fill_animals(engine);

    auto colour = std::make_unique<BooleanQuery>();
    colour->add(Occur::Should, term("brown"));
    colour->add(Occur::Should, term("red"));
    BooleanQuery query;
    query.add(Occur::Must, term("fox"));
    query.add(Occur::Must, std::move(colour));
    query.add(Occur::MustNot, term("bear"));
    CHECK_EQ(engine.ids(query), "[0][2]");

    auto not_bear = std::make_unique<BooleanQuery>();
    not_bear->add(Occur::Should, term("fox"));
    not_bear->add(Occur::MustNot, term("red"));
    BooleanQuery outer;
    outer.add(Occur::Should, std::move(not_bear));
    outer.add(Occur::Should, term("fish"));
    CHECK_EQ(engine.ids(outer), "[0][3][4]");
}

TEST(phrase_query_needs_adjacent_terms_in_order) {
    QueryEngine engine;
    fill_animals(engine);

    CHECK_EQ(engine.ids(*phrase({"brown", "fox"})), "[0]");
    CHECK_EQ(engine.ids(*phrase({"fox", "brown"})), "");
    CHECK_EQ(engine.ids(*phrase({"red", "fox"})), "[1][2]");
    CHECK_EQ(engine.ids(*phrase({"fox", "bear"})), "");
    CHECK_EQ(engine.ids(*phrase({"fox"})), "[0][1][2][4]");
    CHECK_EQ(engine.ids(*phrase({"brown", "zebra"})), "");
}

TEST(phrase_with_repeated_terms_cannot_reuse_one_occurrence) {
    QueryEngine engine;
    engine.add("fox den");
    engine.add("fox fox");
    engine.add("fox den fox");
    engine.add("fox fox fox fox");

    CHECK_EQ(engine.ids(*phrase({"fox", "fox"})), "[1][3]");
    CHECK_EQ(engine.ids(*phrase({"fox", "den", "fox"})), "[2]");
    CHECK_EQ(engine.ids(*phrase({"fox", "fox", "fox"})), "[3]");
}

TEST(phrase_does_not_cross_fields_or_repeated_field_values) {
    QueryEngine engine;
    engine.add("fox", "red");
    engine.add("red fox", "nothing");

    CHECK_EQ(engine.ids(*phrase({"red", "fox"})), "[1]");
    CHECK_EQ(engine.ids(*phrase({"red", "fox"}, "title")), "");
    CHECK_EQ(engine.ids(*phrase({"red", "fox"}, "body")), "[1]");

    Document document;
    document.add({"body", "alpha beta"});
    document.add({"body", "gamma delta"});
    engine.indexer.add_document(document);
    CHECK_EQ(engine.ids(*phrase({"beta", "gamma"})), "");
    CHECK_EQ(engine.ids(*phrase({"alpha", "beta"})), "[2]");
}

TEST(queries_handle_utf8_terms_and_phrases) {
    QueryEngine engine;
    engine.add("Un Caf\xC3\xA9 au lait");
    engine.add("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E");
    engine.add("na\xC3\xAFve cafe");

    CHECK_EQ(engine.ids(*term("cafe")), "[0][2]");
    CHECK_EQ(engine.ids(*phrase({"cafe", "au", "lait"})), "[0]");
    CHECK_EQ(engine.ids(*phrase({"\xE6\x97\xA5", "\xE6\x9C\xAC"})), "[1]");
    CHECK_EQ(engine.ids(*phrase({"\xE6\x9C\xAC", "\xE6\x97\xA5"})), "");
    CHECK_EQ(engine.match("CAF\xC3\x89"), "[0][2]");
    CHECK_EQ(engine.match("\"caf\xC3\xA9 AU lait\""), "[0]");
    CHECK_EQ(engine.match("\xE6\x97\xA5\xE6\x9C\xAC"), "[1]");
}

TEST(removed_documents_never_match_any_query_type) {
    QueryEngine engine;
    fill_animals(engine);
    engine.indexer.remove_document(0);
    engine.indexer.remove_document(4);

    CHECK_EQ(engine.ids(*term("fox")), "[1][2]");
    CHECK_EQ(engine.ids(*term("dog")), "");
    CHECK_EQ(engine.ids(*phrase({"brown", "fox"})), "");
    CHECK_EQ(engine.ids(*phrase({"red", "fox"})), "[1][2]");
    CHECK_EQ(engine.match("fox OR dog"), "[1][2]");
    CHECK_EQ(engine.match("fox -red"), "");
    for (const SearchHit& hit : engine.searcher().search(*term("fox"), 10)) {
        CHECK(hit.doc_id != 0 && hit.doc_id != 4);
    }
}

TEST(updated_documents_match_their_new_text_only) {
    QueryEngine engine;
    fill_animals(engine);
    engine.indexer.update_document(1, make_text_document("", "a green frog near the pond"));

    CHECK_EQ(engine.ids(*term("bear")), "[3]");
    CHECK_EQ(engine.ids(*phrase({"red", "fox"})), "[2]");
    CHECK_EQ(engine.ids(*phrase({"green", "frog"})), "[1]");
    CHECK_EQ(engine.ids(*phrase({"brown", "bear"})), "[3]");
}

TEST(query_scores_match_the_searcher_for_tf_idf_and_bm25) {
    QueryEngine engine;
    engine.add("fox den fox", "fox");
    engine.add("brown fox", "bear");
    engine.add("brown bear and a very long stretch of unrelated words", "brown");
    engine.add("nothing");

    for (const Scoring scoring : {Scoring::tf_idf(), Scoring::bm25_with()}) {
        const Searcher searcher(engine.indexer.get_index(), engine.indexer.get_analyzer(), {.scoring = scoring});

        const std::vector<SearchHit> expected = searcher.search("fox ", 10);
        const std::vector<SearchHit> actual = engine.searcher(scoring).search(*term("fox"), 10);
        CHECK_EQ(ids_of(actual), ids_of(expected));
        for (std::size_t i = 0; i < expected.size() && i < actual.size(); i++) {
            CHECK_NEAR(actual[i].score, expected[i].score, 1e-9);
        }

        BooleanQuery either;
        either.add(Occur::Should, term("brown"));
        either.add(Occur::Should, term("bear"));
        const std::vector<SearchHit> expected_either = searcher.search("brown bear ", 10);
        const std::vector<SearchHit> actual_either = engine.searcher(scoring).search(either, 10);
        CHECK_EQ(ids_of(actual_either), ids_of(expected_either));
        for (std::size_t i = 0; i < expected_either.size() && i < actual_either.size(); i++) {
            CHECK_NEAR(actual_either[i].score, expected_either[i].score, 1e-9);
        }

        const std::vector<SearchHit> expected_title = searcher.search_in("title", "brown ", 10);
        const std::vector<SearchHit> actual_title = engine.searcher(scoring).search(*term("brown", "title"), 10);
        CHECK_EQ(ids_of(actual_title), ids_of(expected_title));
        for (std::size_t i = 0; i < expected_title.size() && i < actual_title.size(); i++) {
            CHECK_NEAR(actual_title[i].score, expected_title[i].score, 1e-9);
        }
    }
}

TEST(searcher_runs_a_query_tree_with_top_k_and_keys) {
    Indexer indexer("stopwords.txt");
    indexer.add_document("a", make_text_document("", "fox"));
    indexer.add_document("b", make_text_document("", "fox fox"));
    indexer.add_document("c", make_text_document("", "fox"));
    const Searcher searcher(indexer);

    const std::vector<SearchHit> hits = searcher.search(TermQuery("fox"), 2);
    CHECK_EQ(hits.size(), static_cast<std::size_t>(2));
    CHECK_EQ(hits[0].key, "b");
    CHECK_EQ(hits[1].key, "a");
    CHECK(searcher.search(TermQuery("fox"), 0).empty());
    CHECK_EQ(searcher.search(TermQuery("fox"), 10).size(), static_cast<std::size_t>(3));
}
