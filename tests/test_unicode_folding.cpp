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

std::string encode(char32_t code_point) {
    std::string text;
    utf8::append(text, code_point);
    return text;
}

struct Form {
    const char* composed;
    const char* decomposed;
    const char* folded;
};

void check_forms(const std::vector<Form>& forms) {
    for (const Form& form : forms) {
        CHECK_EQ(analyze(form.composed), Tokens{form.folded});
        CHECK_EQ(analyze(form.decomposed), Tokens{form.folded});
    }
}

struct Run {
    char32_t first;
    char32_t last;
    char letter;
};

void check_runs(const std::vector<Run>& runs) {
    for (const Run& run : runs) {
        for (char32_t code_point = run.first; code_point <= run.last; code_point++) {
            CHECK_EQ(analyze(encode(code_point)), Tokens{std::string(1, run.letter)});
        }
    }
}

}

TEST(vietnamese_viet_matches_viet_in_composed_and_decomposed_form) {
    CHECK_EQ(analyze("việt"), Tokens{"viet"});
    CHECK_EQ(analyze("ViỆT"), Tokens{"viet"});
    CHECK_EQ(analyze("việt"), Tokens{"viet"});
    CHECK_EQ(analyze("việt"), Tokens{"viet"});
    CHECK_EQ(analyze("việt"), Tokens{"viet"});
    CHECK_EQ(analyze("Tiếng Việt"), (Tokens{"tieng", "viet"}));
    CHECK_EQ(analyze("Tiếng Việt"), (Tokens{"tieng", "viet"}));
}

TEST(vietnamese_tone_marks_fold_for_every_vowel_in_both_forms) {
    check_forms({
        {"Ạạ", "Ạạ", "aa"},
        {"Ảả", "Ảả", "aa"},
        {"Ấấ", "Ấấ", "aa"},
        {"Ầầ", "Ầầ", "aa"},
        {"Ẩẩ", "Ẩẩ", "aa"},
        {"Ẫẫ", "Ẫẫ", "aa"},
        {"Ậậ", "Ậậ", "aa"},
        {"Ắắ", "Ắắ", "aa"},
        {"Ặặ", "Ặặ", "aa"},
        {"Ẹẹ", "Ẹẹ", "ee"},
        {"Ẽẽ", "Ẽẽ", "ee"},
        {"Ệệ", "Ệệ", "ee"},
        {"Ỉỉ", "Ỉỉ", "ii"},
        {"Ịị", "Ịị", "ii"},
        {"Ọọ", "Ọọ", "oo"},
        {"Ớớ", "Ớớ", "oo"},
        {"Ợợ", "Ợợ", "oo"},
        {"Ụụ", "Ụụ", "uu"},
        {"Ứứ", "Ứứ", "uu"},
        {"Ựự", "Ựự", "uu"},
        {"Ỳỳ", "Ỳỳ", "yy"},
        {"Ỹỹ", "Ỹỹ", "yy"},
    });
}

TEST(vietnamese_d_with_stroke_folds_to_d) {
    CHECK_EQ(analyze("Đồng đỏ"), (Tokens{"dong", "do"}));
}

TEST(latin_extended_additional_precomposed_letters_fold_to_their_base_letter) {
    check_runs({
        {0x1E00, 0x1E01, 'a'}, {0x1E02, 0x1E07, 'b'}, {0x1E08, 0x1E09, 'c'},
        {0x1E0A, 0x1E13, 'd'}, {0x1E14, 0x1E1D, 'e'}, {0x1E1E, 0x1E1F, 'f'},
        {0x1E20, 0x1E21, 'g'}, {0x1E22, 0x1E2B, 'h'}, {0x1E2C, 0x1E2F, 'i'},
        {0x1E30, 0x1E35, 'k'}, {0x1E36, 0x1E3D, 'l'}, {0x1E3E, 0x1E43, 'm'},
        {0x1E44, 0x1E4B, 'n'}, {0x1E4C, 0x1E53, 'o'}, {0x1E54, 0x1E57, 'p'},
        {0x1E58, 0x1E5F, 'r'}, {0x1E60, 0x1E69, 's'}, {0x1E6A, 0x1E71, 't'},
        {0x1E72, 0x1E7B, 'u'}, {0x1E7C, 0x1E7F, 'v'}, {0x1E80, 0x1E89, 'w'},
        {0x1E8A, 0x1E8D, 'x'}, {0x1E8E, 0x1E8F, 'y'}, {0x1E90, 0x1E95, 'z'},
        {0x1E96, 0x1E96, 'h'}, {0x1E97, 0x1E97, 't'}, {0x1E98, 0x1E98, 'w'},
        {0x1E99, 0x1E99, 'y'}, {0x1E9A, 0x1E9A, 'a'}, {0x1E9B, 0x1E9D, 's'},
        {0x1EA0, 0x1EB7, 'a'}, {0x1EB8, 0x1EC7, 'e'}, {0x1EC8, 0x1ECB, 'i'},
        {0x1ECC, 0x1EE3, 'o'}, {0x1EE4, 0x1EF1, 'u'}, {0x1EF2, 0x1EF9, 'y'},
        {0x1EFC, 0x1EFD, 'v'}, {0x1EFE, 0x1EFF, 'y'},
    });
}

