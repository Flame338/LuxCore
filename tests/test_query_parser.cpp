#include "TestFramework.h"

#include "Indexer.h"
#include "QueryParser.h"
#include "Searcher.h"

#include <string>

namespace {

std::string describe(const Query& query) {
    if (const auto* term = dynamic_cast<const TermQuery*>(&query)) {
        return (term->get_field() ? *term->get_field() + ":" : "") + term->get_term();
    }
    if (const auto* phrase = dynamic_cast<const PhraseQuery*>(&query)) {
        std::string text = phrase->get_field() ? *phrase->get_field() + ":\"" : "\"";
        std::size_t next = 0;
        for (const PhraseTerm& term : phrase->get_terms()) {
            for (; next < term.position; next++) {
                text += "_ ";
            }
            text += term.term + " ";
            next = term.position + 1;
        }
        text.back() = '"';
        return text;
    }
    const auto& boolean = dynamic_cast<const BooleanQuery&>(query);
    std::string text = "(";
    for (const BooleanClause& clause : boolean.get_clauses()) {
        if (text.size() > 1) text += " ";
        text += clause.occur == Occur::Must ? "+" : clause.occur == Occur::MustNot ? "-" : "";
        text += describe(*clause.query);
    }
    return text + ")";
}

struct ParserFixture {
    Analyzer analyzer = make_standard_analyzer("stopwords.txt");
    QueryParser parser{analyzer};

    std::string parse(const std::string& text) const {
        const ParseResult result = parser.parse(text);
        if (!result.ok()) {
            return "error@" + std::to_string(result.error->offset) + ": " + result.error->message;
        }
        return describe(*result.query);
    }
};

}

TEST(parser_reads_the_example_query) {
    ParserFixture fixture;
    CHECK_EQ(fixture.parse("fox AND (brown OR red) -bear"), "((+fox +(brown red)) -bear)");
}

TEST(parser_reads_single_terms_and_plain_groups) {
    ParserFixture fixture;
    CHECK_EQ(fixture.parse("fox"), "fox");
    CHECK_EQ(fixture.parse("(fox)"), "fox");
    CHECK_EQ(fixture.parse("((fox))"), "fox");
}

TEST(parser_treats_juxtaposition_as_or) {
    ParserFixture fixture;
    CHECK_EQ(fixture.parse("fox bear"), "(fox bear)");
    CHECK_EQ(fixture.parse("fox OR bear"), "(fox bear)");
    CHECK_EQ(fixture.parse("fox   bear  OR red"), "(fox bear red)");
}

TEST(parser_reads_plus_minus_and_not_prefixes) {
    ParserFixture fixture;
    CHECK_EQ(fixture.parse("+fox bear"), "(+fox bear)");
    CHECK_EQ(fixture.parse("fox -bear"), "(fox -bear)");
    CHECK_EQ(fixture.parse("fox NOT bear"), "(fox -bear)");
    CHECK_EQ(fixture.parse("-bear"), "(-bear)");
    CHECK_EQ(fixture.parse("(-bear)"), "(-bear)");
    CHECK_EQ(fixture.parse("+fox"), "fox");
}

TEST(parser_binds_and_tighter_than_or) {
    ParserFixture fixture;
    CHECK_EQ(fixture.parse("fox OR brown AND red"), "(fox (+brown +red))");
    CHECK_EQ(fixture.parse("fox AND brown OR red"), "((+fox +brown) red)");
    CHECK_EQ(fixture.parse("fox AND brown AND red"), "(+fox +brown +red)");
    CHECK_EQ(fixture.parse("fox AND NOT bear"), "(+fox -bear)");
    CHECK_EQ(fixture.parse("fox AND -bear"), "(+fox -bear)");
}

TEST(parser_nests_groups) {
    ParserFixture fixture;
    CHECK_EQ(fixture.parse("(fox OR bear) AND (brown OR red)"), "(+(fox bear) +(brown red))");
    CHECK_EQ(fixture.parse("fox -(brown red)"), "(fox -(brown red))");
    CHECK_EQ(fixture.parse("fox AND (bear OR (red AND brown))"), "(+fox +(bear (+red +brown)))");
}

TEST(parser_reads_quoted_phrases) {
    ParserFixture fixture;
    CHECK_EQ(fixture.parse("\"brown fox\""), "\"brown fox\"");
    CHECK_EQ(fixture.parse("\"fox\""), "fox");
    CHECK_EQ(fixture.parse("+\"brown fox\" -\"red bear\""), "(+\"brown fox\" -\"red bear\")");
    CHECK_EQ(fixture.parse("bear\"brown fox\""), "(bear \"brown fox\")");
}

TEST(parser_keeps_stop_word_gaps_inside_phrases) {
    ParserFixture fixture;
    CHECK_EQ(fixture.parse("\"cat of hat\""), "\"cat _ hat\"");
    CHECK_EQ(fixture.parse("\"the cat of the hat\""), "\"cat _ _ hat\"");
    CHECK_EQ(fixture.parse("\"cat hat\""), "\"cat hat\"");
}

TEST(parser_drops_clauses_that_analyze_to_nothing) {
    ParserFixture fixture;
    CHECK_EQ(fixture.parse("the"), "()");
    CHECK_EQ(fixture.parse("\"the of\""), "()");
    CHECK_EQ(fixture.parse("the AND fox"), "fox");
    CHECK_EQ(fixture.parse("fox -the"), "fox");
    CHECK_EQ(fixture.parse("fox AND (the OR of)"), "fox");
    CHECK_EQ(fixture.parse("fox or bear"), "(fox bear)");
}

