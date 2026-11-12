#include "TestFramework.h"

#include "Analyzer.h"
#include "Highlighter.h"
#include "IdentifierTokenizer.h"
#include "Indexer.h"
#include "InvertedIndex.h"
#include "MatchPlan.h"
#include "Query.h"
#include "QueryParser.h"
#include "Searcher.h"
#include "Utf8.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace {

std::string utf8_of(std::u32string_view code_points) {
    std::string text;
    for (const char32_t code_point : code_points) {
        utf8::append(text, code_point);
    }
    return text;
}

std::string repeat(const std::string& unit, int times) {
    std::string text;
    for (int i = 0; i < times; i++) {
        text += unit;
    }
    return text;
}

bool is_valid_utf8(const std::string& text) {
    for (std::size_t i = 0; i < text.size();) {
        const utf8::Decoded decoded = utf8::decode(text, i);
        if (decoded.code_point == utf8::replacement_character) {
            return false;
        }
        i += decoded.length;
    }
    return true;
}

const std::string E_ACUTE = utf8_of(U"é");
const std::string SUN = utf8_of(U"日");
const std::string EMOJI = utf8_of(U"\U0001F600");

HighlightOptions bracketed(std::size_t length) {
    return {.length = length, .start_marker = "[", .end_marker = "]", .ellipsis = "~"};
}

TermFreqs term_freqs_of(const Analyzer& analyzer, const std::string& text) {
    TermFreqs freqs;
    for (const std::string& token : analyzer.analyze(text)) {
        freqs[token]++;
    }
    return freqs;
}

std::string snippet_with(const Analyzer& analyzer, const std::string& text, const std::string& query,
                         HighlightOptions options = {}, Fuzziness fuzziness = Fuzziness::Off) {
    InvertedIndex index;
    index.add_document(0, FieldTermFreqs{{"body", term_freqs_of(analyzer, text)}});
    const Searcher searcher(index, analyzer, {.fuzziness = fuzziness});
    return Highlighter(std::move(options)).snippet(text, searcher.plan(query));
}

std::string snippet_of(const std::string& text, const std::string& query, HighlightOptions options = {}) {
    const Analyzer analyzer = make_standard_analyzer("stopwords.txt");
    return snippet_with(analyzer, text, query, std::move(options));
}

std::string fuzzy_snippet_of(const std::string& text, const std::string& query, HighlightOptions options = {}) {
    const Analyzer analyzer = make_standard_analyzer("stopwords.txt");
    return snippet_with(analyzer, text, query, std::move(options), Fuzziness::On);
}

std::string snippet_for_query(const std::string& text, const std::string& query_text,
                              std::optional<std::string_view> field = std::nullopt,
                              HighlightOptions options = {}) {
    const Analyzer analyzer = make_standard_analyzer("stopwords.txt");
    const ParseResult parsed = QueryParser(analyzer).parse(query_text);
    return Highlighter(std::move(options)).snippet(text, make_match_plan(analyzer, *parsed.query, field));
}

std::string snippet_for_built_query(const std::string& text, const Query& query,
                                    std::optional<std::string_view> field = std::nullopt) {
    const Analyzer analyzer = make_standard_analyzer("stopwords.txt");
    return Highlighter().snippet(text, make_match_plan(analyzer, query, field));
}

class UnknownQuery : public Query {
public:
    ScoredDocs execute(const QueryContext&) const override { return {}; }
    void add_leaves(QueryLeaves&) const override {}
};

}

TEST(highlight_overlapping_identifier_tokens_give_one_mark) {
    const Analyzer analyzer = make_identifier_analyzer({.keep_original = true, .split_letters_digits = true});
    CHECK_EQ(snippet_with(analyzer, "call getUserName now", "getUserName "), "call <em>getUserName</em> now");
    CHECK_EQ(snippet_with(analyzer, "call getUserName2 now", "getUserName2 "), "call <em>getUserName2</em> now");
}

TEST(highlight_matches_outside_the_window_are_not_marked) {
    const std::string text = "needle " + repeat("zz ", 100) + "needle";
    CHECK_EQ(snippet_of(text, "needle ", bracketed(20)), "[needle] zz zz zz zz z~");
}