TEST(latin_extended_additional_special_expansions) {
    CHECK_EQ(analyze("ẞ"), Tokens{"ss"});
    CHECK_EQ(analyze("Straẞe"), Tokens{"strasse"});
    CHECK_EQ(analyze("Ỻỻ"), Tokens{"llll"});
}

TEST(latin_extended_additional_decomposed_forms_match_composed_forms) {
    check_forms({
        {"ḃ", "ḃ", "b"},
        {"ḍ", "ḍ", "d"},
        {"ḝ", "ḝ", "e"},
        {"ḥ", "ḥ", "h"},
        {"ḱ", "ḱ", "k"},
        {"ḷ", "ḷ", "l"},
        {"ṁ", "ṁ", "m"},
        {"ṇ", "ṇ", "n"},
        {"ṗ", "ṗ", "p"},
        {"ṛ", "ṛ", "r"},
        {"ṣ", "ṣ", "s"},
        {"ṭ", "ṭ", "t"},
        {"ṽ", "ṽ", "v"},
        {"ẅ", "ẅ", "w"},
        {"ẋ", "ẋ", "x"},
        {"ẓ", "ẓ", "z"},
        {"ẗ", "ẗ", "t"},
        {"ẙ", "ẙ", "y"},
    });
}

TEST(latin_extended_b_letters_with_marks_fold_to_their_base_letter) {
    check_runs({
        {0x0180, 0x0183, 'b'}, {0x0187, 0x0188, 'c'}, {0x0189, 0x018C, 'd'},
        {0x0191, 0x0192, 'f'}, {0x0193, 0x0193, 'g'}, {0x0197, 0x0197, 'i'},
        {0x0198, 0x0199, 'k'}, {0x019A, 0x019A, 'l'}, {0x019D, 0x019E, 'n'},
        {0x019F, 0x01A1, 'o'}, {0x01A4, 0x01A5, 'p'}, {0x01AB, 0x01AE, 't'},
        {0x01AF, 0x01B0, 'u'}, {0x01B2, 0x01B2, 'v'}, {0x01B3, 0x01B4, 'y'},
        {0x01B5, 0x01B6, 'z'}, {0x01CD, 0x01CE, 'a'}, {0x01CF, 0x01D0, 'i'},
        {0x01D1, 0x01D2, 'o'}, {0x01D3, 0x01DC, 'u'}, {0x01DE, 0x01E1, 'a'},
        {0x01E4, 0x01E7, 'g'}, {0x01E8, 0x01E9, 'k'}, {0x01EA, 0x01ED, 'o'},
        {0x01F0, 0x01F0, 'j'}, {0x01F4, 0x01F5, 'g'}, {0x01F8, 0x01F9, 'n'},
        {0x01FA, 0x01FB, 'a'}, {0x01FE, 0x01FF, 'o'}, {0x0200, 0x0203, 'a'},
        {0x0204, 0x0207, 'e'}, {0x0208, 0x020B, 'i'}, {0x020C, 0x020F, 'o'},
        {0x0210, 0x0213, 'r'}, {0x0214, 0x0217, 'u'}, {0x0218, 0x0219, 's'},
        {0x021A, 0x021B, 't'}, {0x021E, 0x021F, 'h'}, {0x0220, 0x0220, 'n'},
        {0x0221, 0x0221, 'd'}, {0x0224, 0x0225, 'z'}, {0x0226, 0x0227, 'a'},
        {0x0228, 0x0229, 'e'}, {0x022A, 0x0231, 'o'}, {0x0232, 0x0233, 'y'},
        {0x0234, 0x0234, 'l'}, {0x0235, 0x0235, 'n'}, {0x0236, 0x0236, 't'},
        {0x0237, 0x0237, 'j'}, {0x023A, 0x023A, 'a'}, {0x023B, 0x023C, 'c'},
        {0x023D, 0x023D, 'l'}, {0x023E, 0x023E, 't'}, {0x023F, 0x023F, 's'},
        {0x0240, 0x0240, 'z'}, {0x0243, 0x0243, 'b'}, {0x0244, 0x0244, 'u'},
        {0x0246, 0x0247, 'e'}, {0x0248, 0x0249, 'j'}, {0x024A, 0x024B, 'q'},
        {0x024C, 0x024D, 'r'}, {0x024E, 0x024F, 'y'},
    });
}

