#include "TestFramework.h"

#include "Analyzer.h"
#include "IdentifierTokenizer.h"
#include "InvertedIndex.h"
#include "Searcher.h"

#include <algorithm>
#include <unordered_map>

namespace {

const std::string U_UMLAUT = "\xC3\x9C";
const std::string E_ACUTE = "\xC3\x89";
const std::string A_RING = "\xC3\x85";
const std::string SHARP_S = "\xC3\x9F";
const std::string CYR_DE = "\xD0\x94";
const std::string CYR_o = "\xD0\xBE";
const std::string CYR_m = "\xD0\xBC";
const std::string GREEK_ALPHA = "\xCE\x91";
const std::string CURLY = "\xE2\x80\x99";
const std::string CJK_RI = "\xE6\x97\xA5";

using Words = std::vector<std::string>;

Words split(const std::string& text, IdentifierOptions options = {}) {
    return IdentifierTokenizer(options).tokenize(text);
}

Words analyze(const std::string& text, IdentifierOptions options = {}) {
    return make_identifier_analyzer(options).analyze(text);
}

struct CodeCorpus {
    Analyzer analyzer;
    InvertedIndex index;
    int next_id = 0;

    explicit CodeCorpus(Analyzer analyzer) : analyzer(std::move(analyzer)) {}

    int add(const std::string& text) {
        TermFreqs term_freqs;
        for (const std::string& token : analyzer.analyze(text)) {
            term_freqs[token]++;
        }
        const int id = next_id++;
        index.add_document(id, FieldTermFreqs{{"body", term_freqs}});
        return id;
    }

    std::vector<int> search(const std::string& query) const {
        std::vector<int> ids;
        for (const SearchHit& hit : Searcher(index, analyzer).search(query)) {
            ids.push_back(hit.doc_id);
        }
        std::ranges::sort(ids);
        return ids;
    }
};

}

TEST(identifier_camel_case_splits_at_lower_to_upper) {
    CHECK_EQ(split("getUserName"), (Words{"get", "User", "Name"}));
    CHECK_EQ(split("userName"), (Words{"user", "Name"}));
    CHECK_EQ(split("a"), (Words{"a"}));
    CHECK_EQ(split("aB"), (Words{"a", "B"}));
}

TEST(identifier_pascal_case_splits_at_lower_to_upper) {
    CHECK_EQ(split("GetUserName"), (Words{"Get", "User", "Name"}));
    CHECK_EQ(split("User"), (Words{"User"}));
}

TEST(identifier_acronyms_stay_together_until_a_lowercase_follows) {
    CHECK_EQ(split("HTTPServer"), (Words{"HTTP", "Server"}));
    CHECK_EQ(split("parseXMLDocument"), (Words{"parse", "XML", "Document"}));
    CHECK_EQ(split("getURL"), (Words{"get", "URL"}));
    CHECK_EQ(split("URL"), (Words{"URL"}));
    CHECK_EQ(split("IOError"), (Words{"IO", "Error"}));
}

TEST(identifier_snake_case_splits_on_underscores) {
    CHECK_EQ(split("get_user_name"), (Words{"get", "user", "name"}));
    CHECK_EQ(split("__init__"), (Words{"init"}));
    CHECK_EQ(split("MAX_BUFFER_SIZE"), (Words{"MAX", "BUFFER", "SIZE"}));
}

TEST(identifier_kebab_case_splits_on_hyphens) {
    CHECK_EQ(split("get-user-name"), (Words{"get", "user", "name"}));
    CHECK_EQ(split("my-Component"), (Words{"my", "Component"}));
}

TEST(identifier_paths_split_on_slashes_backslashes_and_dots) {
    CHECK_EQ(split("path/to/file.ext"), (Words{"path", "to", "file", "ext"}));
    CHECK_EQ(split("src\\core\\IndexWriter.cpp"), (Words{"src", "core", "Index", "Writer", "cpp"}));
    CHECK_EQ(split("include/TokenFilter.h"), (Words{"include", "Token", "Filter", "h"}));
}

TEST(identifier_mixed_styles_in_one_text) {
    CHECK_EQ(split("open src/userName.ts, get_UserId"),
             (Words{"open", "src", "user", "Name", "ts", "get", "User", "Id"}));
}

TEST(identifier_digits_stay_attached_by_default) {
    CHECK_EQ(split("utf8"), (Words{"utf8"}));
    CHECK_EQ(split("sha256sum"), (Words{"sha256sum"}));
    CHECK_EQ(split("h2o"), (Words{"h2o"}));
    CHECK_EQ(split("H2O"), (Words{"H2O"}));
    CHECK_EQ(split("3D"), (Words{"3D"}));
}