TEST(highlight_never_splits_a_multibyte_character) {
    const std::string unit = E_ACUTE + SUN + EMOJI + " ";
    const std::string text = repeat(unit, 5) + "needle" + repeat(unit, 5);
    const std::size_t total = utf8::count_code_points(text);
    HighlightOptions options{.start_marker = "", .end_marker = "", .ellipsis = ""};
    for (std::size_t length = 1; length <= total + 5; length++) {
        options.length = length;
        const std::string snippet = snippet_of(text, "needle ", options);
        CHECK(is_valid_utf8(snippet));
        CHECK_EQ(utf8::count_code_points(snippet), std::min(length, total));
    }
}

TEST(highlight_empty_text_gives_an_empty_snippet) {
    CHECK_EQ(snippet_of("", "fox"), "");
}

TEST(highlight_one_highlighter_serves_text_plans_and_query_tree_plans) {
    Indexer indexer("stopwords.txt");
    const Document document = make_text_document("Fox tales", "A quick brown fox");
    indexer.add_document("fox", document);
    const Searcher searcher(indexer.get_index(), indexer.get_analyzer());
    const Highlighter highlighter;
    const TermQuery query("fox");
    const MatchPlan tree_plan = make_match_plan(indexer.get_analyzer(), query);
    CHECK_EQ(highlighter.snippet("a fox runs", searcher.plan("fox ")), "a <em>fox</em> runs");
    CHECK_EQ(highlighter.snippet("a fox runs", tree_plan), "a <em>fox</em> runs");
    CHECK_EQ(highlighter.snippet(document, "body", tree_plan), "A quick brown <em>fox</em>");
}

TEST(highlight_query_marks_must_and_should_clauses) {
    CHECK_EQ(snippet_for_query("a red fox met a bear", "+fox bear"),
             "a red <em>fox</em> met a <em>bear</em>");
    CHECK_EQ(snippet_for_query("a red fox met a bear", "fox AND bear"),
             "a red <em>fox</em> met a <em>bear</em>");
    CHECK_EQ(snippet_for_query("a red fox met a bear", "fox OR wolf"), "a red <em>fox</em> met a bear");
}

TEST(highlight_query_does_not_mark_a_must_not_term) {
    CHECK_EQ(snippet_for_query("a red fox met a bear", "fox -bear"), "a red <em>fox</em> met a bear");
    CHECK_EQ(snippet_for_query("a red fox met a bear", "fox NOT bear"), "a red <em>fox</em> met a bear");
}

TEST(highlight_query_marks_nested_groups) {
    CHECK_EQ(snippet_for_query("alpha beta gamma delta", "(alpha OR delta) AND gamma"),
             "<em>alpha</em> beta <em>gamma</em> <em>delta</em>");
    CHECK_EQ(snippet_for_query("alpha beta gamma delta", "alpha AND (beta OR (gamma AND -delta))"),
             "<em>alpha</em> <em>beta</em> <em>gamma</em> delta");
}

TEST(highlight_query_phrase_with_a_repeated_word_marks_each_word_once) {
    CHECK_EQ(snippet_for_query("go go go now", "\"go go\"", std::nullopt, {.start_marker = "[", .end_marker = "]"}),
             "[go] [go] [go] now");
}

TEST(highlight_query_phrase_built_by_hand_uses_its_relative_positions) {
    const PhraseQuery spaced(std::vector<PhraseTerm>{{"quick", 3}, {"fox", 5}});
    CHECK_EQ(snippet_for_built_query("a quick red fox", spaced), "a <em>quick</em> red <em>fox</em>");
    CHECK_EQ(snippet_for_built_query("a quick fox", spaced), "a quick fox");
}

TEST(highlight_query_phrase_finds_a_term_among_tokens_sharing_a_position) {
    const Analyzer analyzer = make_identifier_analyzer({.keep_original = true, .split_letters_digits = true});
    const Highlighter highlighter;
    const std::string text = "call getUserName now";
    const PhraseQuery call_get(std::vector<std::string>{"call", "get"});
    const PhraseQuery call_whole(std::vector<std::string>{"call", "getusername"});
    const PhraseQuery call_user(std::vector<std::string>{"call", "user"});
    CHECK_EQ(highlighter.snippet(text, make_match_plan(analyzer, call_get)), "<em>call</em> <em>get</em>UserName now");
    CHECK_EQ(highlighter.snippet(text, make_match_plan(analyzer, call_whole)),
             "<em>call</em> <em>getUserName</em> now");
    CHECK_EQ(highlighter.snippet(text, make_match_plan(analyzer, call_user)), "call getUserName now");
}

