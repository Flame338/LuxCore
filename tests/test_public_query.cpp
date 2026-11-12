#include "TestFramework.h"

#include "LuxCore.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

struct TempFile {
    std::filesystem::path path;

    explicit TempFile(const std::string& name)
        : path(std::filesystem::temp_directory_path() / ("luxcore_public_query_" + name + ".lux")) {
        std::filesystem::remove(path);
    }

    ~TempFile() {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }

    std::string str() const { return path.string(); }
};

std::string sorted_keys(const std::vector<luxcore::Hit>& hits) {
    std::vector<std::string> keys;
    for (const luxcore::Hit& hit : hits) {
        keys.push_back(hit.key);
    }
    std::sort(keys.begin(), keys.end());
    std::string text;
    for (const std::string& key : keys) {
        text += key + ",";
    }
    return text;
}

void fill_garden(luxcore::Index& index) {
    index.add("a", "Garden notes", "tomato plants need water and sun");
    index.add("b", "Kitchen", "tomato soup recipe with basil");
    index.add("c", "Garden tools", "spade rake and a hose");
}

luxcore::Index garden() {
    luxcore::Index index;
    fill_garden(index);
    return index;
}

luxcore::Index phrases(bool positions) {
    luxcore::Index index(luxcore::IndexConfig{.positions = positions});
    index.add("fox", "Animals", "the quick brown fox jumps over the lazy dog");
    index.add("dog", "Pets", "a lazy brown dog sleeps");
    return index;
}

luxcore::Index reading() {
    luxcore::Index index;
    index.add("reading", "Reading list", "a book about how Lucene ranks search results");
    return index;
}

struct QueryFailure {
    bool thrown = false;
    bool is_invalid_argument = false;
    std::size_t offset = 0;
    std::string message;
};

template <typename Body>
QueryFailure query_failure(Body body) {
    QueryFailure failure;
    try {
        body();
    } catch (const std::invalid_argument& error) {
        const auto* query_error = dynamic_cast<const luxcore::QueryError*>(&error);
        failure.thrown = query_error != nullptr;
        failure.is_invalid_argument = true;
        failure.offset = query_error != nullptr ? query_error->offset() : 0;
        failure.message = error.what();
    }
    return failure;
}

std::string long_body() {
    std::string body;
    for (int i = 0; i < 30; i++) {
        body += "filler words fill space ";
    }
    return body + "needle appears here " + body;
}

}

TEST(public_query_scopes_a_term_to_a_field) {
    const luxcore::Index index = garden();

    CHECK_EQ(sorted_keys(index.search_query("title:garden")), "a,c,");
    CHECK_EQ(sorted_keys(index.search_query("title:tomato")), "");
    CHECK_EQ(sorted_keys(index.search_query("body:tomato")), "a,b,");
}

TEST(public_query_supports_required_and_excluded_clauses) {
    const luxcore::Index index = garden();

    CHECK_EQ(sorted_keys(index.search_query("+tomato -soup")), "a,");
    CHECK_EQ(sorted_keys(index.search_query("tomato NOT soup")), "a,");
    CHECK_EQ(sorted_keys(index.search_query("+tomato -basil")), "a,");
}

TEST(public_query_supports_or) {
    const luxcore::Index index = garden();

    CHECK_EQ(sorted_keys(index.search_query("spade OR basil")), "b,c,");
    CHECK_EQ(sorted_keys(index.search_query("spade basil")), "b,c,");
}

TEST(public_query_and_binds_tighter_than_or) {
    const luxcore::Index index = garden();

    CHECK_EQ(sorted_keys(index.search_query("tomato AND garden OR spade")), "a,c,");
    CHECK_EQ(sorted_keys(index.search_query("tomato AND (garden OR spade)")), "a,");
}

TEST(public_query_blank_text_returns_no_hits) {
    const luxcore::Index index = garden();

    CHECK(index.search_query("").empty());
    CHECK(index.search_query("   ").empty());
    CHECK(luxcore::Index().search_query("tomato").empty());
}

