#include "TestFramework.h"

#include "Analyzer.h"
#include "Highlighter.h"
#include "IdentifierTokenizer.h"
#include "Indexer.h"
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

std::string snippet_of(const std::string& text, const std::string& query, HighlightOptions options = {}) {
    const Analyzer analyzer = make_standard_analyzer("stopwords.txt");
    return Highlighter(analyzer, std::move(options)).snippet(text, query);
}

HighlightOptions fuzzy(HighlightOptions options = {}) {
    options.fuzziness = Fuzziness::On;
    return options;
}

std::string snippet_for_query(const std::string& text, const std::string& query_text,
                              std::optional<std::string_view> field = std::nullopt,
                              HighlightOptions options = {}) {
    const Analyzer analyzer = make_standard_analyzer("stopwords.txt");
    const ParseResult parsed = QueryParser(analyzer).parse(query_text);
    return Highlighter(analyzer, std::move(options)).snippet(text, *parsed.query, field);
}

std::string snippet_for_built_query(const std::string& text, const Query& query,
                                    std::optional<std::string_view> field = std::nullopt) {
    const Analyzer analyzer = make_standard_analyzer("stopwords.txt");
    return Highlighter(analyzer).snippet(text, query, field);
}

std::unique_ptr<Query> term(const std::string& text, std::optional<std::string> field = std::nullopt) {
    return std::make_unique<TermQuery>(text, std::move(field));
}

class UnknownQuery : public Query {
public:
    ScoredDocs execute(const QueryContext&) const override { return {}; }
};

}

TEST(highlight_marks_an_ascii_term) {
    CHECK_EQ(snippet_of("The quick brown fox jumps over the lazy dog", "fox "),
             "The quick brown <em>fox</em> jumps over the lazy dog");
}

TEST(highlight_marks_every_occurrence_of_a_term) {
    CHECK_EQ(snippet_of("fox and fox and fox", "fox "), "<em>fox</em> and <em>fox</em> and <em>fox</em>");
}

TEST(highlight_marks_several_query_terms) {
    CHECK_EQ(snippet_of("The quick brown fox jumps over the lazy dog", "quick dog "),
             "The <em>quick</em> brown fox jumps over the lazy <em>dog</em>");
}

TEST(highlight_repeated_query_term_gives_one_mark_per_occurrence) {
    CHECK_EQ(snippet_of("a fox runs", "fox fox "), "a <em>fox</em> runs");
}

TEST(highlight_is_case_insensitive_both_ways) {
    CHECK_EQ(snippet_of("Lux engine", "LUX "), "<em>Lux</em> engine");
    CHECK_EQ(snippet_of("LUX engine", "lux "), "<em>LUX</em> engine");
}

TEST(highlight_does_not_mark_a_longer_word_when_the_query_word_is_finished) {
    CHECK_EQ(snippet_of("LuxCore rocks", "lux "), "LuxCore rocks");
}

TEST(highlight_ignores_stop_words_in_the_query) {
    CHECK_EQ(snippet_of("The quick fox", "the fox "), "The quick <em>fox</em>");
}

TEST(highlight_follows_the_analyzer_for_accents) {
    const std::string cafe_accented = "Caf" + utf8_of(U"é");
    CHECK_EQ(snippet_of("A " + cafe_accented + " in town", "cafe "), "A <em>" + cafe_accented + "</em> in town");
    CHECK_EQ(snippet_of("A Cafe in town", cafe_accented + " "), "A <em>Cafe</em> in town");
}

TEST(highlight_accent_marks_keep_the_original_bytes) {
    const std::string resume = "R" + utf8_of(U"é") + "sum" + utf8_of(U"é");
    CHECK_EQ(snippet_of("My " + resume + " is ready", "resume "), "My <em>" + resume + "</em> is ready");
}

TEST(highlight_marks_the_unfinished_last_word_as_a_prefix) {
    CHECK_EQ(snippet_of("A search engine for text", "searc"), "A <em>search</em> engine for text");
    CHECK_EQ(snippet_of("Searching is fun", "searc"), "<em>Searching</em> is fun");
}

TEST(highlight_marks_full_words_and_the_unfinished_last_word_together) {
    CHECK_EQ(snippet_of("A search engine for text", "engine sea"),
             "A <em>search</em> <em>engine</em> for text");
}

