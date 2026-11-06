#include "TestFramework.h"

#include "Analyzer.h"
#include "Highlighter.h"
#include "IdentifierTokenizer.h"
#include "Utf8.h"

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
