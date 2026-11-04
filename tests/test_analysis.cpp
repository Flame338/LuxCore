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
    static const std::string alphabet = "abcXYZ019 \t\n.,!?'''-_/()[]{}sS";

    std::string text;
    const int length = rng() % 40;
    for (int i = 0; i < length; i++) {
        switch (rng() % 10) {
            case 0: text += CURLY; break;
            case 1: text += CURLY.substr(0, 1 + rng() % 2); break;
            case 2: text += static_cast<char>(128 + rng() % 128); break;
            case 3: text += (rng() % 2 ? "the" : "And"); break;
            default: text += alphabet[rng() % alphabet.size()]; break;
        }
    }
    return text;
}

std::string tokens_of(const std::string& text) {
    return join(StandardTokenizer().tokenize(text));
}

std::string analyzed(const std::string& text) {
    static const Analyzer analyzer = make_standard_analyzer("stopwords.txt");
    return join(analyzer.analyze(text));
}

}

TEST(tokenizer_splits_on_non_alphanumerics_and_keeps_case) {
    CHECK_EQ(tokens_of("Hello, World! 42x"), "[Hello][World][42x]");
    CHECK_EQ(tokens_of("a-b c_d"), "[a][b][c][d]");
    CHECK_EQ(tokens_of(""), "");
    CHECK_EQ(tokens_of("  ,.  "), "");
}

TEST(tokenizer_treats_non_ascii_bytes_as_delimiters) {
    CHECK_EQ(tokens_of("caf\xC3\xA9 au lait"), "[caf][au][lait]");
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