TEST(latin_extended_b_digraphs_and_ligatures_expand) {
    CHECK_EQ(analyze("ǄǅǆǱǲǳ"), Tokens{"dzdzdzdzdzdz"});
    CHECK_EQ(analyze("Ǉǈǉ"), Tokens{"ljljlj"});
    CHECK_EQ(analyze("Ǌǋǌ"), Tokens{"njnjnj"});
    CHECK_EQ(analyze("ǢǣǼǽ"), Tokens{"aeaeaeae"});
    CHECK_EQ(analyze("Ƣƣ"), Tokens{"oioi"});
    CHECK_EQ(analyze("Ȣȣ"), Tokens{"ouou"});
    CHECK_EQ(analyze("ƕǶ"), Tokens{"hvhv"});
    CHECK_EQ(analyze("ȸȹ"), Tokens{"dbqp"});
}

TEST(latin_extended_b_decomposed_forms_match_composed_forms) {
    check_forms({
        {"Ǎ", "Ǎ", "a"},
        {"ǎ", "ǎ", "a"},
        {"Ǐ", "Ǐ", "i"},
        {"Ǒ", "Ǒ", "o"},
        {"ǔ", "ǔ", "u"},
        {"ǘ", "ǘ", "u"},
        {"ǘ", "ǘ", "u"},
        {"Ǜ", "Ǜ", "u"},
        {"ǟ", "ǟ", "a"},
        {"ǧ", "ǧ", "g"},
        {"ǫ", "ǫ", "o"},
        {"ǵ", "ǵ", "g"},
        {"ǹ", "ǹ", "n"},
        {"ȁ", "ȁ", "a"},
        {"ȇ", "ȇ", "e"},
        {"ȓ", "ȓ", "r"},
        {"ș", "ș", "s"},
        {"ț", "ț", "t"},
        {"ȟ", "ȟ", "h"},
        {"ȧ", "ȧ", "a"},
        {"ȩ", "ȩ", "e"},
        {"ȭ", "ṏ", "o"},
        {"ȳ", "ȳ", "y"},
    });
}

TEST(latin_extended_b_word_in_context) {
    CHECK_EQ(analyze("Huơng Ưng"), (Tokens{"huong", "ung"}));
    CHECK_EQ(analyze("BƀƁ Ƀ"), (Tokens{"bbb", "b"}));
}

TEST(fullwidth_latin_letters_fold_to_ascii) {
    for (int i = 0; i < 26; i++) {
        const std::string expected(1, static_cast<char>('a' + i));
        CHECK_EQ(analyze(encode(0xFF21 + i)), Tokens{expected});
        CHECK_EQ(analyze(encode(0xFF41 + i)), Tokens{expected});
    }
}

TEST(fullwidth_digits_fold_to_ascii) {
    for (int i = 0; i < 10; i++) {
        CHECK_EQ(analyze(encode(0xFF10 + i)), Tokens{std::string(1, static_cast<char>('0' + i))});
    }
}

TEST(fullwidth_text_matches_ascii_text) {
    CHECK_EQ(analyze("ＬｕｘＣｏｒｅ２０２６"),
             Tokens{"luxcore2026"});
    CHECK_EQ(analyze("ｈｅｌｌｏ ｗｏｒｌｄ"),
             analyze("hello world"));
    CHECK_EQ(analyze("ＡBｃ"), Tokens{"abc"});
}

TEST(fullwidth_punctuation_and_space_still_separate_tokens) {
    CHECK_EQ(analyze("ＡＢ，ＣＤ"), (Tokens{"ab", "cd"}));
    CHECK_EQ(analyze("ＡＢ　ＣＤ"), (Tokens{"ab", "cd"}));
    CHECK_EQ(analyze("１．２"), (Tokens{"1", "2"}));
}

TEST(a_byte_order_mark_does_not_attach_to_the_first_token) {
    CHECK_EQ(analyze("﻿hello world"), (Tokens{"hello", "world"}));
    CHECK_EQ(analyze("\xEF\xBB\xBFhello"), Tokens{"hello"});
    CHECK_EQ(analyze("﻿việt"), Tokens{"viet"});
}

TEST(a_byte_order_mark_is_a_separator_anywhere) {
    CHECK_EQ(analyze("foo﻿bar"), (Tokens{"foo", "bar"}));
    CHECK_EQ(analyze("foo﻿"), Tokens{"foo"});
    CHECK_EQ(analyze("﻿﻿"), Tokens{});
    CHECK_EQ(analyze("﻿"), Tokens{});
}

TEST(tokenizer_splits_on_a_byte_order_mark_before_any_filtering) {
    const StandardTokenizer tokenizer;
    CHECK_EQ(tokenizer.tokenize("﻿Hello"), Tokens{"Hello"});
    CHECK_EQ(tokenizer.tokenize("a﻿b"), (Tokens{"a", "b"}));
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

TEST(ascii_text_is_unchanged_by_unicode_folding) {
    CHECK_EQ(analyze("Hello, World 42 foo_bar"), (Tokens{"hello", "world", "42", "foo", "bar"}));
}
