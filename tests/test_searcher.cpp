#include "TestFramework.h"

#include "Searcher.h"

#include <cmath>

namespace {

struct Corpus {
    Analyzer analyzer = make_standard_analyzer("stopwords.txt");
    InvertedIndex index;
    std::unordered_map<int, Document> store;

    void add(int id, const std::string& title, const std::string& body) {
        store[id] = {id, title, body};

        std::unordered_map<std::string, int> term_freqs;
        for (const std::string& token : analyzer.analyze(title + " " + body)) {
            term_freqs[token]++;
        }
        index.add_document(id, term_freqs);
    }

    std::vector<SearchHit> search(const std::string& query, int top_k = 10) const {
        return Searcher(index, analyzer, store).search(query, top_k);
    }
};

std::string ids_of(const std::vector<SearchHit>& hits) {
    std::string text;
    for (const SearchHit& hit : hits) {
        text += "[" + std::to_string(hit.doc_id) + "]";
    }
    return text;
}

}

TEST(searcher_ranks_a_document_with_the_rarer_term_first) {
    Corpus corpus;
    corpus.add(1, "a", "foo bar");
    corpus.add(2, "b", "foo baz");
    corpus.add(3, "c", "foo qux");

    CHECK_EQ(ids_of(corpus.search("foo bar")).substr(0, 3), "[1]");
}

TEST(searcher_scores_with_tf_times_lucene_classic_idf) {
    Corpus corpus;
    corpus.add(1, "x", "apple apple pie");
    corpus.add(2, "y", "pie");

    const std::vector<SearchHit> hits = corpus.search("apple");
    CHECK_EQ(hits.size(), static_cast<std::size_t>(1));
    const double idf = std::log(3.0 / 2.0) + 1.0;
    CHECK_NEAR(hits[0].score, 2.0 * idf, 1e-9);
}

TEST(searcher_still_scores_a_term_that_appears_in_every_document) {
    Corpus corpus;
    corpus.add(1, "a", "foo");
    corpus.add(2, "b", "foo");
    corpus.add(3, "c", "foo");

    const std::vector<SearchHit> hits = corpus.search("foo");
    CHECK_EQ(hits.size(), static_cast<std::size_t>(3));
    CHECK(hits[0].score > 0.0);
}

TEST(searcher_orders_equal_scores_by_doc_id) {
    Corpus corpus;
    corpus.add(5, "a", "foo");
    corpus.add(2, "b", "foo");
    corpus.add(9, "c", "foo");

    CHECK_EQ(ids_of(corpus.search("foo")), "[2][5][9]");
}

TEST(searcher_returns_nothing_for_unknown_words_stop_words_or_an_empty_query) {
    Corpus corpus;
    corpus.add(1, "a", "foo bar");

    CHECK(corpus.search("zzz").empty());
    CHECK(corpus.search("the and").empty());
    CHECK(corpus.search("").empty());
}

TEST(searcher_limits_the_number_of_results_to_top_k) {
    Corpus corpus;
    for (int id = 1; id <= 5; id++) {
        corpus.add(id, "t", "foo");
    }

    CHECK_EQ(ids_of(corpus.search("foo", 2)), "[1][2]");
}

TEST(searcher_analyzes_the_query_the_same_way_as_documents) {
    Corpus corpus;
    corpus.add(1, "Notes", "John's book");

    CHECK_EQ(ids_of(corpus.search("JOHN")), "[1]");
    CHECK_EQ(ids_of(corpus.search("John\xE2\x80\x99s")), "[1]");
}


TEST(searcher_finds_a_word_from_its_prefix) {
    Corpus corpus;
    corpus.add(1, "t", "search engine");
    corpus.add(2, "t", "apple");

    CHECK_EQ(ids_of(corpus.search("sear")), "[1]");
}

