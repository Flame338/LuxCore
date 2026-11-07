#include "TestFramework.h"

#include "FuzzyIndex.h"
#include "Indexer.h"
#include "Searcher.h"

namespace {

Searcher fuzzy_searcher(const Indexer& indexer, Scoring scoring = Scoring::tf_idf()) {
    return Searcher(indexer.get_index(), indexer.get_analyzer(), FieldBoosts::standard(), &indexer.get_keys(),
                    scoring, Fuzziness::On);
}

Searcher exact_searcher(const Indexer& indexer) {
    return Searcher(indexer.get_index(), indexer.get_analyzer(), FieldBoosts::standard(), &indexer.get_keys());
}

std::string keys_of(const Searcher& searcher, const std::string& query) {
    std::string text;
    for (const SearchHit& hit : searcher.search(query)) {
        text += "[" + hit.key + "]";
    }
    return text;
}

bool is_candidate(const Indexer& indexer, const std::string& term, std::size_t max_distance) {
    for (const FuzzyMatch& match : indexer.get_index().fuzzy_terms(term, max_distance)) {
        if (match.distance == 0) {
            return true;
        }
    }
    return false;
}

bool same_hits(const std::vector<SearchHit>& a, const std::vector<SearchHit>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); i++) {
        if (a[i].doc_id != b[i].doc_id || a[i].score != b[i].score || a[i].key != b[i].key) {
            return false;
        }
    }
    return true;
}

void add_search_engine_corpus(Indexer& indexer) {
    indexer.add_document("engine", make_text_document("Search engine", "ranking and indexing"));
    indexer.add_document("garden", make_text_document("Garden", "tomatoes and basil"));
    indexer.add_document("kitchen", make_text_document("Kitchen", "pots and pans"));
}

}

TEST(searching_a_plan_gives_the_same_hits_and_scores_as_searching_the_text) {
    Indexer indexer("stopwords.txt");
    add_search_engine_corpus(indexer);
    indexer.add_document("search", make_text_document("Search search", "searching engines"));
    indexer.add_document("engine room", make_text_document("Engine room", "pots search"));

    for (const Searcher& searcher : {exact_searcher(indexer), fuzzy_searcher(indexer)}) {
        for (const std::string query : {"search ", "search engine ", "searc", "serch engin ", "serch eng", "engine engine ",
                                        "search search sear", "s", "se", "zzz ", ""}) {
            CHECK(same_hits(searcher.search(searcher.plan(query)), searcher.search(query)));
            CHECK(same_hits(searcher.search(searcher.plan(query, "title"), 3), searcher.search_in("title", query, 3)));
            CHECK(same_hits(searcher.search(searcher.plan(query, "body"), 3), searcher.search_in("body", query, 3)));
        }
    }
}

TEST(a_plan_decides_fuzziness_for_the_searcher_that_scores_it) {
    Indexer indexer("stopwords.txt");
    add_search_engine_corpus(indexer);
    const Searcher exact = exact_searcher(indexer);
    const Searcher fuzzy = fuzzy_searcher(indexer);

    CHECK(exact.search(fuzzy.plan("serch ")).size() == 1);
    CHECK(fuzzy.search(exact.plan("serch ")).empty());
}

TEST(fuzzy_search_finds_a_document_despite_a_typo_in_each_word) {
    Indexer indexer("stopwords.txt");
    add_search_engine_corpus(indexer);
    const Searcher searcher = fuzzy_searcher(indexer);

    CHECK_EQ(keys_of(searcher, "serch engine "), "[engine]");
    CHECK_EQ(keys_of(searcher, "search engin "), "[engine]");
    CHECK_EQ(keys_of(searcher, "serch engin "), "[engine]");
    CHECK_EQ(keys_of(searcher, "saerch engine "), "[engine]");
    CHECK_EQ(keys_of(searcher, "seaarch engine "), "[engine]");
}

TEST(fuzzy_search_combines_with_prefix_matching_on_the_unfinished_word) {
    Indexer indexer("stopwords.txt");
    add_search_engine_corpus(indexer);
    const Searcher searcher = fuzzy_searcher(indexer);

    CHECK_EQ(keys_of(searcher, "serch eng"), "[engine]");
    CHECK_EQ(keys_of(searcher, "serch engin"), "[engine]");
}

TEST(fuzzy_search_tolerates_a_typo_in_a_utf8_word) {
    Indexer indexer("stopwords.txt");
    indexer.add_document("cafe", make_text_document("Caf\xC3\xA9 menu", "espresso"));
    indexer.add_document("other", make_text_document("Garden", "tomatoes"));
    const Searcher searcher = fuzzy_searcher(indexer);

    CHECK_EQ(keys_of(searcher, "esprsso "), "[cafe]");
    CHECK_EQ(keys_of(searcher, "cafw menu "), "[cafe]");
}

