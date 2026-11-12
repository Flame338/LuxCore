#include "TestFramework.h"

#include "Analyzer.h"
#include "Tokenizer.h"
#include "Utf8.h"

#include <string>
#include <vector>

namespace {

using Tokens = std::vector<std::string>;

Tokens analyze(const std::string& text) {
    static const Analyzer analyzer = make_standard_analyzer();
    return analyzer.analyze(text);
}

struct Form {
    const char* composed;
    const char* decomposed;
    const char* folded;
};

struct Run {
    char32_t first;
    char32_t last;
    char letter;
};

}

TEST(latin_extended_b_word_in_context) {
    CHECK_EQ(analyze("Huơng Ưng"), (Tokens{"huong", "ung"}));
    CHECK_EQ(analyze("BƀƁ Ƀ"), (Tokens{"bbb", "b"}));
}

TEST(fullwidth_text_matches_ascii_text) {
    CHECK_EQ(analyze("ＬｕｘＣｏｒｅ２０２６"),
             Tokens{"luxcore2026"});
    CHECK_EQ(analyze("ｈｅｌｌｏ ｗｏｒｌｄ"),
             analyze("hello world"));
    CHECK_EQ(analyze("ＡBｃ"), Tokens{"abc"});
}

TEST(a_byte_order_mark_does_not_attach_to_the_first_token) {
    CHECK_EQ(analyze("﻿hello world"), (Tokens{"hello", "world"}));
    CHECK_EQ(analyze("\xEF\xBB\xBFhello"), Tokens{"hello"});
    CHECK_EQ(analyze("﻿việt"), Tokens{"viet"});
}

TEST(query_with_a_leading_byte_order_mark_or_trailing_mark_analyzes_cleanly) {
    const Analyzer analyzer = make_standard_analyzer();

    const AnalyzedQuery bom = analyzer.analyze_query("﻿việt ");
    CHECK_EQ(bom.terms, Tokens{"viet"});
    CHECK(!bom.prefix.has_value());

    const AnalyzedQuery typing = analyzer.analyze_query("ｖｉệ");
    CHECK(typing.terms.empty());
    CHECK(typing.prefix.has_value());
    CHECK_EQ(typing.prefix.value_or(""), std::string("vie"));
}