TEST(searcher_ranks_the_exact_word_above_its_completions) {
    Corpus corpus;
    corpus.add(1, "t", "searching searching searching");
    corpus.add(2, "t", "sear");

    const std::vector<SearchHit> hits = corpus.search("sear");
    CHECK_EQ(ids_of(hits), "[2][1]");
    if (hits.size() != 2) {
        return;
    }
    const double idf = std::log(3.0 / 3.0) + 1.0;
    CHECK_NEAR(hits[0].score, 1.0 * idf, 1e-9);
    CHECK_NEAR(hits[1].score, 0.5 * 3.0 / 4.0 * idf, 1e-9);
}

TEST(searcher_does_not_expand_a_finished_word) {
    Corpus corpus;
    corpus.add(1, "t", "search");
    corpus.add(2, "t", "searching");

    CHECK_EQ(ids_of(corpus.search("search ")), "[1]");
    CHECK_EQ(ids_of(corpus.search("search")), "[1][2]");
}

TEST(searcher_caps_how_many_words_a_short_prefix_expands_to) {
    Corpus corpus;
    const char* words[] = {"sea", "seal", "seat", "sec", "see", "seed", "seek", "sell", "send", "sent"};
    for (int id = 1; id <= 10; id++) {
        corpus.add(id, "t", words[id - 1]);
    }

    CHECK_EQ(ids_of(corpus.search("se")), "[1][2][3][4]");
    CHECK(corpus.search("s").empty());
    CHECK_EQ(ids_of(corpus.search("sea")), "[1][2][3]");
}

TEST(searcher_keeps_the_most_common_completions_under_the_cap) {
    Corpus corpus;
    corpus.add(1, "t", "seal");
    corpus.add(2, "t", "sent");
    corpus.add(3, "t", "sent");
    corpus.add(4, "t", "sent");
    corpus.add(5, "t", "seed");
    corpus.add(6, "t", "seed");
    corpus.add(7, "t", "sea");
    corpus.add(8, "t", "seat");

    CHECK_EQ(ids_of(corpus.search("se")), "[1][2][3][4][5][6][7]");
}

TEST(searcher_allows_thirty_two_completions_for_a_long_prefix) {
    Corpus corpus;
    for (int id = 1; id <= 40; id++) {
        corpus.add(id, "t", "abc" + std::string(1, static_cast<char>('a' + id / 10)) +
                                std::string(1, static_cast<char>('a' + id % 10)));
    }

    CHECK_EQ(corpus.search("abc", 100).size(), static_cast<std::size_t>(32));
}

TEST(searcher_scores_a_prefix_with_one_idf_and_saturated_completions) {
    Corpus corpus;
    corpus.add(1, "t", "apple apple pie");
    corpus.add(2, "t", "pie");
    corpus.add(3, "t", "applesauce");

    const std::vector<SearchHit> hits = corpus.search("apple");
    CHECK_EQ(ids_of(hits), "[1][3]");
    if (hits.size() != 2) {
        return;
    }
    const double idf = std::log(4.0 / 3.0) + 1.0;
    CHECK_NEAR(hits[0].score, 2.0 * idf, 1e-9);
    CHECK_NEAR(hits[1].score, 0.25 * idf, 1e-9);
}

TEST(searcher_saturates_repeated_completions_instead_of_adding_them_up) {
    Corpus corpus;
    corpus.add(1, "t", "searching searched searches searcher");
    corpus.add(2, "t", "searching");
    corpus.add(3, "t", "pie");

    const std::vector<SearchHit> hits = corpus.search("sear");
    CHECK_EQ(ids_of(hits), "[1][2]");
    if (hits.size() != 2) {
        return;
    }
    const double idf = std::log(4.0 / 3.0) + 1.0;
    CHECK_NEAR(hits[0].score, 0.5 * 4.0 / 5.0 * idf, 1e-9);
    CHECK_NEAR(hits[1].score, 0.5 * 1.0 / 2.0 * idf, 1e-9);
}