TEST(public_query_does_not_match_prefixes_or_typos) {
    const luxcore::Index index = garden();

    CHECK_EQ(sorted_keys(index.search("tomat")), "a,b,");
    CHECK_EQ(sorted_keys(index.search_query("tomat")), "");
    CHECK_EQ(sorted_keys(index.search_query("tomatoe")), "");
}

TEST(public_query_reports_a_parse_error_with_its_offset) {
    const luxcore::Index index = garden();

    const QueryFailure open_paren = query_failure([&] { index.search_query("tomato (garden"); });
    CHECK(open_paren.thrown);
    CHECK(open_paren.is_invalid_argument);
    CHECK_EQ(open_paren.offset, static_cast<std::size_t>(7));
    CHECK(!open_paren.message.empty());

    const QueryFailure quote = query_failure([&] { index.search_query("tomato \"garden"); });
    CHECK(quote.thrown);
    CHECK_EQ(quote.offset, static_cast<std::size_t>(7));

    const QueryFailure field = query_failure([&] { index.search_query("title:"); });
    CHECK(field.thrown);
}

TEST(public_query_single_words_work_without_positions) {
    const luxcore::Index index = phrases(false);

    CHECK_EQ(sorted_keys(index.search_query("quick")), "fox,");
    CHECK_EQ(sorted_keys(index.search_query("brown -quick")), "dog,");
}

TEST(public_query_hyphenated_words_are_phrases_when_positions_are_on) {
    luxcore::Index index(luxcore::IndexConfig{.positions = true});
    index.add("net", "Network", "connect to the wi-fi router");
    index.add("other", "Other", "fi comes before wi here: fi wi");

    CHECK_EQ(sorted_keys(index.search_query("wi-fi")), "net,");
}

TEST(public_query_positions_survive_save_and_load_when_load_asks_for_them) {
    const TempFile file("positions");
    phrases(true).save(file.str());

    const luxcore::Index with_positions = luxcore::Index::load(file.str(), luxcore::IndexConfig{.positions = true});
    CHECK_EQ(sorted_keys(with_positions.search_query("\"quick brown\"")), "fox,");
    CHECK_EQ(sorted_keys(with_positions.search_query("\"brown quick\"")), "");

    const luxcore::Index without_positions = luxcore::Index::load(file.str());
    CHECK(query_failure([&] { without_positions.search_query("\"quick brown\""); }).thrown);
    CHECK_EQ(sorted_keys(without_positions.search_query("quick")), "fox,");
}

TEST(public_query_a_loaded_index_keeps_working_after_changes) {
    const TempFile file("changes");
    phrases(true).save(file.str());

    luxcore::Index loaded = luxcore::Index::load(file.str(), luxcore::IndexConfig{.positions = true});
    loaded.add("cat", "Cats", "the quick brown cat");

    CHECK_EQ(sorted_keys(loaded.search_query("\"quick brown\"")), "cat,fox,");
}

TEST(public_snippet_marks_the_typo_tolerant_match_that_search_found) {
    const luxcore::Index index = reading();

    CHECK_EQ(index.search("lucne ranks").size(), static_cast<std::size_t>(1));
    CHECK_EQ(index.snippet("reading", "body", "lucne ranks").value_or("-"),
             "a book about how <em>Lucene</em> <em>ranks</em> search results");
}

TEST(public_snippet_query_counts_only_terms_scoped_to_the_field) {
    const luxcore::Index index = reading();

    CHECK_EQ(index.snippet_query("reading", "title", "title:list").value_or("-"), "Reading <em>list</em>");
    CHECK_EQ(index.snippet_query("reading", "body", "title:book").value_or("-"),
             "a book about how Lucene ranks search results");
    CHECK_EQ(index.snippet_query("reading", "body", "body:book").value_or("-"),
             "a <em>book</em> about how Lucene ranks search results");
}