TEST(highlight_query_phrase_counts_as_one_distinct_term_in_the_window_choice) {
    const std::string text = "quick brown " + repeat("zz ", 100) + "fox bear " + repeat("zz ", 100);
    const std::string snippet = snippet_for_query(text, "\"quick brown\" fox bear", std::nullopt, bracketed(40));
    CHECK(snippet.find("[fox] [bear]") != std::string::npos);
    CHECK(!snippet.starts_with("[quick]"));
}

TEST(highlight_query_field_scope_ignores_terms_of_another_field) {
    CHECK_EQ(snippet_for_query("A quick fox", "title:fox", "body"), "A quick fox");
    CHECK_EQ(snippet_for_query("A quick fox", "title:fox", "title"), "A quick <em>fox</em>");
    CHECK_EQ(snippet_for_query("A quick brown fox", "body:\"quick brown\"", "title"), "A quick brown fox");
    CHECK_EQ(snippet_for_query("A quick brown fox", "body:\"quick brown\"", "body"),
             "A <em>quick</em> <em>brown</em> fox");
}

TEST(highlight_query_plan_scopes_to_its_field_for_the_document_overload) {
    const Analyzer analyzer = make_standard_analyzer("stopwords.txt");
    const Highlighter highlighter;
    const Document document = make_text_document("Fox tales", "A quick brown fox");
    const ParseResult title_query = QueryParser(analyzer).parse("title:fox");
    CHECK_EQ(highlighter.snippet(document, "title", make_match_plan(analyzer, *title_query.query, "title")),
             "<em>Fox</em> tales");
    CHECK_EQ(highlighter.snippet(document, "body", make_match_plan(analyzer, *title_query.query, "body")),
             "A quick brown fox");

    const ParseResult any_query = QueryParser(analyzer).parse("fox");
    CHECK_EQ(highlighter.snippet(document, "title", make_match_plan(analyzer, *any_query.query, "title")),
             "<em>Fox</em> tales");
    CHECK_EQ(highlighter.snippet(document, "body", make_match_plan(analyzer, *any_query.query, "body")),
             "A quick brown <em>fox</em>");
}

TEST(highlight_query_document_overload_returns_empty_for_a_missing_field) {
    const Analyzer analyzer = make_standard_analyzer("stopwords.txt");
    const Document document = make_text_document("Fox tales", "A quick brown fox");
    const ParseResult parsed = QueryParser(analyzer).parse("fox");
    CHECK_EQ(Highlighter().snippet(document, "summary", make_match_plan(analyzer, *parsed.query)), "");
}

TEST(highlight_query_marks_a_cjk_term_and_a_cjk_phrase) {
    const std::string text = utf8_of(U"我住在东京很久了");
    CHECK_EQ(snippet_for_query(text, utf8_of(U"京")),
             utf8_of(U"我住在东") + "<em>" + utf8_of(U"京") + "</em>" + utf8_of(U"很久了"));
    CHECK_EQ(snippet_for_query(text, utf8_of(U"东京")),
             utf8_of(U"我住在") + "<em>" + utf8_of(U"东京") + "</em>" + utf8_of(U"很久了"));
    CHECK_EQ(snippet_for_query(utf8_of(U"东西京"), utf8_of(U"东京")), utf8_of(U"东西京"));
}

TEST(highlight_query_empty_boolean_gives_the_unmarked_beginning) {
    const BooleanQuery empty;
    const Analyzer analyzer = make_standard_analyzer("stopwords.txt");
    const std::string text = "alpha beta gamma delta epsilon";
    CHECK_EQ(Highlighter().snippet(text, make_match_plan(analyzer, empty)), text);
    CHECK_EQ(Highlighter({.length = 12}).snippet(text, make_match_plan(analyzer, empty)), "alpha beta g...");
    CHECK_EQ(snippet_for_query(text, "", std::nullopt, {.length = 12}), "alpha beta g...");
}

TEST(highlight_query_with_only_must_not_clauses_marks_nothing) {
    CHECK_EQ(snippet_for_query("a fox and a bear", "-bear"), "a fox and a bear");
}

