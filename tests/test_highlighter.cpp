#include "TestFramework.h"

#include "Analyzer.h"
#include "Highlighter.h"
#include "IdentifierTokenizer.h"
#include "Utf8.h"

#include <algorithm>
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