TEST(fuzzy_search_is_off_by_default_and_finds_nothing_for_typos) {
    Indexer indexer("stopwords.txt");
    add_search_engine_corpus(indexer);
    const Searcher searcher = exact_searcher(indexer);

    CHECK_EQ(keys_of(searcher, "serch engine "), "[engine]");
    CHECK_EQ(keys_of(searcher, "serch "), "");
    CHECK_EQ(keys_of(searcher, "serch engin "), "");
}

TEST(fuzzy_search_ranks_an_exact_match_above_a_near_match) {
    Indexer indexer("stopwords.txt");
    indexer.add_document("typo", make_text_document("Serch notes", "serch serch serch serch serch"));
    indexer.add_document("exact", make_text_document("Notes", "search"));
    indexer.add_document("filler", make_text_document("Garden", "tomatoes"));

    for (const Scoring scoring : {Scoring::tf_idf(), Scoring::bm25_with()}) {
        const Searcher searcher = fuzzy_searcher(indexer, scoring);
        const std::vector<SearchHit> hits = searcher.search("search ");
        CHECK_EQ(hits.size(), static_cast<std::size_t>(2));
        CHECK_EQ(hits[0].key, "exact");
        CHECK_EQ(hits[1].key, "typo");
        CHECK(hits[0].score > hits[1].score);
    }
}

TEST(fuzzy_search_ranks_a_closer_match_above_a_farther_one) {
    Indexer indexer("stopwords.txt");
    indexer.add_document("far", make_text_document("Notes", "searxyh"));
    indexer.add_document("near", make_text_document("Notes", "searh"));
    const Searcher searcher = fuzzy_searcher(indexer);

    CHECK_EQ(keys_of(searcher, "search "), "[near][far]");
}

TEST(fuzzy_search_finds_nothing_for_a_word_with_no_near_term) {
    Indexer indexer("stopwords.txt");
    add_search_engine_corpus(indexer);
    const Searcher searcher = fuzzy_searcher(indexer);

    CHECK_EQ(keys_of(searcher, "zzzzzz "), "");
    CHECK_EQ(keys_of(searcher, "xq "), "");
}

TEST(fuzzy_search_respects_field_scope_and_boosts) {
    Indexer indexer("stopwords.txt");
    indexer.add_document("title-hit", make_text_document("Rocket", "garden"));
    indexer.add_document("body-hit", make_text_document("Garden", "rocket"));
    const Searcher searcher = fuzzy_searcher(indexer);

    CHECK_EQ(keys_of(searcher, "rocet "), "[title-hit][body-hit]");

    std::string scoped;
    for (const SearchHit& hit : searcher.search_in("body", "rocet ")) {
        scoped += "[" + hit.key + "]";
    }
    CHECK_EQ(scoped, "[body-hit]");
}

TEST(a_removed_documents_terms_are_no_longer_fuzzy_candidates) {
    Indexer indexer("stopwords.txt");
    indexer.add_document("a", make_text_document("Zeppelin", "airship"));
    indexer.add_document("b", make_text_document("Garden", "airship"));

    CHECK(is_candidate(indexer, "zeppelin", 0));
    CHECK(is_candidate(indexer, "airship", 0));

    indexer.remove_document("a");

    CHECK(!is_candidate(indexer, "zeppelin", 0));
    CHECK(!is_candidate(indexer, "zeppelin", 2));
    CHECK(is_candidate(indexer, "airship", 0));
    CHECK_EQ(keys_of(fuzzy_searcher(indexer), "zepelin "), "");
    CHECK_EQ(keys_of(fuzzy_searcher(indexer), "airshp "), "[b]");
}

TEST(an_updated_documents_old_terms_stop_matching_fuzzily) {
    Indexer indexer("stopwords.txt");
    indexer.add_document("a", make_text_document("Zeppelin", "airship"));
    indexer.add_document("a", make_text_document("Balloon", "airship"));

    CHECK(!is_candidate(indexer, "zeppelin", 0));
    CHECK(is_candidate(indexer, "balloon", 0));
    CHECK_EQ(keys_of(fuzzy_searcher(indexer), "zepelin "), "");
    CHECK_EQ(keys_of(fuzzy_searcher(indexer), "baloon "), "[a]");
}

TEST(fuzzy_index_removal_keeps_the_remaining_terms_and_reuses_nodes) {
    FuzzyIndex fuzzy;
    fuzzy.add("search");
    fuzzy.add("searching");
    fuzzy.add("seal");
    fuzzy.remove("search");
    fuzzy.remove("missing");

    CHECK_EQ(fuzzy.size(), static_cast<std::size_t>(2));
    CHECK(fuzzy.candidates("search", 0).empty());
    CHECK_EQ(fuzzy.candidates("searching", 0).size(), static_cast<std::size_t>(1));
    CHECK_EQ(fuzzy.candidates("seal", 0).size(), static_cast<std::size_t>(1));

    fuzzy.add("search");
    CHECK_EQ(fuzzy.size(), static_cast<std::size_t>(3));
    CHECK_EQ(fuzzy.candidates("search", 0).size(), static_cast<std::size_t>(1));
}