TEST(highlight_fuzzy_allows_one_edit_for_three_to_five_code_points) {
    CHECK_EQ(fuzzy_snippet_of("cot cut cow", "cat "), "<em>cot</em> <em>cut</em> cow");
    CHECK_EQ(fuzzy_snippet_of("frut fract", "fruit "), "<em>frut</em> fract");
}

TEST(highlight_fuzzy_does_not_mark_words_beyond_the_limit) {
    CHECK_EQ(fuzzy_snippet_of("A garden with tomatoes", "serch "), "A garden with tomatoes");
    CHECK_EQ(fuzzy_snippet_of("A searching engine", "serch "), "A searching engine");
}

TEST(highlight_fuzzy_repeated_query_word_gives_the_same_marks) {
    CHECK_EQ(fuzzy_snippet_of("A search engine", "serch serch "), "A <em>search</em> engine");
}

TEST(highlight_fuzzy_assigns_a_word_to_the_nearest_query_word) {
    const std::string text = "search seerch " + repeat("zz ", 100) + "serchx engine " + repeat("zz ", 100);
    const std::string snippet = fuzzy_snippet_of(text, "serchx seerch engine ", bracketed(40));
    CHECK(snippet.find("[serchx] [engine]") != std::string::npos);
    CHECK(!snippet.starts_with("[search]"));
}

TEST(highlight_fuzzy_follows_the_analyzer_for_accents) {
    const std::string cafe_accented = "Caf" + E_ACUTE;
    CHECK_EQ(fuzzy_snippet_of("A " + cafe_accented + " in town", "cafee "),
             "A <em>" + cafe_accented + "</em> in town");
    CHECK_EQ(fuzzy_snippet_of("A " + cafe_accented + " in town", "cafw "),
             "A <em>" + cafe_accented + "</em> in town");
    CHECK_EQ(snippet_of("A " + cafe_accented + " in town", "cafee "), "A " + cafe_accented + " in town");
}

TEST(highlight_fuzzy_leaves_a_cjk_query_as_it_was) {
    const std::string text = utf8_of(U"我住在东京很久了");
    CHECK_EQ(fuzzy_snippet_of(text, utf8_of(U"东京")), snippet_of(text, utf8_of(U"东京")));
    CHECK_EQ(fuzzy_snippet_of(text, utf8_of(U"东 ")),
             utf8_of(U"我住在") + "<em>" + utf8_of(U"东") + "</em>" + utf8_of(U"京很久了"));
}

TEST(highlight_fuzzy_does_not_touch_the_unfinished_last_word) {
    CHECK_EQ(fuzzy_snippet_of("serch results and search", "serch"), "<em>serch</em> results and search");
    CHECK_EQ(fuzzy_snippet_of("serch results and search", "serch "),
             "<em>serch</em> results and <em>search</em>");
    CHECK_EQ(fuzzy_snippet_of("A search engine", "serch eng"), "A <em>search</em> <em>engine</em>");
}

TEST(highlight_fuzzy_marks_what_a_fuzzy_search_found) {
    Indexer indexer("stopwords.txt");
    indexer.add_document("engine", make_text_document("Search engine", "ranking and indexing"));
    indexer.add_document("garden", make_text_document("Garden", "tomatoes and basil"));
    indexer.add_document("kitchen", make_text_document("Kitchen", "pots and pans"));
    const Searcher exact(indexer.get_index(), indexer.get_analyzer());
    const Searcher fuzzy_searcher(indexer, {.fuzziness = Fuzziness::On});
    const Highlighter highlighter;

    for (const std::string query : {"serch ", "engin ", "rankng ", "saerch engine "}) {
        const TextPlan plan = fuzzy_searcher.plan(query);
        const std::vector<SearchHit> hits = fuzzy_searcher.search(plan);
        CHECK_EQ(hits.size(), std::size_t{1});
        for (const SearchHit& hit : hits) {
            const Document& document = indexer.get_document(hit.doc_id);
            CHECK_EQ(hit.key, "engine");
            const std::string title = highlighter.snippet(document, "title", plan);
            const std::string body = highlighter.snippet(document, "body", plan);
            CHECK(title.find("<em>") != std::string::npos || body.find("<em>") != std::string::npos);
        }
    }

    CHECK(exact.search("serch ").empty());
    const TextPlan plan = fuzzy_searcher.plan("serch ");
    CHECK(fuzzy_searcher.search(plan).size() == 1);
    const Document& engine = indexer.get_document(fuzzy_searcher.search(plan).front().doc_id);
    CHECK_EQ(highlighter.snippet(engine, "title", plan), "<em>Search</em> engine");
    CHECK_EQ(highlighter.snippet(engine, "title", exact.plan("serch ")), "Search engine");
}

