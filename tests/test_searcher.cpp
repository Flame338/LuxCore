#include "TestFramework.h"

#include "Indexer.h"
#include "Searcher.h"
#include "Utf8.h"

#include <cmath>

namespace {

struct Corpus {
    Analyzer analyzer = make_standard_analyzer("stopwords.txt");
    InvertedIndex index;

    void add(int id, const std::string& title, const std::string& body) {
        std::unordered_map<std::string, int> term_freqs;
        for (const std::string& token : analyzer.analyze(title + " " + body)) {
            term_freqs[token]++;
        }
        index.add_document(id, term_freqs);
    }

    std::vector<SearchHit> search(const std::string& query, int top_k = 10) const {
        return Searcher(index, analyzer).search(query, top_k);
    }

    std::vector<Suggestion> suggest(const std::string& query, std::size_t limit = 5) const {
        return Searcher(index, analyzer).suggest(query, limit);
    }
};

struct Engine {
    Indexer indexer{"stopwords.txt"};

    void add(const std::string& title, const std::string& body) {
        indexer.add_document(make_text_document(title, body));
    }

    std::vector<SearchHit> search(const std::string& query, int top_k = 10) const {
        return Searcher(indexer.get_index(), indexer.get_analyzer()).search(query, top_k);
    }

    std::vector<Suggestion> suggest(const std::string& query, std::size_t limit = 5) const {
        return Searcher(indexer.get_index(), indexer.get_analyzer()).suggest(query, limit);
    }
};

std::string cyrillic_word(const std::string& stem, int id) {
    std::string word = stem;
    utf8::append(word, 0x430 + id / 10);
    utf8::append(word, 0x430 + id % 10);
    return word;
}

std::string suggestions_text(const std::vector<Suggestion>& suggestions) {
    std::string text;
    for (const Suggestion& suggestion : suggestions) {
        text += suggestion.term + ":" + std::to_string(suggestion.doc_freq) + ",";
    }
    return text;
}

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

TEST(suggest_lists_completions_by_document_frequency_then_term) {
    Corpus corpus;
    corpus.add(1, "t", "search");
    corpus.add(2, "t", "search");
    corpus.add(3, "t", "searching");
    corpus.add(4, "t", "seared");
    corpus.add(5, "t", "seared");

    CHECK_EQ(suggestions_text(corpus.suggest("sear")), "search:2,seared:2,searching:1,");
}

TEST(suggest_includes_the_exact_term_among_the_completions) {
    Corpus corpus;
    corpus.add(1, "t", "search");
    corpus.add(2, "t", "searching");

    CHECK_EQ(suggestions_text(corpus.suggest("search")), "search:1,searching:1,");
}

TEST(suggest_returns_at_most_the_limit) {
    Corpus corpus;
    corpus.add(1, "t", "search");
    corpus.add(2, "t", "search");
    corpus.add(3, "t", "searching");
    corpus.add(4, "t", "seared");

    CHECK_EQ(suggestions_text(corpus.suggest("sear", 2)), "search:2,searching:1,");
}

TEST(suggest_is_empty_once_the_word_is_finished_or_the_query_is_empty) {
    Corpus corpus;
    corpus.add(1, "t", "search");

    CHECK(corpus.suggest("sear ").empty());
    CHECK(corpus.suggest("").empty());
    CHECK(corpus.suggest("zzz").empty());
}

TEST(suggest_completes_a_stop_word_being_typed) {
    Corpus corpus;
    corpus.add(1, "t", "theory");

    CHECK_EQ(suggestions_text(corpus.suggest("the")), "theory:1,");
}

TEST(searcher_finds_accented_text_by_its_plain_and_accented_forms) {
    Engine engine;
    engine.add("Trip", "Café Münchën");
    engine.add("Other", "tea");

    CHECK_EQ(ids_of(engine.search("cafe")), "[0]");
    CHECK_EQ(ids_of(engine.search("CAFÉ")), "[0]");
    CHECK_EQ(ids_of(engine.search("Café")), "[0]");
    CHECK_EQ(ids_of(engine.search("munchen")), "[0]");
    CHECK_EQ(ids_of(engine.search("MÜNCHËN")), "[0]");
    CHECK_EQ(ids_of(engine.search("café")), "[0]");
    CHECK_EQ(ids_of(engine.search("cafe munchen")), "[0]");
    CHECK(engine.search("cafes").empty());
}

TEST(searcher_finds_accented_text_by_a_typed_prefix) {
    Engine engine;
    engine.add("Trip", "Café Münchën");
    engine.add("Other", "tea");

    CHECK_EQ(ids_of(engine.search("caf")), "[0]");
    CHECK_EQ(ids_of(engine.search("CAF")), "[0]");
    CHECK_EQ(ids_of(engine.search("Mün")), "[0]");
    CHECK_EQ(ids_of(engine.search("cafe mun")), "[0]");
    CHECK_EQ(suggestions_text(engine.suggest("caf")), "cafe:1,");
}

TEST(searcher_finds_decomposed_documents_by_composed_queries) {
    Engine engine;
    engine.add("t", "résumé");

    CHECK_EQ(ids_of(engine.search("résumé")), "[0]");
    CHECK_EQ(ids_of(engine.search("RESUME")), "[0]");
    CHECK_EQ(ids_of(engine.search("res")), "[0]");
}

TEST(searcher_finds_han_documents_by_the_word_or_by_one_character) {
    Engine engine;
    engine.add("t", "東京");
    engine.add("t", "京都");
    engine.add("t", "大阪");

    CHECK_EQ(ids_of(engine.search("東京")), "[0][1]");
    CHECK_EQ(ids_of(engine.search("京")), "[0][1]");
    CHECK_EQ(ids_of(engine.search("東")), "[0]");
    CHECK_EQ(ids_of(engine.search("大阪")), "[2]");
    CHECK_EQ(ids_of(engine.search("東京タワー")), "[0][1]");
    CHECK(engine.search("名古屋").empty());
}

TEST(searcher_finds_cyrillic_documents_by_prefix_and_by_whole_word) {
    Engine engine;
    engine.add("Город", "Москва столица");
    engine.add("Город", "Мурманск");

    CHECK_EQ(ids_of(engine.search("москва")), "[0]");
    CHECK_EQ(ids_of(engine.search("МОСКВА")), "[0]");
    CHECK_EQ(ids_of(engine.search("МОСКВ")), "[0]");
    CHECK_EQ(ids_of(engine.search("МОС")), "[0]");
    CHECK_EQ(ids_of(engine.search("Мурм")), "[1]");
    CHECK_EQ(suggestions_text(engine.suggest("МОС")), "москва:1,");
}

TEST(searcher_keeps_cyrillic_yo_distinct_from_ye) {
    Engine engine;
    engine.add("t", "ёлка");
    engine.add("t", "елка");

    CHECK_EQ(ids_of(engine.search("ёлка")), "[0]");
    CHECK_EQ(ids_of(engine.search("ЕЛКА")), "[1]");
}

TEST(searcher_caps_a_two_letter_cyrillic_prefix_like_a_two_letter_ascii_one) {
    Engine engine;
    for (int n = 1; n <= 10; n++) {
        engine.add("t", cyrillic_word("ма", n));
    }

    CHECK_EQ(ids_of(engine.search("МА")), "[0][1][2][3]");
    CHECK_EQ(ids_of(engine.search("ма")), "[0][1][2][3]");
}

TEST(searcher_expands_nothing_for_a_one_letter_cyrillic_prefix) {
    Engine engine;
    engine.add("t", "мама");
    engine.add("t", "м");

    CHECK_EQ(ids_of(engine.search("М")), "[1]");
}

TEST(searcher_allows_thirty_two_completions_for_a_three_letter_cyrillic_prefix) {
    Engine engine;
    for (int n = 1; n <= 40; n++) {
        engine.add("t", cyrillic_word("мак", n));
    }

    CHECK_EQ(engine.search("мак", 100).size(), static_cast<std::size_t>(32));
}

TEST(searcher_counts_a_folded_prefix_in_code_points) {
    Engine engine;
    const char* words[] = {"sea", "seal", "seat", "sec", "see", "seed", "seek", "sell", "send", "sent"};
    for (const char* word : words) {
        engine.add("t", word);
    }

    CHECK_EQ(ids_of(engine.search("SÉ")), "[0][1][2][3]");
    CHECK_EQ(ids_of(engine.search("Sé")), ids_of(engine.search("se")));
}