TEST(highlight_marks_cjk_neighbours_as_one_span) {
    const std::string text = utf8_of(U"我住在东京很久了");
    const std::string query = utf8_of(U"东京");
    CHECK_EQ(snippet_of(text, query),
             utf8_of(U"我住在") + "<em>" + utf8_of(U"东京") + "</em>" +
                 utf8_of(U"很久了"));
}

TEST(highlight_marks_a_single_cjk_character) {
    const std::string text = utf8_of(U"我住在东京很久了");
    const std::string query = utf8_of(U"京");
    CHECK_EQ(snippet_of(text, query),
             utf8_of(U"我住在东") + "<em>" + utf8_of(U"京") + "</em>" +
                 utf8_of(U"很久了"));
}

TEST(highlight_overlapping_identifier_tokens_give_one_mark) {
    const Analyzer analyzer = make_identifier_analyzer({.keep_original = true, .split_letters_digits = true});
    const Highlighter highlighter(analyzer);
    CHECK_EQ(highlighter.snippet("call getUserName now", "getUserName "), "call <em>getUserName</em> now");
    CHECK_EQ(highlighter.snippet("call getUserName2 now", "getUserName2 "), "call <em>getUserName2</em> now");
}

TEST(highlight_marks_only_the_matching_part_of_an_identifier) {
    const Analyzer analyzer = make_identifier_analyzer({.keep_original = true, .split_letters_digits = true});
    const Highlighter highlighter(analyzer);
    CHECK_EQ(highlighter.snippet("getUserName", "user "), "get<em>User</em>Name");
    CHECK_EQ(highlighter.snippet("getUserName", "get name "), "<em>get</em>User<em>Name</em>");
}

TEST(highlight_short_document_comes_back_whole_without_ellipsis) {
    CHECK_EQ(snippet_of("Short text about foxes", "foxes "), "Short text about <em>foxes</em>");
}

TEST(highlight_document_of_exactly_the_length_has_no_ellipsis) {
    CHECK_EQ(snippet_of("abc fox xyz", "fox ", bracketed(11)), "abc [fox] xyz");
    CHECK_EQ(snippet_of("abc fox xyz", "fox ", bracketed(10)), "~bc [fox] xyz");
}

TEST(highlight_long_document_is_cut_around_the_match_with_ellipsis_on_both_edges) {
    const std::string text = repeat(E_ACUTE + " ", 100) + "needle" + repeat(SUN + " ", 100);
    CHECK_EQ(snippet_of(text, "needle ", bracketed(40)),
             "~" + repeat(E_ACUTE + " ", 5) + "[needle]" + repeat(SUN + " ", 12) + "~");
}

TEST(highlight_match_at_the_start_has_no_left_ellipsis) {
    const std::string text = "needle" + repeat(SUN + " ", 100);
    CHECK_EQ(snippet_of(text, "needle ", bracketed(40)), "[needle]" + repeat(SUN + " ", 17) + "~");
}

TEST(highlight_match_at_the_end_has_no_right_ellipsis) {
    const std::string text = repeat(E_ACUTE + " ", 100) + "needle";
    CHECK_EQ(snippet_of(text, "needle ", bracketed(40)), "~" + repeat(E_ACUTE + " ", 17) + "[needle]");
}