TEST(highlight_fuzzy_marks_only_the_candidates_the_search_kept) {
    const Analyzer analyzer = make_standard_analyzer("stopwords.txt");
    std::string kept_words;
    for (char letter = 'b'; letter <= 'u'; letter++) {
        kept_words += std::string("serch") + letter + " ";
    }
    const std::string text = kept_words + "serchz";

    InvertedIndex index;
    index.add_document(0, FieldTermFreqs{{"body", term_freqs_of(analyzer, text)}});
    index.add_document(1, FieldTermFreqs{{"body", term_freqs_of(analyzer, kept_words)}});
    index.add_document(2, FieldTermFreqs{{"body", term_freqs_of(analyzer, kept_words)}});
    index.add_document(3, FieldTermFreqs{{"body", term_freqs_of(analyzer, "serchz")}});
    const Searcher searcher(index, analyzer, {.fuzziness = Fuzziness::On});

    const TextPlan plan = searcher.plan("serch ");
    std::vector<int> found;
    for (const SearchHit& hit : searcher.search(plan)) {
        found.push_back(hit.doc_id);
    }
    CHECK(std::ranges::find(found, 3) == found.end());

    const std::string snippet = Highlighter(bracketed(1000)).snippet(text, plan);
    CHECK(snippet.find("[serchb]") != std::string::npos);
    CHECK(snippet.find("[serchu]") != std::string::npos);
    CHECK(snippet.find("[serchz]") == std::string::npos);
    CHECK(snippet.find("serchz") != std::string::npos);
    CHECK_EQ(std::ranges::count(snippet, '['), 20);
}

TEST(highlight_one_code_point_prefix_marks_only_the_exact_word) {
    CHECK_EQ(snippet_of("q qux quick", "q"), "<em>q</em> qux quick");
    CHECK_EQ(snippet_of("qux quick", "q"), "qux quick");
}

TEST(highlight_three_code_point_prefix_marks_at_most_thirty_two_completions) {
    std::string text;
    for (char first = 'a'; first <= 'z'; first++) {
        text += std::string("tre") + first + "x ";
    }
    for (char first = 'a'; first <= 'g'; first++) {
        text += std::string("tre") + first + "y ";
    }
    const std::string snippet = snippet_of(text, "tre", bracketed(1000));
    CHECK_EQ(std::ranges::count(snippet, '['), 32);
    CHECK(snippet.find("[treax]") != std::string::npos);
    CHECK(snippet.find("[trezx]") == std::string::npos);
}

TEST(highlight_field_scoped_plan_marks_completions_chosen_from_that_field) {
    const Analyzer analyzer = make_standard_analyzer("stopwords.txt");
    const std::string text = "pad pal pan pat pay pax";
    InvertedIndex index;
    index.add_document(0, FieldTermFreqs{{"title", term_freqs_of(analyzer, "pad pal pan pat pay")},
                                         {"body", term_freqs_of(analyzer, "pax")}});
    index.add_document(1, FieldTermFreqs{{"body", term_freqs_of(analyzer, "pax")}});
    index.add_document(2, FieldTermFreqs{{"body", term_freqs_of(analyzer, "pax")}});
    const Searcher searcher(index, analyzer);
    const Highlighter highlighter;

    CHECK_EQ(highlighter.snippet(text, searcher.plan("pa")),
             "<em>pad</em> <em>pal</em> <em>pan</em> pat pay <em>pax</em>");
    CHECK_EQ(highlighter.snippet(text, searcher.plan("pa", "title")),
             "<em>pad</em> <em>pal</em> <em>pan</em> <em>pat</em> pay pax");
    CHECK_EQ(highlighter.snippet(text, searcher.plan("pa", "body")),
             "pad pal pan pat pay <em>pax</em>");
}