TEST(parser_returns_an_empty_query_for_blank_input) {
    ParserFixture fixture;
    CHECK_EQ(fixture.parse(""), "()");
    CHECK_EQ(fixture.parse("   \t "), "()");
}

TEST(parser_reads_field_scoped_terms) {
    ParserFixture fixture;
    CHECK_EQ(fixture.parse("title:fox"), "title:fox");
    CHECK_EQ(fixture.parse("title:\"red fox\""), "title:\"red fox\"");
    CHECK_EQ(fixture.parse("title:(fox OR bear)"), "(title:fox title:bear)");
    CHECK_EQ(fixture.parse("title:fox body:fox"), "(title:fox body:fox)");
    CHECK_EQ(fixture.parse("+title:fox -body:bear"), "(+title:fox -body:bear)");
    CHECK_EQ(fixture.parse("title:(fox body:bear)"), "(title:fox body:bear)");
    CHECK_EQ(fixture.parse("title:the"), "()");
}

TEST(parser_analyzes_words_like_the_indexer_does) {
    ParserFixture fixture;
    CHECK_EQ(fixture.parse("Fox"), "fox");
    CHECK_EQ(fixture.parse("CAF\xC3\x89"), "cafe");
    CHECK_EQ(fixture.parse("John's"), "john");
    CHECK_EQ(fixture.parse("wi-fi"), "\"wi fi\"");
    CHECK_EQ(fixture.parse("state-of-the-art"), "\"state _ _ art\"");
    CHECK_EQ(fixture.parse("\xE6\x97\xA5\xE6\x9C\xAC"), "\"\xE6\x97\xA5 \xE6\x9C\xAC\"");
}

TEST(parser_treats_lowercase_operators_as_words_and_colons_in_odd_places_as_text) {
    ParserFixture fixture;
    CHECK_EQ(fixture.parse("fox and bear"), "(fox bear)");
    CHECK_EQ(fixture.parse("fox NOTHING"), "(fox nothing)");
    CHECK_EQ(fixture.parse("a-b:c"), "\"b c\"");
}

TEST(parser_reports_errors_with_a_message_and_offset) {
    ParserFixture fixture;
    CHECK_EQ(fixture.parse("fox AND"), "error@7: expected a term");
    CHECK_EQ(fixture.parse("AND fox"), "error@0: expected a term");
    CHECK_EQ(fixture.parse("fox OR OR bear"), "error@7: expected a term");
    CHECK_EQ(fixture.parse("(fox"), "error@0: missing closing parenthesis for the one opened here");
    CHECK_EQ(fixture.parse("fox (bear OR (red)"), "error@4: missing closing parenthesis for the one opened here");
    CHECK_EQ(fixture.parse("fox)"), "error@3: unmatched closing parenthesis");
    CHECK_EQ(fixture.parse("()"), "error@0: empty parentheses");
    CHECK_EQ(fixture.parse("fox \"brown"), "error@4: unterminated quote");
    CHECK_EQ(fixture.parse("title:"), "error@6: expected a term after 'title:'");
    CHECK_EQ(fixture.parse("title: fox"), "error@6: expected a term after 'title:'");
    CHECK_EQ(fixture.parse("title:-fox"), "error@6: expected a term after 'title:'");
    CHECK_EQ(fixture.parse("+"), "error@1: expected a term");
    CHECK_EQ(fixture.parse("- -fox"), "error@2: expected a term");
    CHECK_EQ(fixture.parse("NOT"), "error@3: expected a term");
    CHECK_EQ(fixture.parse("fox AND OR bear"), "error@8: expected a term");
}

TEST(parser_rejects_absurd_nesting_instead_of_overflowing_the_stack) {
    ParserFixture fixture;
    const std::string deep = std::string(500, '(') + "fox" + std::string(500, ')');
    CHECK_EQ(fixture.parse(deep).substr(0, 6), "error@");
    const std::string fine = std::string(60, '(') + "fox" + std::string(60, ')');
    CHECK_EQ(fixture.parse(fine), "fox");
}

TEST(parse_failures_carry_no_query) {
    ParserFixture fixture;
    const ParseResult failed = fixture.parser.parse("fox AND (");
    CHECK(!failed.ok());
    CHECK(failed.query == nullptr);

    const ParseResult fine = fixture.parser.parse("fox");
    CHECK(fine.ok());
    CHECK(fine.query != nullptr);
}

TEST(parsed_queries_search_an_index_end_to_end) {
    Indexer indexer("stopwords.txt");
    indexer.add_document("a", make_text_document("Brown fox", "the quick brown fox jumps over the lazy dog"));
    indexer.add_document("b", make_text_document("Red bear", "a red fox and a brown bear"));
    indexer.add_document("c", make_text_document("Fox tales", "red fox sleeps"));
    const QueryParser parser(indexer.get_analyzer());
    const Searcher searcher(indexer);

    auto keys_of = [&](const std::string& text) {
        const ParseResult parsed = parser.parse(text);
        std::string keys;
        for (const SearchHit& hit : searcher.search(*parsed.query, 10)) {
            keys += hit.key;
        }
        return keys;
    };
    CHECK_EQ(keys_of("fox AND (brown OR red) -bear"), "ac");
    CHECK_EQ(keys_of("title:fox"), "ac");
    CHECK_EQ(keys_of("title:bear"), "b");
    CHECK_EQ(keys_of("\"brown fox\""), "a");
    CHECK_EQ(keys_of("\"quick fox\""), "");
    CHECK_EQ(keys_of("\"jumps over the lazy\""), "a");
    CHECK_EQ(keys_of("\"jumps lazy\""), "");
    CHECK_EQ(keys_of("-bear"), "");
}
