#include "TestFramework.h"

#include "Analyzer.h"

#include <algorithm>
#include <fstream>
#include <random>
#include <regex>
#include <set>

namespace {

const std::string CURLY = "\xE2\x80\x99";

std::string join(const std::vector<std::string>& tokens) {
    std::string text;
    for (const std::string& token : tokens) {
        text += "[" + token + "]";
    }
    return text;
}

std::string replace_all(std::string text, const std::string& from, const std::string& to) {
    for (std::size_t pos = text.find(from); pos != std::string::npos;
         pos = text.find(from, pos + to.size())) {
        text.replace(pos, from.size(), to);
    }
    return text;
}

std::vector<std::string> tokenizer_oracle(const std::string& input) {
    static const std::regex word("[A-Za-z0-9]+(?:['\\x01][A-Za-z0-9]+)*");
    const std::string marked = replace_all(input, CURLY, "\x01");

    std::vector<std::string> tokens;
    for (std::sregex_iterator it(marked.begin(), marked.end(), word), end; it != end; ++it) {
        tokens.push_back(replace_all(it->str(), "\x01", CURLY));
    }
    return tokens;
}

std::set<std::string> load_stop_words() {
    std::set<std::string> stop_words;
    std::ifstream file("stopwords.txt");
    for (std::string word; file >> word;) {
        stop_words.insert(word);
    }
    return stop_words;
}

std::vector<std::string> analyzer_oracle(const std::string& input) {
    static const std::set<std::string> stop_words = load_stop_words();

    std::vector<std::string> tokens;
    for (std::string token : tokenizer_oracle(input)) {
        std::transform(token.begin(), token.end(), token.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        token = replace_all(token, CURLY, "'");
        if (token.size() > 2 && token.compare(token.size() - 2, 2, "'s") == 0) {
            token.resize(token.size() - 2);
        }
        if (!stop_words.contains(token)) {
            tokens.push_back(token);
        }
    }
    return tokens;
}

std::string random_text(std::mt19937& rng) {
    static const std::string never_valid_bytes = "\xC0\xC1\xF5\xF6\xF7\xF8\xFB\xFC\xFD\xFE\xFF";
    static const std::string alphabet = "abcXYZ019 \t\n.,!?'''-_/()[]{}sS";

    std::string text;
    const int length = rng() % 40;
    for (int i = 0; i < length; i++) {
        switch (rng() % 10) {
            case 0: text += CURLY; break;
            case 1: text += CURLY.substr(0, 1 + rng() % 2); break;
            case 2: text += never_valid_bytes[rng() % never_valid_bytes.size()]; break;
            case 3: text += (rng() % 2 ? "the" : "And"); break;
            default: text += alphabet[rng() % alphabet.size()]; break;
        }
    }
    return text;
}

std::string random_unicode_text(std::mt19937& rng) {
    static const std::vector<std::string> pieces = {
        "a", "Z", "7", " ", ".", "'", CURLY, "the", "x",
        "caf\xC3\xA9", "\xC3\x89", "na\xC3\xAF" "ve", "e\xCC\x81", "\xC3\x9F", "\xC3\xB8",
        "\xD0\x9C\xD0\xBE\xD1\x81", "\xCE\x91\xCE\x98", "\xE6\x9D\xB1", "\xE4\xBA\xAC",
        "\xE3\x81\x82", "\xE3\x82\xAB", "\xED\x95\x9C", "\xF0\xA0\x80\x80",
        "\xE2\x80\x94", "\xE2\x80\xA6", "\xC2\xBF", "\xC3\x97", "\xF0\x9F\x98\x80", "\xEF\xBF\xBD",
        "\xC3", "\xE6\x9D", "\xE2\x80", "\xF0\x9F\x98", "\x80", "\xFF", "\xED\xA0\x80", "\xC0\x80",
    };

    std::string text;
    const int length = rng() % 14;
    for (int i = 0; i < length; i++) {
        text += pieces[rng() % pieces.size()];
    }
    return text;
}

bool appending_a_letter_is_consistent(const std::string& text) {
    StandardTokenizer tokenizer;
    const std::vector<std::string> tokens = tokenizer.tokenize(text);
    const std::vector<std::string> probe = tokenizer.tokenize(text + "x");
    if (probe.size() == tokens.size() + 1) {
        return std::equal(tokens.begin(), tokens.end(), probe.begin());
    }
    if (probe.size() == tokens.size() && !tokens.empty()) {
        return std::equal(tokens.begin(), tokens.end() - 1, probe.begin()) &&
               probe.back().starts_with(tokens.back());
    }
    return false;
}

std::string tokens_of(const std::string& text) {
    return join(StandardTokenizer().tokenize(text));
}

std::string analyzed(const std::string& text) {
    static const Analyzer analyzer = make_standard_analyzer("stopwords.txt");
    return join(analyzer.analyze(text));
}

std::string query_of(const std::string& text) {
    static const Analyzer analyzer = make_standard_analyzer("stopwords.txt");
    const AnalyzedQuery query = analyzer.analyze_query(text);
    return join(query.terms) + (query.prefix ? " prefix=" + *query.prefix : " prefix=none");
}

}

TEST(tokenizer_splits_on_non_alphanumerics_and_keeps_case) {
    CHECK_EQ(tokens_of("Hello, World! 42x"), "[Hello][World][42x]");
    CHECK_EQ(tokens_of("a-b c_d"), "[a][b][c][d]");
    CHECK_EQ(tokens_of(""), "");
    CHECK_EQ(tokens_of("  ,.  "), "");
}

TEST(tokenizer_keeps_non_ascii_letters_and_digits_with_bytes_and_case_untouched) {
    CHECK_EQ(tokens_of("caf\xC3\xA9 au lait"), "[caf\xC3\xA9][au][lait]");
    CHECK_EQ(tokens_of("café résumé"), "[café][résumé]");
    CHECK_EQ(tokens_of("Zażółć gęślą"), "[Zażółć][gęślą]");
    CHECK_EQ(tokens_of("naïve"), "[naïve]");
    CHECK_EQ(tokens_of("ÉCOLE Москва ΑΘΗΝΑ"), "[ÉCOLE][Москва][ΑΘΗΝΑ]");
    CHECK_EQ(tokens_of("aªb µm 2º"), "[aªb][µm][2º]");
    CHECK_EQ(tokens_of("١٢٣ x"), "[١٢٣][x]");
}

TEST(tokenizer_splits_on_non_ascii_punctuation_symbols_and_separators) {
    CHECK_EQ(tokens_of("“quoted” — text…"), "[quoted][text]");
    CHECK_EQ(tokens_of("¿Qué?"), "[Qué]");
    CHECK_EQ(tokens_of("5×3 8÷2"), "[5][3][8][2]");
    CHECK_EQ(tokens_of("a\xC2\xA0" "b"), "[a][b]");
    CHECK_EQ(tokens_of("a«b»c§d¶e°f±g"), "[a][b][c][d][e][f][g]");
    CHECK_EQ(tokens_of("a→b ★ c∑d"), "[a][b][c][d]");
    CHECK_EQ(tokens_of("a\xE2\xAF\xBF" "b"), "[a][b]");
    CHECK_EQ(tokens_of("a\xE2\xB0\x80" "b"), "[a\xE2\xB0\x80" "b]");
    CHECK_EQ(tokens_of("a、b。c「d」e"), "[a][b][c][d][e]");
    CHECK_EQ(tokens_of("a\xE3\x80\x80" "b"), "[a][b]");
    CHECK_EQ(tokens_of("a，b！c（d）e"), "[a][b][c][d][e]");
    CHECK_EQ(tokens_of("a\xEF\xB8\xB0" "b\xEF\xB9\xAF" "c"), "[a][b][c]");
    CHECK_EQ(tokens_of("a\xEF\xBC\x91" "b"), "[a\xEF\xBC\x91" "b]");
    CHECK_EQ(tokens_of("a😀b\xF0\x9F\x8C\x80" "c\xF0\x9F\xAB\xBF" "d"), "[a][b][c][d]");
    CHECK_EQ(tokens_of("a\xEE\x80\x80" "b\xEF\xA3\xBF" "c"), "[a][b][c]");
    CHECK_EQ(tokens_of("a\xEF\xBF\xBD" "b"), "[a][b]");
}

TEST(tokenizer_emits_han_and_hiragana_one_character_per_token) {
    CHECK_EQ(tokens_of("東京"), "[東][京]");
    CHECK_EQ(tokens_of("東京tower"), "[東][京][tower]");
    CHECK_EQ(tokens_of("abc東def"), "[abc][東][def]");
    CHECK_EQ(tokens_of("ひらがな"), "[ひ][ら][が][な]");
    CHECK_EQ(tokens_of("\xF0\xA0\x80\x80\xF0\xA0\x80\x81"), "[\xF0\xA0\x80\x80][\xF0\xA0\x80\x81]");
    CHECK_EQ(tokens_of("\xE3\x90\x80\xE4\xB6\xBF\xEF\xA4\x80"), "[\xE3\x90\x80][\xE4\xB6\xBF][\xEF\xA4\x80]");
    CHECK_EQ(tokens_of("東, 京"), "[東][京]");
}

TEST(tokenizer_keeps_katakana_and_hangul_as_word_runs) {
    CHECK_EQ(tokens_of("カタカナ ハンカク"), "[カタカナ][ハンカク]");
    CHECK_EQ(tokens_of("한국어 서울"), "[한국어][서울]");
    CHECK_EQ(tokens_of("カタカナabc"), "[カタカナabc]");
}

TEST(tokenizer_keeps_combining_marks_inside_the_word) {
    CHECK_EQ(tokens_of("cafe\xCC\x81"), "[cafe\xCC\x81]");
    CHECK_EQ(tokens_of("re\xCC\x81sume\xCC\x81 ok"), "[re\xCC\x81sume\xCC\x81][ok]");
}

TEST(tokenizer_keeps_apostrophes_between_non_ascii_letters_only) {
    CHECK_EQ(tokens_of("l'école"), "[l'école]");
    CHECK_EQ(tokens_of("l" + CURLY + "école"), "[l" + CURLY + "école]");
    CHECK_EQ(tokens_of("école' x"), "[école][x]");
    CHECK_EQ(tokens_of("a'東"), "[a][東]");
    CHECK_EQ(tokens_of("東'a"), "[東][a]");
    CHECK_EQ(tokens_of("a" + CURLY + "東"), "[a][東]");
    CHECK_EQ(tokens_of("a'\xC3" "b"), "[a][b]");
    CHECK_EQ(tokens_of("a'—b"), "[a][b]");
}

TEST(tokenizer_treats_invalid_and_truncated_utf8_as_separators) {
    CHECK_EQ(tokens_of("a\xFF" "b"), "[a][b]");
    CHECK_EQ(tokens_of("a\x80" "b"), "[a][b]");
    CHECK_EQ(tokens_of("caf\xC3"), "[caf]");
    CHECK_EQ(tokens_of("caf\xC3 au"), "[caf][au]");
    CHECK_EQ(tokens_of("a\xE6\x9D" "b"), "[a][b]");
    CHECK_EQ(tokens_of("a\xF0\x9F\x98" "b"), "[a][b]");
    CHECK_EQ(tokens_of("a\xC0\x80" "b"), "[a][b]");
    CHECK_EQ(tokens_of("a\xED\xA0\x80" "b"), "[a][b]");
    CHECK_EQ(tokens_of("a\xF4\x90\x80\x80" "b"), "[a][b]");
    CHECK_EQ(tokens_of("\xE6\x9D"), "");
    CHECK_EQ(tokens_of("\xC3\xC3\xA9"), "[\xC3\xA9]");
}

TEST(tokenizer_never_crashes_or_reads_past_the_end_on_arbitrary_bytes) {
    StandardTokenizer tokenizer;
    std::mt19937 rng(99);
    std::size_t total_tokens = 0;
    for (int i = 0; i < 20000; i++) {
        std::string text;
        const int length = rng() % 12;
        for (int j = 0; j < length; j++) {
            text += static_cast<char>(rng() % 256);
        }
        for (std::size_t cut = 0; cut <= text.size(); cut++) {
            total_tokens += tokenizer.tokenize(text.substr(0, cut)).size();
        }
    }
    CHECK(total_tokens > 0);
}

TEST(tokenizer_keeps_straight_apostrophes_between_alphanumerics) {
    CHECK_EQ(tokens_of("don't"), "[don't]");
    CHECK_EQ(tokens_of("Don't STOP me"), "[Don't][STOP][me]");
    CHECK_EQ(tokens_of("can't've"), "[can't've]");
    CHECK_EQ(tokens_of("it's the end"), "[it's][the][end]");
}

TEST(tokenizer_drops_apostrophes_at_word_edges) {
    CHECK_EQ(tokens_of("dogs' toys"), "[dogs][toys]");
    CHECK_EQ(tokens_of("'hello'"), "[hello]");
    CHECK_EQ(tokens_of("rock 'n' roll"), "[rock][n][roll]");
    CHECK_EQ(tokens_of("don''t"), "[don][t]");
}

TEST(tokenizer_keeps_curly_apostrophes_between_alphanumerics) {
    CHECK_EQ(tokens_of("Don" + CURLY + "t"), "[Don" + CURLY + "t]");
    CHECK_EQ(tokens_of("O" + CURLY + "Brien" + CURLY + "s car"),
             "[O" + CURLY + "Brien" + CURLY + "s][car]");
    CHECK_EQ(tokens_of(CURLY + "hello" + CURLY), "[hello]");
}

TEST(tokenizer_treats_a_truncated_curly_apostrophe_as_a_delimiter) {
    CHECK_EQ(tokens_of("don\xE2\x80t"), "[don][t]");
}

TEST(tokenizer_agrees_with_a_regex_on_random_text) {
    StandardTokenizer tokenizer;
    std::mt19937 rng(4242);
    int mismatches = 0;
    for (int i = 0; i < 20000; i++) {
        const std::string text = random_text(rng);
        if (tokenizer.tokenize(text) != tokenizer_oracle(text)) {
            if (++mismatches <= 3) {
                std::cout << "    tokenize mismatch: got " << join(tokenizer.tokenize(text))
                          << " expected " << join(tokenizer_oracle(text)) << "\n";
            }
        }
    }
    CHECK_EQ(mismatches, 0);
}

TEST(analyzer_lowercases_and_removes_stop_words) {
    CHECK_EQ(analyzed("The Quick brown FOX"), "[quick][brown][fox]");
    CHECK_EQ(analyzed("the and a of"), "");
}

TEST(analyzer_folds_curly_apostrophes_into_straight_ones) {
    CHECK_EQ(analyzed("Don" + CURLY + "t"), "[don't]");
    CHECK_EQ(analyzed("Don" + CURLY + "t"), analyzed("Don't"));
    CHECK_EQ(analyzed("rock" + CURLY + "n" + CURLY + "roll"), "[rock'n'roll]");
}

TEST(analyzer_strips_english_possessives) {
    CHECK_EQ(analyzed("John's book"), "[john][book]");
    CHECK_EQ(analyzed("John" + CURLY + "s book"), "[john][book]");
    CHECK_EQ(analyzed("Cats' and dogs's"), "[cats][dogs]");
    CHECK_EQ(analyzed("O" + CURLY + "Brien" + CURLY + "s car"), "[o'brien][car]");
}

TEST(analyzer_possessive_stripping_can_turn_a_contraction_into_a_stop_word) {
    CHECK_EQ(analyzed("it" + CURLY + "s the end"), "[end]");
}

TEST(analyzer_with_a_missing_stop_word_file_keeps_every_word) {
    const Analyzer analyzer = make_standard_analyzer("no-such-file.txt");
    CHECK_EQ(join(analyzer.analyze("The Cat")), "[the][cat]");
}

TEST(analyzer_runs_only_the_filters_it_was_given) {
    Analyzer analyzer(std::make_unique<StandardTokenizer>());
    analyzer.add_filter(std::make_unique<LowercaseFilter>());
    CHECK_EQ(join(analyzer.analyze("The Cat's Hat")), "[the][cat's][hat]");
}

TEST(analyzer_agrees_with_an_independent_implementation_on_random_text) {
    const Analyzer analyzer = make_standard_analyzer("stopwords.txt");
    std::mt19937 rng(4242);
    int mismatches = 0;
    for (int i = 0; i < 20000; i++) {
        const std::string text = random_text(rng);
        if (analyzer.analyze(text) != analyzer_oracle(text)) {
            if (++mismatches <= 3) {
                std::cout << "    analyze mismatch: got " << join(analyzer.analyze(text))
                          << " expected " << join(analyzer_oracle(text)) << "\n";
            }
        }
    }
    CHECK_EQ(mismatches, 0);
}

TEST(analyze_query_treats_the_last_word_as_a_prefix_only_while_typing) {
    CHECK_EQ(query_of("foo sear"), "[foo] prefix=sear");
    CHECK_EQ(query_of("foo sear "), "[foo][sear] prefix=none");
    CHECK_EQ(query_of("foo sear."), "[foo][sear] prefix=none");
    CHECK_EQ(query_of("Foo SEAR"), "[foo] prefix=sear");
}

TEST(analyze_query_keeps_a_stop_word_being_typed) {
    CHECK_EQ(query_of("the"), " prefix=the");
    CHECK_EQ(query_of("it"), " prefix=it");
    CHECK_EQ(query_of("search the"), "[search] prefix=the");
    CHECK_EQ(query_of("the search"), " prefix=search");
    CHECK_EQ(query_of("the "), " prefix=none");
}

TEST(analyze_query_handles_apostrophes_at_the_end_of_the_query) {
    CHECK_EQ(query_of("john'"), " prefix=john");
    CHECK_EQ(query_of("John" + CURLY), " prefix=john");
    CHECK_EQ(query_of("John" + CURLY + "s"), " prefix=john");
    CHECK_EQ(query_of("it's"), " prefix=it");
    CHECK_EQ(query_of("don't"), " prefix=don't");
    CHECK_EQ(query_of("foo '"), "[foo] prefix=none");
}

TEST(analyze_query_of_empty_or_blank_text_is_empty) {
    CHECK_EQ(query_of(""), " prefix=none");
    CHECK_EQ(query_of("   "), " prefix=none");
    CHECK_EQ(query_of("..."), " prefix=none");
}

TEST(analyze_query_treats_a_trailing_non_ascii_letter_as_part_of_the_open_word) {
    CHECK_EQ(query_of("caf\xC3\xA9"), " prefix=caf\xC3\xA9");
    CHECK_EQ(query_of("foo caf\xC3\xA9"), "[foo] prefix=caf\xC3\xA9");
    CHECK_EQ(query_of("caf\xC3\xA9 "), "[caf\xC3\xA9] prefix=none");
}

TEST(analyze_query_treats_a_trailing_han_character_as_a_finished_word) {
    CHECK_EQ(query_of("\xE6\x9D\xB1\xE4\xBA\xAC"), "[\xE6\x9D\xB1][\xE4\xBA\xAC] prefix=none");
    CHECK_EQ(query_of("tokyo \xE6\x9D\xB1"), "[tokyo][\xE6\x9D\xB1] prefix=none");
}

TEST(analyze_query_treats_a_trailing_symbol_or_invalid_byte_as_ending_the_word) {
    CHECK_EQ(query_of("foo\xE2\x80\x94"), "[foo] prefix=none");
    CHECK_EQ(query_of("foo\xC3"), "[foo] prefix=none");
    CHECK_EQ(query_of("foo\xE6\x9D"), "[foo] prefix=none");
}

TEST(analyze_query_probe_agrees_with_a_trailing_space) {
    const Analyzer analyzer = make_standard_analyzer("stopwords.txt");
    std::mt19937 rng(4242);
    int mismatches = 0;
    for (int i = 0; i < 20000; i++) {
        const std::string text = i % 2 == 0 ? random_text(rng) : random_unicode_text(rng);
        const AnalyzedQuery query = analyzer.analyze_query(text + " ");
        if (query.terms != analyzer.analyze(text) || query.prefix) {
            if (++mismatches <= 3) {
                std::cout << "    analyze_query mismatch: got " << join(query.terms) << " for '"
                          << text << " '\n";
            }
        }
    }
    CHECK_EQ(mismatches, 0);
}

TEST(tokenizer_appending_a_letter_extends_an_open_word_and_starts_a_new_one_otherwise) {
    StandardTokenizer tokenizer;
    CHECK_EQ(tokenizer.tokenize("searx").size(), tokenizer.tokenize("sear").size());
    CHECK_EQ(tokenizer.tokenize("john'x").size(), tokenizer.tokenize("john'").size());
    CHECK_EQ(tokenizer.tokenize("John" + CURLY + "x").size(), tokenizer.tokenize("John" + CURLY).size());
    CHECK_EQ(tokenizer.tokenize("sear x").size(), tokenizer.tokenize("sear ").size() + 1);
    CHECK_EQ(tokenizer.tokenize("foo 'x").size(), tokenizer.tokenize("foo '").size() + 1);
    CHECK_EQ(tokenizer.tokenize("caf\xC3\xA9x").size(), tokenizer.tokenize("caf\xC3\xA9").size());
    CHECK_EQ(tokenizer.tokenize("x").size(), tokenizer.tokenize("").size() + 1);
}

TEST(tokenizer_appending_a_letter_only_extends_the_last_token_on_random_text) {
    std::mt19937 rng(4242);
    int mismatches = 0;
    for (int i = 0; i < 20000; i++) {
        if (!appending_a_letter_is_consistent(random_text(rng))) {
            mismatches++;
        }
    }
    CHECK_EQ(mismatches, 0);
}

TEST(tokenizer_appending_a_letter_is_consistent_after_every_kind_of_ending) {
    const std::vector<std::string> endings = {
        "caf", "café", "Москва", "Zażółć", "cafe\xCC\x81", "東", "東京", "ひ", "カタ", "한",
        "\xF0\xA0\x80\x80", "—", "…", "¿", "×", "😀", "\xEF\xBF\xBD", "é'", "a" + CURLY, "東'", "東" + CURLY,
        "\xC3", "\xE6\x9D", "\xF0\x9F\x98", "\xE2\x80", "\xE2", "\x80", "\xFF", " ", "",
    };
    for (const std::string& ending : endings) {
        CHECK(appending_a_letter_is_consistent(ending));
        CHECK(appending_a_letter_is_consistent("foo " + ending));
    }
}

TEST(tokenizer_appending_a_letter_is_consistent_on_random_unicode_text) {
    std::mt19937 rng(4242);
    int mismatches = 0;
    for (int i = 0; i < 20000; i++) {
        if (!appending_a_letter_is_consistent(random_unicode_text(rng))) {
            mismatches++;
        }
    }
    CHECK_EQ(mismatches, 0);
}
