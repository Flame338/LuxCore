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
const std::string CURLY = "\xE2\x80\x99";
const std::string CJK_RI = "\xE6\x97\xA5";

using Words = std::vector<std::string>;

Words split(const std::string& text, IdentifierOptions options = {}) {
    return IdentifierTokenizer(options).tokenize(text);
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