TEST(highlight_disabled_ellipsis_adds_nothing_at_the_cut_edges) {
    const std::string text = repeat(E_ACUTE + " ", 100) + "needle" + repeat(SUN + " ", 100);
    HighlightOptions options = bracketed(40);
    options.ellipsis = "";
    CHECK_EQ(snippet_of(text, "needle ", options),
             repeat(E_ACUTE + " ", 5) + "[needle]" + repeat(SUN + " ", 12));
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

TEST(highlight_length_counts_code_points_not_bytes) {
    const std::string text = repeat(SUN, 30);
    HighlightOptions options{.length = 10, .start_marker = "", .end_marker = "", .ellipsis = ""};
    CHECK_EQ(snippet_of(text, "zzz ", options), repeat(SUN, 10));
}

TEST(highlight_marks_a_match_cut_by_the_window_edge_up_to_the_edge) {
    CHECK_EQ(snippet_of("aaa supercalifragilistic", "supercalifragilistic ", bracketed(8)), "~a [superc]~");
}

TEST(highlight_prefers_the_window_with_more_distinct_query_terms) {
    const std::string text = "alpha alpha alpha " + repeat("zz ", 100) + "alpha beta " + repeat("zz ", 100);
    const std::string snippet = snippet_of(text, "alpha beta ", bracketed(40));
    CHECK(snippet.find("[alpha] [beta]") != std::string::npos);
    CHECK(snippet.find("[alpha] [alpha]") == std::string::npos);
}

TEST(highlight_prefers_the_window_with_more_matches_when_distinct_terms_tie) {
    const std::string text = "alpha " + repeat("zz ", 100) + "alpha zz alpha " + repeat("zz ", 100);
    const std::string snippet = snippet_of(text, "alpha ", bracketed(40));
    CHECK(snippet.find("[alpha] zz [alpha]") != std::string::npos);
}

TEST(highlight_earliest_window_wins_a_tie) {
    const std::string text = "alpha " + repeat("zz ", 100) + "alpha " + repeat("zz ", 100);
    const std::string snippet = snippet_of(text, "alpha ", bracketed(40));
    CHECK(snippet.starts_with("[alpha] zz"));
}

TEST(highlight_without_a_match_returns_the_unmarked_beginning) {
    CHECK_EQ(snippet_of("alpha beta gamma delta epsilon", "zzz ", {.length = 12}), "alpha beta g...");
}

TEST(highlight_uses_the_configured_markers) {
    CHECK_EQ(snippet_of("a fox runs", "fox ", {.start_marker = "**", .end_marker = "**"}), "a **fox** runs");
    CHECK_EQ(snippet_of("a fox runs", "fox ", {.start_marker = "\x1B[1m", .end_marker = "\x1B[0m"}),
             "a \x1B[1mfox\x1B[0m runs");
}

TEST(highlight_empty_query_returns_the_text_unmarked) {
    CHECK_EQ(snippet_of("Some text", ""), "Some text");
    CHECK_EQ(snippet_of("Some text", "   "), "Some text");
}

TEST(highlight_empty_text_gives_an_empty_snippet) {
    CHECK_EQ(snippet_of("", "fox"), "");
}

TEST(highlight_document_overload_reads_the_named_field) {
    const Analyzer analyzer = make_standard_analyzer("stopwords.txt");
    const Highlighter highlighter(analyzer);
    const Document document = make_text_document("Fox tales", "A quick brown fox");
    CHECK_EQ(highlighter.snippet(document, "body", "fox "), "A quick brown <em>fox</em>");
    CHECK_EQ(highlighter.snippet(document, "title", "fox "), "<em>Fox</em> tales");
}

TEST(highlight_document_overload_returns_empty_for_a_missing_field) {
    const Analyzer analyzer = make_standard_analyzer("stopwords.txt");
    const Highlighter highlighter(analyzer);
    const Document document = make_text_document("Fox tales", "A quick brown fox");
    CHECK_EQ(highlighter.snippet(document, "summary", "fox "), "");
}

TEST(highlight_query_overloads_resolve_with_string_literals) {
    const Analyzer analyzer = make_standard_analyzer("stopwords.txt");
    const Highlighter highlighter(analyzer);
    const TermQuery query("fox");
    const Document document = make_text_document("Fox tales", "A quick brown fox");
    CHECK_EQ(highlighter.snippet("a fox runs", "fox "), "a <em>fox</em> runs");
    CHECK_EQ(highlighter.snippet("a fox runs", query), "a <em>fox</em> runs");
    CHECK_EQ(highlighter.snippet("a fox runs", query, "body"), "a <em>fox</em> runs");
    CHECK_EQ(highlighter.snippet(document, "body", "fox "), "A quick brown <em>fox</em>");
    CHECK_EQ(highlighter.snippet(document, "body", query), "A quick brown <em>fox</em>");
}

TEST(highlight_query_term_is_compared_as_an_index_term_without_analysis) {
    CHECK_EQ(snippet_for_built_query("The Fox runs", TermQuery("fox")), "The <em>Fox</em> runs");
    CHECK_EQ(snippet_for_built_query("The Fox runs", TermQuery("Fox")), "The Fox runs");
}

TEST(highlight_query_marks_a_parsed_term) {
    CHECK_EQ(snippet_for_query("The quick brown fox", "fox"), "The quick brown <em>fox</em>");
}

TEST(highlight_query_does_not_mark_a_prefix_of_a_longer_word) {
    CHECK_EQ(snippet_for_query("Searching is fun", "searc"), "Searching is fun");
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

TEST(highlight_query_skips_the_whole_subtree_of_a_must_not_clause) {
    CHECK_EQ(snippet_for_query("fox bear wolf", "fox -(bear OR wolf)"), "<em>fox</em> bear wolf");

    auto inner = std::make_unique<BooleanQuery>();
    inner->add(Occur::Must, term("bear"));
    inner->add(Occur::Should, term("wolf"));
    BooleanQuery outer;
    outer.add(Occur::Must, term("fox"));
    outer.add(Occur::MustNot, std::move(inner));
    CHECK_EQ(snippet_for_built_query("fox bear wolf", outer), "<em>fox</em> bear wolf");
}

TEST(highlight_query_marks_nested_groups) {
    CHECK_EQ(snippet_for_query("alpha beta gamma delta", "(alpha OR delta) AND gamma"),
             "<em>alpha</em> beta <em>gamma</em> <em>delta</em>");
    CHECK_EQ(snippet_for_query("alpha beta gamma delta", "alpha AND (beta OR (gamma AND -delta))"),
             "<em>alpha</em> <em>beta</em> <em>gamma</em> delta");
}

TEST(highlight_query_phrase_marks_only_complete_occurrences) {
    CHECK_EQ(snippet_for_query("quick fox and quick brown fox and brown quick", "\"quick brown\""),
             "quick fox and <em>quick</em> <em>brown</em> fox and brown quick");
}

TEST(highlight_query_phrase_marks_every_complete_occurrence) {
    CHECK_EQ(snippet_for_query("quick brown and quick brown", "\"quick brown\""),
             "<em>quick</em> <em>brown</em> and <em>quick</em> <em>brown</em>");
}

TEST(highlight_query_phrase_with_a_repeated_word_marks_each_word_once) {
    CHECK_EQ(snippet_for_query("go go go now", "\"go go\"", std::nullopt, {.start_marker = "[", .end_marker = "]"}),
             "[go] [go] [go] now");
}

TEST(highlight_query_phrase_keeps_the_gap_of_a_removed_stop_word) {
    CHECK_EQ(snippet_for_query("A cat and hat on a mat", "\"cat of hat\""),
             "A <em>cat</em> and <em>hat</em> on a mat");
    CHECK_EQ(snippet_for_query("A cat hat on a mat", "\"cat of hat\""), "A cat hat on a mat");
    CHECK_EQ(snippet_for_query("A cat on the hat", "\"cat of hat\""), "A cat on the hat");
}

TEST(highlight_query_phrase_built_by_hand_uses_its_relative_positions) {
    const PhraseQuery spaced(std::vector<PhraseTerm>{{"quick", 3}, {"fox", 5}});
    CHECK_EQ(snippet_for_built_query("a quick red fox", spaced), "a <em>quick</em> red <em>fox</em>");
    CHECK_EQ(snippet_for_built_query("a quick fox", spaced), "a quick fox");
}

TEST(highlight_query_phrase_finds_a_term_among_tokens_sharing_a_position) {
    const Analyzer analyzer = make_identifier_analyzer({.keep_original = true, .split_letters_digits = true});
    const Highlighter highlighter(analyzer);
    CHECK_EQ(highlighter.snippet("call getUserName now", PhraseQuery(std::vector<std::string>{"call", "get"})),
             "<em>call</em> <em>get</em>UserName now");
    CHECK_EQ(highlighter.snippet("call getUserName now", PhraseQuery(std::vector<std::string>{"call", "getusername"})),
             "<em>call</em> <em>getUserName</em> now");
    CHECK_EQ(highlighter.snippet("call getUserName now", PhraseQuery(std::vector<std::string>{"call", "user"})),
             "call getUserName now");
}

TEST(highlight_query_window_prefers_a_complete_phrase_over_scattered_words) {
    const std::string text =
        "quick zz brown " + repeat("zz ", 100) + "quick brown " + repeat("zz ", 100);
    const std::string snippet = snippet_for_query(text, "\"quick brown\"", std::nullopt, bracketed(40));
    CHECK(snippet.find("[quick] [brown]") != std::string::npos);
    CHECK(!snippet.starts_with("quick zz"));
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

TEST(highlight_query_without_a_field_scope_counts_every_term) {
    CHECK_EQ(snippet_for_query("A quick fox", "title:fox"), "A quick <em>fox</em>");
    CHECK_EQ(snippet_for_query("A quick brown fox", "body:\"quick brown\""),
             "A <em>quick</em> <em>brown</em> fox");
}

TEST(highlight_query_unscoped_terms_match_in_any_field) {
    CHECK_EQ(snippet_for_query("A quick fox", "fox", "body"), "A quick <em>fox</em>");
    CHECK_EQ(snippet_for_query("A quick fox", "fox", "title"), "A quick <em>fox</em>");
}

TEST(highlight_query_mixes_scoped_and_unscoped_terms) {
    CHECK_EQ(snippet_for_query("red fox and blue bear", "title:fox bear", "body"),
             "red fox and blue <em>bear</em>");
    CHECK_EQ(snippet_for_query("red fox and blue bear", "title:fox bear", "title"),
             "red <em>fox</em> and blue <em>bear</em>");
}

TEST(highlight_query_document_overload_scopes_to_the_named_field) {
    const Analyzer analyzer = make_standard_analyzer("stopwords.txt");
    const Highlighter highlighter(analyzer);
    const Document document = make_text_document("Fox tales", "A quick brown fox");
    const ParseResult title_query = QueryParser(analyzer).parse("title:fox");
    CHECK_EQ(highlighter.snippet(document, "title", *title_query.query), "<em>Fox</em> tales");
    CHECK_EQ(highlighter.snippet(document, "body", *title_query.query), "A quick brown fox");

    const ParseResult any_query = QueryParser(analyzer).parse("fox");
    CHECK_EQ(highlighter.snippet(document, "title", *any_query.query), "<em>Fox</em> tales");
    CHECK_EQ(highlighter.snippet(document, "body", *any_query.query), "A quick brown <em>fox</em>");
}

TEST(highlight_query_document_overload_returns_empty_for_a_missing_field) {
    const Analyzer analyzer = make_standard_analyzer("stopwords.txt");
    const Highlighter highlighter(analyzer);
    const Document document = make_text_document("Fox tales", "A quick brown fox");
    const ParseResult parsed = QueryParser(analyzer).parse("fox");
    CHECK_EQ(highlighter.snippet(document, "summary", *parsed.query), "");
}

TEST(highlight_query_follows_the_analyzer_for_accents) {
    const std::string cafe_accented = "Caf" + E_ACUTE;
    CHECK_EQ(snippet_for_query("A Cafe in town", cafe_accented), "A <em>Cafe</em> in town");
    CHECK_EQ(snippet_for_query("A " + cafe_accented + " in town", cafe_accented),
             "A <em>" + cafe_accented + "</em> in town");
    CHECK_EQ(snippet_for_query("A " + cafe_accented + " in town", "cafe"),
             "A <em>" + cafe_accented + "</em> in town");
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
    CHECK_EQ(Highlighter(analyzer).snippet(text, empty), text);
    CHECK_EQ(Highlighter(analyzer, {.length = 12}).snippet(text, empty), "alpha beta g...");
    CHECK_EQ(snippet_for_query(text, "", std::nullopt, {.length = 12}), "alpha beta g...");
}

TEST(highlight_query_with_only_must_not_clauses_marks_nothing) {
    CHECK_EQ(snippet_for_query("a fox and a bear", "-bear"), "a fox and a bear");
}

TEST(highlight_query_of_an_unknown_kind_contributes_nothing) {
    CHECK_EQ(snippet_for_built_query("a fox runs", UnknownQuery()), "a fox runs");

    BooleanQuery mixed;
    mixed.add(Occur::Must, std::make_unique<UnknownQuery>());
    mixed.add(Occur::Should, term("fox"));
    CHECK_EQ(snippet_for_built_query("a fox runs", mixed), "a <em>fox</em> runs");
}

TEST(highlight_query_and_string_paths_agree_on_a_plain_word) {
    const Analyzer analyzer = make_standard_analyzer("stopwords.txt");
    const Highlighter highlighter(analyzer);
    const ParseResult parsed = QueryParser(analyzer).parse("fox");
    CHECK_EQ(highlighter.snippet("The Fox runs", *parsed.query), highlighter.snippet("The Fox runs", "fox "));
}

TEST(highlight_fuzzy_is_off_by_default) {
    CHECK_EQ(snippet_of("A search engine", "serch "), "A search engine");
    CHECK(HighlightOptions{}.fuzziness == Fuzziness::Off);
}

TEST(highlight_fuzzy_marks_the_word_a_typo_stands_for) {
    CHECK_EQ(snippet_of("A search engine", "serch ", fuzzy()), "A <em>search</em> engine");
    CHECK_EQ(snippet_of("A search engine", "saerch ", fuzzy()), "A <em>search</em> engine");
    CHECK_EQ(snippet_of("A search engine", "serch engin ", fuzzy()), "A <em>search</em> <em>engine</em>");
}

TEST(highlight_fuzzy_also_marks_a_typo_in_the_text) {
    CHECK_EQ(snippet_of("A serch engine", "search ", fuzzy()), "A <em>serch</em> engine");
}

TEST(highlight_fuzzy_keeps_marking_the_exact_word) {
    CHECK_EQ(snippet_of("serch search", "serch ", fuzzy()), "<em>serch</em> <em>search</em>");
    CHECK_EQ(snippet_of("The fox", "fox ", fuzzy()), "The <em>fox</em>");
}

TEST(highlight_fuzzy_never_applies_to_words_of_one_or_two_code_points) {
    CHECK_EQ(snippet_of("an oz and ox", "ox ", fuzzy()), "an oz and <em>ox</em>");
    CHECK_EQ(snippet_of("x y z", "y ", fuzzy()), "x <em>y</em> z");
}

TEST(highlight_fuzzy_allows_one_edit_for_three_to_five_code_points) {
    CHECK_EQ(snippet_of("cot cut cow", "cat ", fuzzy()), "<em>cot</em> <em>cut</em> cow");
    CHECK_EQ(snippet_of("frut fract", "fruit ", fuzzy()), "<em>frut</em> fract");
}

TEST(highlight_fuzzy_allows_two_edits_from_six_code_points) {
    CHECK_EQ(snippet_of("orxnxe oxxnxe", "orange ", fuzzy()), "<em>orxnxe</em> oxxnxe");
}

TEST(highlight_fuzzy_does_not_mark_words_beyond_the_limit) {
    CHECK_EQ(snippet_of("A garden with tomatoes", "serch ", fuzzy()), "A garden with tomatoes");
    CHECK_EQ(snippet_of("A searching engine", "serch ", fuzzy()), "A searching engine");
}

TEST(highlight_fuzzy_repeated_query_word_gives_the_same_marks) {
    CHECK_EQ(snippet_of("A search engine", "serch serch ", fuzzy()), "A <em>search</em> engine");
}

TEST(highlight_fuzzy_counts_a_query_word_and_its_typo_hits_as_one_distinct_term) {
    const std::string text = "serch search " + repeat("zz ", 100) + "serch engine " + repeat("zz ", 100);
    const std::string snippet = snippet_of(text, "serch engine ", fuzzy(bracketed(40)));
    CHECK(snippet.find("[serch] [engine]") != std::string::npos);
    CHECK(!snippet.starts_with("[serch] [search]"));
}

TEST(highlight_fuzzy_counts_fuzzy_and_exact_hits_of_different_words_as_distinct_terms) {
    const std::string text = "search search search " + repeat("zz ", 100) + "search engine " + repeat("zz ", 100);
    const std::string snippet = snippet_of(text, "serch engine ", fuzzy(bracketed(40)));
    CHECK(snippet.find("[search] [engine]") != std::string::npos);
    CHECK(!snippet.starts_with("[search] [search]"));
}

TEST(highlight_fuzzy_assigns_a_word_to_the_nearest_query_word) {
    const std::string text = "search seerch " + repeat("zz ", 100) + "serchx engine " + repeat("zz ", 100);
    const std::string snippet = snippet_of(text, "serchx seerch engine ", fuzzy(bracketed(40)));
    CHECK(snippet.find("[serchx] [engine]") != std::string::npos);
    CHECK(!snippet.starts_with("[search]"));
}

TEST(highlight_fuzzy_follows_the_analyzer_for_accents) {
    const std::string cafe_accented = "Caf" + E_ACUTE;
    CHECK_EQ(snippet_of("A " + cafe_accented + " in town", "cafee ", fuzzy()),
             "A <em>" + cafe_accented + "</em> in town");
    CHECK_EQ(snippet_of("A " + cafe_accented + " in town", "cafw ", fuzzy()),
             "A <em>" + cafe_accented + "</em> in town");
    CHECK_EQ(snippet_of("A " + cafe_accented + " in town", "cafee ", {}), "A " + cafe_accented + " in town");
}

TEST(highlight_fuzzy_leaves_a_cjk_query_as_it_was) {
    const std::string text = utf8_of(U"我住在东京很久了");
    CHECK_EQ(snippet_of(text, utf8_of(U"东京"), fuzzy()), snippet_of(text, utf8_of(U"东京")));
    CHECK_EQ(snippet_of(text, utf8_of(U"东 "), fuzzy()),
             utf8_of(U"我住在") + "<em>" + utf8_of(U"东") + "</em>" + utf8_of(U"京很久了"));
}

TEST(highlight_fuzzy_does_not_touch_the_unfinished_last_word) {
    CHECK_EQ(snippet_of("serch results and search", "serch", fuzzy()), "<em>serch</em> results and search");
    CHECK_EQ(snippet_of("serch results and search", "serch ", fuzzy()),
             "<em>serch</em> results and <em>search</em>");
    CHECK_EQ(snippet_of("A search engine", "serch eng", fuzzy()), "A <em>search</em> <em>engine</em>");
}

TEST(highlight_fuzzy_applies_to_the_document_overload) {
    const Analyzer analyzer = make_standard_analyzer("stopwords.txt");
    const Highlighter highlighter(analyzer, fuzzy());
    const Document document = make_text_document("Search tales", "A quick brown fox");
    CHECK_EQ(highlighter.snippet(document, "title", "serch "), "<em>Search</em> tales");
    CHECK_EQ(highlighter.snippet(document, "body", "serch "), "A quick brown fox");
}

TEST(highlight_fuzzy_does_not_apply_to_a_query_tree) {
    const Analyzer analyzer = make_standard_analyzer("stopwords.txt");
    const Highlighter highlighter(analyzer, fuzzy());
    CHECK_EQ(highlighter.snippet("A search engine", TermQuery("serch")), "A search engine");
    CHECK_EQ(highlighter.snippet("A search engine", TermQuery("search")), "A <em>search</em> engine");
}

TEST(highlight_fuzzy_marks_what_a_fuzzy_search_found) {
    Indexer indexer("stopwords.txt");
    indexer.add_document("engine", make_text_document("Search engine", "ranking and indexing"));
    indexer.add_document("garden", make_text_document("Garden", "tomatoes and basil"));
    indexer.add_document("kitchen", make_text_document("Kitchen", "pots and pans"));
    const Searcher exact(indexer.get_index(), indexer.get_analyzer());
    const Searcher fuzzy_searcher(indexer.get_index(), indexer.get_analyzer(), FieldBoosts::standard(),
                                  &indexer.get_keys(), Scoring::tf_idf(), Fuzziness::On);
    const Highlighter plain(indexer.get_analyzer());
    const Highlighter fuzzy_highlighter(indexer.get_analyzer(), fuzzy());

    for (const std::string query : {"serch ", "engin ", "rankng ", "saerch engine "}) {
        const std::vector<SearchHit> hits = fuzzy_searcher.search(query);
        CHECK_EQ(hits.size(), std::size_t{1});
        for (const SearchHit& hit : hits) {
            const Document& document = indexer.get_document(hit.doc_id);
            CHECK_EQ(hit.key, "engine");
            const std::string title = fuzzy_highlighter.snippet(document, "title", query);
            const std::string body = fuzzy_highlighter.snippet(document, "body", query);
            CHECK(title.find("<em>") != std::string::npos || body.find("<em>") != std::string::npos);
        }
    }

    CHECK(exact.search("serch ").empty());
    CHECK(fuzzy_searcher.search("serch ").size() == 1);
    const Document& engine = indexer.get_document(fuzzy_searcher.search("serch ").front().doc_id);
    CHECK_EQ(fuzzy_highlighter.snippet(engine, "title", "serch "), "<em>Search</em> engine");
    CHECK_EQ(plain.snippet(engine, "title", "serch "), "Search engine");
}
