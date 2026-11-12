#include "TestFramework.h"

#include "Analyzer.h"
#include "Utf8.h"

#include <algorithm>
#include <filesystem>
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

std::string folded(const std::string& token) {
    std::vector<std::string> tokens{token};
    UnicodeFoldingFilter().apply(tokens);
    return join(tokens);
}

std::string with_stop_words_file(const std::string& contents, const std::string& text) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "luxcore_unicode_stop_words.txt";
    std::ofstream(path) << contents;
    const std::string result = join(make_standard_analyzer(path.string()).analyze(text));
    std::filesystem::remove(path);
    return result;
}

std::string query_of(const std::string& text) {
    static const Analyzer analyzer = make_standard_analyzer("stopwords.txt");
    const AnalyzedQuery query = analyzer.analyze_query(text);
    return join(query.terms) + (query.prefix ? " prefix=" + *query.prefix : " prefix=none");
}

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

TEST(folding_leaves_the_multiplication_and_division_signs_alone) {
    CHECK_EQ(folded("a\xC3\x97" "b"), "[a\xC3\x97" "b]");
    CHECK_EQ(folded("a\xC3\xB7" "b"), "[a\xC3\xB7" "b]");
}

TEST(folding_is_idempotent_on_random_tokens) {
    std::mt19937 rng(4242);
    int mismatches = 0;
    for (int i = 0; i < 20000; i++) {
        std::vector<std::string> once = StandardTokenizer().tokenize(random_unicode_text(rng));
        UnicodeFoldingFilter().apply(once);
        std::vector<std::string> twice = once;
        UnicodeFoldingFilter().apply(twice);
        if (once != twice) {
            mismatches++;
        }
    }
    CHECK_EQ(mismatches, 0);
}

TEST(analyzer_stop_words_from_a_file_match_folded_tokens) {
    CHECK_EQ(with_stop_words_file("Été\nдля\nτο\n", "été ETE Été summer для ДЛЯ το ΤΟ"), "[summer]");
    CHECK_EQ(with_stop_words_file("e\u0301te\u0301\n", "été ete summer"), "[summer]");
    CHECK_EQ(with_stop_words_file("café\nSTRASSE\n", "Café CAFE straße x"), "[x]");
    CHECK_EQ(with_stop_words_file("\u0301\n", "a"), "[a]");
}

TEST(analyzer_with_a_missing_stop_word_file_keeps_every_word) {
    const Analyzer analyzer = make_standard_analyzer("no-such-file.txt");
    CHECK_EQ(join(analyzer.analyze("The Cat")), "[the][cat]");
}

TEST(analyze_query_handles_apostrophes_at_the_end_of_the_query) {
    CHECK_EQ(query_of("john'"), " prefix=john");
    CHECK_EQ(query_of("John" + CURLY), " prefix=john");
    CHECK_EQ(query_of("John" + CURLY + "s"), " prefix=john");
    CHECK_EQ(query_of("it's"), " prefix=it");
    CHECK_EQ(query_of("don't"), " prefix=don't");
    CHECK_EQ(query_of("foo '"), "[foo] prefix=none");
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