TEST(searcher_combines_finished_words_with_the_prefix) {
    Corpus corpus;
    corpus.add(1, "t", "search engine");
    corpus.add(2, "t", "search party");

    const std::vector<SearchHit> hits = corpus.search("search eng");
    CHECK_EQ(ids_of(hits), "[1][2]");
    if (hits.size() != 2) {
        return;
    }
    const double search_idf = std::log(3.0 / 3.0) + 1.0;
    const double prefix_idf = std::log(3.0 / 2.0) + 1.0;
    CHECK_NEAR(hits[0].score, search_idf + 0.25 * prefix_idf, 1e-9);
    CHECK_NEAR(hits[1].score, search_idf, 1e-9);
}

TEST(searcher_finds_completions_of_a_stop_word_being_typed) {
    Corpus corpus;
    corpus.add(1, "t", "theory");

    CHECK_EQ(ids_of(corpus.search("the")), "[1]");
    CHECK(corpus.search("the ").empty());
}

TEST(searcher_matches_a_possessive_being_typed) {
    Corpus corpus;
    corpus.add(1, "Notes", "John's book");

    CHECK_EQ(ids_of(corpus.search("john'")), "[1]");
    CHECK_EQ(ids_of(corpus.search("John\xE2\x80\x99s")), "[1]");
}

TEST(searcher_returns_nothing_for_an_empty_blank_or_punctuation_query) {
    Corpus corpus;
    corpus.add(1, "t", "foo bar");

    CHECK(corpus.search("").empty());
    CHECK(corpus.search("   ").empty());
    CHECK(corpus.search(".").empty());
}

TEST(searcher_breaks_ties_between_completion_matches_by_doc_id) {
    Corpus corpus;
    corpus.add(7, "t", "searching");
    corpus.add(3, "t", "searching");
    corpus.add(5, "t", "searching");

    CHECK_EQ(ids_of(corpus.search("sear")), "[3][5][7]");
}

TEST(searcher_earlier_words_are_never_expanded) {
    Corpus corpus;
    corpus.add(1, "t", "search engine");
    corpus.add(2, "t", "searchable engine");

    const std::vector<SearchHit> partial = corpus.search("sear eng");
    CHECK_EQ(ids_of(partial), "[1][2]");
    if (partial.size() != 2) {
        return;
    }
    const double idf = std::log(3.0 / 3.0) + 1.0;
    CHECK_NEAR(partial[0].score, 0.25 * idf, 1e-9);
    CHECK_NEAR(partial[1].score, 0.25 * idf, 1e-9);

    const std::vector<SearchHit> whole = corpus.search("sear engine");
    CHECK_EQ(ids_of(whole), "[1][2]");
    if (whole.size() != 2) {
        return;
    }
    CHECK_NEAR(whole[0].score, 1.0 * idf, 1e-9);
    CHECK_NEAR(whole[1].score, 1.0 * idf, 1e-9);
}

TEST(searcher_exact_term_survives_the_cap_even_with_a_lower_doc_frequency) {
    Corpus corpus;
    corpus.add(50, "t", "q");
    for (int i = 0; i < 20; i++) {
        corpus.add(1 + i, "t", "q" + std::string(1, static_cast<char>('a' + i)));
    }
    for (int id = 101; id <= 103; id++) {
        corpus.add(id, "t", "qzz");
    }

    CHECK_EQ(ids_of(corpus.search("q", 200)), "[50]");

    Corpus two_letters;
    two_letters.add(50, "t", "qq");
    for (int i = 0; i < 20; i++) {
        two_letters.add(1 + i, "t", "qq" + std::string(1, static_cast<char>('a' + i)));
    }
    for (int id = 101; id <= 103; id++) {
        two_letters.add(id, "t", "qqzz");
    }

    const std::vector<SearchHit> hits = two_letters.search("qq", 200);
    CHECK_EQ(hits.size(), static_cast<std::size_t>(7));
    if (hits.size() != 7) {
        return;
    }
    CHECK_EQ(hits[0].doc_id, 50);
    CHECK_EQ(ids_of(hits), "[50][1][2][3][101][102][103]");
}

TEST(searcher_a_one_character_prefix_matches_only_an_exact_one_character_term) {
    Corpus corpus;
    corpus.add(1, "t", "s");
    corpus.add(2, "t", "sea");

    CHECK_EQ(ids_of(corpus.search("s")), "[1]");
}