TEST(identifier_digit_before_capitalized_word_splits_without_the_digit_option) {
    CHECK_EQ(split("Base64Encode"), (Words{"Base64", "Encode"}));
    CHECK_EQ(split("ISO8601Date"), (Words{"ISO8601", "Date"}));
}

TEST(identifier_digit_option_splits_at_letter_digit_boundaries) {
    const IdentifierOptions digits{.split_letters_digits = true};
    CHECK_EQ(split("utf8", digits), (Words{"utf", "8"}));
    CHECK_EQ(split("8bit", digits), (Words{"8", "bit"}));
    CHECK_EQ(split("sha256sum", digits), (Words{"sha", "256", "sum"}));
    CHECK_EQ(split("Base64Encode", digits), (Words{"Base", "64", "Encode"}));
    CHECK_EQ(split("utf8_to_utf16", digits), (Words{"utf", "8", "to", "utf", "16"}));
    CHECK_EQ(split("H2O", digits), (Words{"H", "2", "O"}));
    CHECK_EQ(split("12345", digits), (Words{"12345"}));
}

TEST(identifier_keep_original_emits_the_whole_token_before_its_words) {
    const IdentifierOptions keep{.keep_original = true};
    CHECK_EQ(split("getUserName", keep), (Words{"getUserName", "get", "User", "Name"}));
    CHECK_EQ(split("HTTPServer", keep), (Words{"HTTPServer", "HTTP", "Server"}));
}

TEST(identifier_keep_original_does_not_duplicate_unsplit_tokens) {
    const IdentifierOptions keep{.keep_original = true};
    CHECK_EQ(split("user", keep), (Words{"user"}));
    CHECK_EQ(split("get_user", keep), (Words{"get", "user"}));
    CHECK_EQ(split("one getTwo three", keep), (Words{"one", "getTwo", "get", "Two", "three"}));
}

TEST(identifier_keep_original_combines_with_digit_option) {
    const IdentifierOptions both{.keep_original = true, .split_letters_digits = true};
    CHECK_EQ(split("utf8", both), (Words{"utf8", "utf", "8"}));
    CHECK_EQ(split("getUtf8Name", both), (Words{"getUtf8Name", "get", "Utf", "8", "Name"}));
}

TEST(identifier_analyzer_indexes_lowercased_words_and_original) {
    CHECK_EQ(analyze("getUserName"), (Words{"get", "user", "name"}));
    CHECK_EQ(analyze("getUserName", {.keep_original = true}),
             (Words{"getusername", "get", "user", "name"}));
    CHECK_EQ(analyze("utf8", {.split_letters_digits = true}), (Words{"utf", "8"}));
}

TEST(identifier_non_ascii_upper_after_lower_splits) {
    CHECK_EQ(split("get" + U_UMLAUT + "berName"), (Words{"get", U_UMLAUT + "ber", "Name"}));
    CHECK_EQ(split(E_ACUTE + "coleName"), (Words{E_ACUTE + "cole", "Name"}));
    CHECK_EQ(split("caf\xC3\xA9Latte"), (Words{"caf\xC3\xA9", "Latte"}));
    CHECK_EQ(split("stra" + SHARP_S + "eName"), (Words{"stra" + SHARP_S + "e", "Name"}));
}

TEST(identifier_non_ascii_upper_runs_keep_acronym_rule) {
    CHECK_EQ(split(A_RING + E_ACUTE + "Server"), (Words{A_RING + E_ACUTE, "Server"}));
    CHECK_EQ(split(A_RING + E_ACUTE), (Words{A_RING + E_ACUTE}));
}

TEST(identifier_non_ascii_lower_to_upper_in_cyrillic_and_greek) {
    CHECK_EQ(split(CYR_o + CYR_m + CYR_DE + CYR_o + CYR_m),
             (Words{CYR_o + CYR_m, CYR_DE + CYR_o + CYR_m}));
    CHECK_EQ(split("\xCE\xB1" + GREEK_ALPHA + "\xCE\xB2"), (Words{"\xCE\xB1", GREEK_ALPHA + "\xCE\xB2"}));
}

TEST(identifier_non_ascii_analyzer_folds_after_splitting) {
    CHECK_EQ(analyze("get" + U_UMLAUT + "berName"), (Words{"get", "uber", "name"}));
    CHECK_EQ(analyze(E_ACUTE + "coleName"), (Words{"ecole", "name"}));
    CHECK_EQ(analyze(E_ACUTE + "coleName", {.keep_original = true}),
             (Words{"ecolename", "ecole", "name"}));
}

TEST(identifier_non_ascii_digit_option_uses_non_ascii_digits) {
    const IdentifierOptions digits{.split_letters_digits = true};
    CHECK_EQ(split("abc\xD9\xA1\xD9\xA2", digits), (Words{"abc", "\xD9\xA1\xD9\xA2"}));
}