TEST(public_snippet_query_marks_a_phrase_when_positions_are_on) {
    luxcore::Index index(luxcore::IndexConfig{.positions = true});
    index.add("fox", "Animals", "the quick brown fox jumps");

    CHECK_EQ(index.snippet_query("fox", "body", "\"quick brown\"").value_or("-"),
             "the <em>quick</em> <em>brown</em> fox jumps");
}

TEST(public_snippet_query_phrase_without_positions_is_a_query_error) {
    const luxcore::Index index = phrases(false);

    const QueryFailure phrase = query_failure([&] { index.snippet_query("fox", "body", "\"quick brown\""); });
    CHECK(phrase.thrown);
    CHECK_EQ(phrase.offset, static_cast<std::size_t>(0));

    const QueryFailure broken = query_failure([&] { index.snippet_query("fox", "body", "(quick"); });
    CHECK(broken.thrown);
    CHECK_EQ(broken.offset, static_cast<std::size_t>(0));
}

TEST(public_snippet_is_empty_for_an_unknown_key_or_field) {
    const luxcore::Index index = reading();

    CHECK(!index.snippet("missing", "body", "lucene").has_value());
    CHECK(!index.snippet("reading", "author", "lucene").has_value());
    CHECK(!index.snippet_query("missing", "body", "lucene").has_value());
    CHECK(!index.snippet_query("reading", "author", "lucene").has_value());
}

TEST(public_snippet_uses_custom_markers) {
    const luxcore::Index index = reading();
    luxcore::SnippetOptions options;
    options.start_marker = "[";
    options.end_marker = "]";

    CHECK_EQ(index.snippet("reading", "body", "lucene", options).value_or("-"),
             "a book about how [Lucene] ranks search results");
    CHECK_EQ(index.snippet_query("reading", "body", "lucene", options).value_or("-"),
             "a book about how [Lucene] ranks search results");
}

TEST(public_snippet_cuts_a_long_text_around_the_match_with_the_ellipsis) {
    luxcore::Index index;
    index.add("long", "Long", long_body());

    luxcore::SnippetOptions options;
    options.length = 60;
    const std::string cut = index.snippet_query("long", "body", "needle", options).value_or("-");

    CHECK(cut.find("<em>needle</em>") != std::string::npos);
    CHECK(cut.size() < 160);
    CHECK(cut.rfind("...", 0) == 0);
    CHECK(cut.size() >= 3 && cut.compare(cut.size() - 3, 3, "...") == 0);

    options.ellipsis = "~";
    const std::string tilde = index.snippet_query("long", "body", "needle", options).value_or("-");
    CHECK(tilde.rfind("~", 0) == 0);
    CHECK(tilde.find("...") == std::string::npos);

    options.ellipsis = "";
    const std::string bare = index.snippet_query("long", "body", "needle", options).value_or("-");
    CHECK(bare.find("<em>needle</em>") != std::string::npos);
    CHECK(bare.find("...") == std::string::npos);
    CHECK(bare.rfind("~", 0) != 0);
}

TEST(public_query_methods_are_safe_beside_a_writer) {
    luxcore::Index index = garden();
    std::atomic<bool> done{false};
    std::atomic<int> bad{0};

    std::vector<std::thread> readers;
    for (int i = 0; i < 3; i++) {
        readers.emplace_back([&] {
            while (!done.load()) {
                const std::vector<luxcore::Hit> hits = index.search_query("tomato OR garden");
                if (hits.size() < 2) {
                    bad++;
                }
                const auto text = index.snippet_query("a", "body", "tomato");
                if (!text || text->find("<em>tomato</em>") == std::string::npos) {
                    bad++;
                }
                if (!index.snippet("b", "body", "tomatoe")) {
                    bad++;
                }
            }
        });
    }

    for (int i = 0; i < 200; i++) {
        index.add("extra" + std::to_string(i), "Extra", "tomato garden filler " + std::to_string(i));
    }
    done = true;
    for (std::thread& reader : readers) {
        reader.join();
    }

    CHECK_EQ(bad.load(), 0);
    CHECK_EQ(index.size(), static_cast<std::size_t>(203));
}
