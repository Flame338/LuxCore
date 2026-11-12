#include "TestFramework.h"

#include "Analyzer.h"
#include "IdentifierTokenizer.h"

#include <string>
#include <vector>

namespace {

std::string describe(const std::vector<Token>& tokens) {
    std::string text;
    for (const Token& token : tokens) {
        text += "[" + token.term + "@" + std::to_string(token.position) + ":" +
                std::to_string(token.start_offset) + "-" + std::to_string(token.end_offset) + "]";
    }
    return text;
}

const Analyzer& plain_analyzer() {
    static const Analyzer analyzer = make_standard_analyzer();
    return analyzer;
}

const Analyzer& stop_analyzer() {
    static const Analyzer analyzer = make_standard_analyzer("stopwords.txt");
    return analyzer;
}

}

TEST(token_offsets_point_at_the_original_bytes_of_accented_text) {
    CHECK_EQ(describe(plain_analyzer().analyze_tokens("Caf\xC3\xA9 R\xC3\xA9sum\xC3\xA9")),
             "[cafe@0:0-5][resume@1:6-14]");
    CHECK_EQ(describe(plain_analyzer().analyze_tokens("Stra\xC3\x9F" "e")),
             "[strasse@0:0-7]");
    CHECK_EQ(describe(plain_analyzer().analyze_tokens("\xC3\x89" "COLE \xD0\x9C\xD0\xBE\xD1\x81")),
             "[ecole@0:0-6][\xD0\xBC\xD0\xBE\xD1\x81@1:7-13]");
}

TEST(query_prefix_is_folded_keeps_stop_words_and_keeps_original_offsets) {
    const AnalyzedTokenQuery accented = plain_analyzer().analyze_query_tokens("Caf\xC3\xA9");
    CHECK(accented.tokens.empty());
    CHECK_EQ(describe({*accented.prefix}), "[cafe@0:0-5]");

    const AnalyzedTokenQuery stop = stop_analyzer().analyze_query_tokens("the");
    CHECK_EQ(describe({*stop.prefix}), "[the@0:0-3]");
}

TEST(identifier_tokenizer_offsets_cover_each_split_word) {
    const Analyzer analyzer = make_identifier_analyzer();
    CHECK_EQ(describe(analyzer.analyze_tokens("getHTTPResponse")),
             "[get@0:0-3][http@1:3-7][response@2:7-15]");
    CHECK_EQ(describe(analyzer.analyze_tokens("fooBar baz")),
             "[foo@0:0-3][bar@1:3-6][baz@2:7-10]");

    const Analyzer keeping = make_identifier_analyzer({.keep_original = true});
    CHECK_EQ(describe(keeping.analyze_tokens("fooBar baz")),
             "[foobar@0:0-6][foo@0:0-3][bar@1:3-6][baz@2:7-10]");
}

TEST(tokenizer_terms_are_exactly_the_original_bytes_at_their_offsets) {
    const std::vector<std::string> inputs = {
        "Hello, World",
        "caf\xC3\xA9 au lait",
        "go \xE6\x97\xA5\xE6\x9C\xAC",
        "John's Mary\xE2\x80\x99s don't",
        "x\xFF" "y a\xC3",
        "",
    };
    for (const std::string& input : inputs) {
        for (const Token& token : StandardTokenizer().tokenize_tokens(input)) {
            CHECK(token.end_offset <= input.size());
            CHECK_EQ(input.substr(token.start_offset, token.end_offset - token.start_offset),
                     token.term);
        }
    }
}

TEST(analyze_equals_the_terms_of_analyze_tokens) {
    const std::vector<std::string> inputs = {
        "",
        "The Caf\xC3\xA9's menu and the Stra\xC3\x9F" "e",
        "go \xE6\x97\xA5\xE6\x9C\xAC of Mary\xE2\x80\x99s",
        "getHTTPResponse and fooBar",
        "  ,. ",
    };
    const Analyzer identifier = make_identifier_analyzer({.keep_original = true});
    for (const std::string& input : inputs) {
        CHECK_EQ(stop_analyzer().analyze(input), terms_of(stop_analyzer().analyze_tokens(input)));
        CHECK_EQ(identifier.analyze(input), terms_of(identifier.analyze_tokens(input)));

        const AnalyzedQuery query = stop_analyzer().analyze_query(input);
        const AnalyzedTokenQuery token_query = stop_analyzer().analyze_query_tokens(input);
        CHECK_EQ(query.terms, terms_of(token_query.tokens));
        CHECK_EQ(query.prefix.has_value(), token_query.prefix.has_value());
        if (query.prefix) {
            CHECK_EQ(*query.prefix, token_query.prefix->term);
        }
    }
}