TEST(identifier_caseless_scripts_never_split) {
    CHECK_EQ(split(CJK_RI + "Name"), (Words{CJK_RI, "Name"}));
    CHECK_EQ(split("name" + CJK_RI), (Words{"name", CJK_RI}));
}

TEST(identifier_apostrophes_never_split_a_word) {
    CHECK_EQ(split("O'Neil"), (Words{"O'Neil"}));
    CHECK_EQ(split("don't"), (Words{"don't"}));
    CHECK_EQ(split("getUser's"), (Words{"get", "User's"}));
    CHECK_EQ(split("O" + CURLY + "Neil"), (Words{"O" + CURLY + "Neil"}));
}

TEST(identifier_plain_text_and_edge_inputs) {
    CHECK_EQ(split(""), Words{});
    CHECK_EQ(split("  ___  "), Words{});
    CHECK_EQ(split("hello world"), (Words{"hello", "world"}));
    CHECK_EQ(split("Hello World"), (Words{"Hello", "World"}));
    CHECK_EQ(split("\xFF" "bad"), (Words{"bad"}));
}

TEST(identifier_standard_analyzer_is_unchanged) {
    Analyzer standard = make_standard_analyzer();
    CHECK_EQ(standard.analyze("getUserName"), (Words{"getusername"}));
    CHECK_EQ(standard.analyze("get_user_name"), (Words{"get", "user", "name"}));
}

TEST(identifier_search_user_finds_camel_case_document) {
    CodeCorpus standard(make_standard_analyzer());
    standard.add("function getUserName()");
    CHECK_EQ(standard.search("user"), std::vector<int>{});

    CodeCorpus code(make_identifier_analyzer());
    const int camel = code.add("function getUserName()");
    const int snake = code.add("def get_user_name():");
    const int pascal = code.add("class UserNameProvider");
    const int kebab = code.add("<user-name-badge>");
    const int path = code.add("src/user/profile.cpp");
    const int unrelated = code.add("function parseDocument()");

    const std::vector<int> hits = code.search("user");
    CHECK_EQ(hits, (std::vector<int>{camel, snake, pascal, kebab, path}));
    CHECK(std::ranges::find(hits, unrelated) == hits.end());
}

TEST(identifier_search_matches_across_styles_and_case) {
    CodeCorpus code(make_identifier_analyzer());
    const int camel = code.add("getUserName");
    const int snake = code.add("get_user_name");
    code.add("setPassword");

    CHECK_EQ(code.search("USER_NAME"), (std::vector<int>{camel, snake}));
    CHECK_EQ(code.search("userName"), (std::vector<int>{camel, snake}));
    CHECK_EQ(code.search("get-user"), (std::vector<int>{camel, snake}));
}

TEST(identifier_search_prefix_completes_the_last_word) {
    CodeCorpus code(make_identifier_analyzer());
    const int doc = code.add("getUserName");
    code.add("getPassword");

    CHECK_EQ(code.search("getUse"), (std::vector<int>{0, 1}));
    CHECK_EQ(code.search("get Pas"), (std::vector<int>{0, 1}));
    CHECK_EQ(code.search("userN"), (std::vector<int>{doc}));
}

TEST(identifier_search_with_keep_original_matches_whole_identifier_too) {
    CodeCorpus code(make_identifier_analyzer({.keep_original = true}));
    const int target = code.add("getUserName");
    const int other = code.add("get something else");

    const std::vector<SearchHit> hits = Searcher(code.index, code.analyzer).search("getUserName");
    CHECK_EQ(hits.size(), std::size_t{2});
    CHECK_EQ(hits.front().doc_id, target);
    CHECK_EQ(hits.back().doc_id, other);

    CHECK_EQ(code.index.get_document_frequency("getusername") > 0, true);
    CHECK_EQ(code.index.get_document_frequency("user") > 0, true);
}

TEST(identifier_search_non_ascii_identifier_by_folded_word) {
    CodeCorpus code(make_identifier_analyzer());
    const int doc = code.add("get" + U_UMLAUT + "berName");
    const int school = code.add(E_ACUTE + "coleName");

    CHECK_EQ(code.search("uber"), (std::vector<int>{doc}));
    CHECK_EQ(code.search("ecole"), (std::vector<int>{school}));
    CHECK_EQ(code.search("name"), (std::vector<int>{doc, school}));
}

TEST(identifier_search_digit_option_finds_number_part) {
    CodeCorpus plain(make_identifier_analyzer());
    plain.add("encode utf8 text");
    CHECK_EQ(plain.search("utf"), std::vector<int>{0});

    CodeCorpus split_digits(make_identifier_analyzer({.split_letters_digits = true}));
    const int doc = split_digits.add("encode utf8 text");
    CHECK_EQ(split_digits.search("8"), (std::vector<int>{doc}));
    CHECK_EQ(split_digits.search("utf8"), (std::vector<int>{doc}));
}
