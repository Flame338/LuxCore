#include "TestFramework.h"

#include "LuxCore.h"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

struct TempFile {
    std::filesystem::path path;

    explicit TempFile(const std::string& name)
        : path(std::filesystem::temp_directory_path() / ("luxcore_prepare_" + name + ".lux")) {
        std::filesystem::remove(path);
    }

    ~TempFile() {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }

    std::string str() const { return path.string(); }
};

std::string read_all(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string saved_bytes(const luxcore::Index& index, const std::string& name) {
    const TempFile file(name);
    index.save(file.str());
    return read_all(file.path);
}

struct Doc {
    std::string key;
    std::string title;
    std::string body;
};

std::vector<Doc> make_docs(int count) {
    std::mt19937 rng(29);
    const std::vector<std::string> words = {
        "lucene", "ranks",  "search", "results", "book",   "train", "edinburgh", "raincoat", "milk",  "eggs",
        "bread",  "butter", "quick",  "brown",   "fox",    "jumps", "lazy",      "dog",      "rust",  "vector",
        "index",  "Café",   "naïve",  "日本",    "token",  "it's",  "John's",    "parse",    "query", "fuzzy"};
    std::vector<Doc> docs;
    for (int i = 0; i < count; i++) {
        Doc doc;
        // Every fifth key repeats an earlier one, so commits also exercise replacement.
        doc.key = "doc-" + std::to_string(i % 5 == 4 ? i / 3 : i);
        for (int w = 0; w < 3; w++) {
            doc.title += (w ? " " : "") + words[rng() % words.size()];
        }
        const int length = 10 + static_cast<int>(rng() % 120);
        for (int w = 0; w < length; w++) {
            doc.body += (w ? (rng() % 7 == 0 ? ", " : " ") : "") + words[rng() % words.size()];
        }
        docs.push_back(doc);
    }
    return docs;
}

const std::vector<std::string> fuzzy_queries = {"lucne ranks", "book", "brwn fx", "edinb", "cafe", "日本", "johns",
                                                "it's", "milk eggs bread", "zzz nothing", "", "rust vecto"};

const std::vector<std::string> syntax_queries = {"lucene",         "+book OR ranks", "title:book",  "body:fox -dog",
                                                 "quick AND brown", "naïve",          "edinburgh*", "query OR fuzzy"};

std::vector<luxcore::SnippetOptions> snippet_option_sets() {
    luxcore::SnippetOptions narrow;
    narrow.length = 30;
    narrow.start_marker = "[";
    narrow.end_marker = "]";
    narrow.ellipsis = "~";
    return {luxcore::SnippetOptions{}, narrow};
}

}

TEST(prepared_commits_in_serial_order_save_the_same_bytes_as_serial_adds) {
    const std::vector<Doc> docs = make_docs(600);
    for (const bool positions : {false, true}) {
        luxcore::Index serial(luxcore::IndexConfig{.positions = positions});
        for (const Doc& doc : docs) {
            serial.add(doc.key, doc.title, doc.body);
        }
        serial.remove("doc-7");

        luxcore::Index prepared_index(luxcore::IndexConfig{.positions = positions});
        std::vector<std::optional<luxcore::Prepared>> prepared(docs.size());
        std::vector<std::thread> threads;
        constexpr std::size_t thread_count = 8;
        for (std::size_t t = 0; t < thread_count; t++) {
            threads.emplace_back([&, t] {
                for (std::size_t i = t; i < docs.size(); i += thread_count) {
                    prepared[i].emplace(prepared_index.prepare(docs[i].title, docs[i].body));
                }
            });
        }
        for (std::thread& thread : threads) {
            thread.join();
        }
        for (std::size_t i = 0; i < docs.size(); i++) {
            prepared_index.commit(docs[i].key, std::move(*prepared[i]));
        }
        prepared_index.remove("doc-7");

        CHECK(saved_bytes(serial, "serial") == saved_bytes(prepared_index, "prepared"));
    }
}

TEST(an_unstored_body_is_searched_and_scored_like_a_stored_one) {
    const std::vector<Doc> docs = make_docs(300);
    luxcore::Index stored;
    luxcore::Index unstored;
    for (const Doc& doc : docs) {
        stored.add(doc.key, doc.title, doc.body);
        unstored.commit(doc.key, unstored.prepare(doc.title, doc.body, {.store_body = false}));
    }
    for (const std::string& query : fuzzy_queries) {
        const std::vector<luxcore::Hit> left = stored.search(query, 40);
        const std::vector<luxcore::Hit> right = unstored.search(query, 40);
        CHECK_EQ(left.size(), right.size());
        for (std::size_t i = 0; i < left.size() && i < right.size(); i++) {
            CHECK_EQ(left[i].key, right[i].key);
            CHECK(left[i].score == right[i].score);
        }
    }
    for (const std::string& query : syntax_queries) {
        const std::vector<luxcore::Hit> left = stored.search_query(query, 40);
        const std::vector<luxcore::Hit> right = unstored.search_query(query, 40);
        CHECK_EQ(left.size(), right.size());
        for (std::size_t i = 0; i < left.size() && i < right.size(); i++) {
            CHECK_EQ(left[i].key, right[i].key);
            CHECK(left[i].score == right[i].score);
        }
    }
}

TEST(an_unstored_body_keeps_its_title_and_has_no_stored_body) {
    luxcore::Index index;
    index.commit("trip", index.prepare("Trip planning", "book the train to Edinburgh", {.store_body = false}));
    CHECK_EQ(index.stored("trip", "title"), std::optional<std::string>("Trip planning"));
    CHECK(!index.stored("trip", "body"));
    CHECK(!index.snippet("trip", "body", "train"));
    CHECK_EQ(index.search("edinburgh").size(), static_cast<std::size_t>(1));

    index.add("trip", "Trip planning", "stored again");
    CHECK_EQ(index.stored("trip", "body"), std::optional<std::string>("stored again"));
    index.commit("trip", index.prepare("Trip planning", "unstored again", {.store_body = false}));
    CHECK(!index.stored("trip", "body"));
    CHECK(index.search("edinburgh").empty());
    CHECK_EQ(index.search("unstored").size(), static_cast<std::size_t>(1));
}

TEST(snippet_text_matches_snippet_of_the_stored_body) {
    const std::vector<Doc> docs = make_docs(200);
    luxcore::Index index;
    for (const Doc& doc : docs) {
        index.add(doc.key, doc.title, doc.body);
    }
    int compared = 0;
    for (const luxcore::SnippetOptions& options : snippet_option_sets()) {
        for (std::size_t i = 0; i < docs.size(); i += 13) {
            const std::string body = *index.stored(docs[i].key, "body");
            for (const std::string& query : fuzzy_queries) {
                CHECK_EQ(index.snippet_text(body, query, options), *index.snippet(docs[i].key, "body", query, options));
                compared++;
            }
            for (const std::string& query : syntax_queries) {
                CHECK_EQ(index.snippet_text_query(body, query, options),
                         *index.snippet_query(docs[i].key, "body", query, options));
                compared++;
            }
        }
    }
    CHECK(compared > 500);
}

TEST(snippet_text_query_reports_a_bad_query_like_snippet_query) {
    luxcore::Index index;
    index.add("a", "title", "some body text");
    for (const std::string& query : {std::string("(unclosed"), std::string("a AND"), std::string("\"phrase words\"")}) {
        std::optional<std::size_t> stored_offset;
        std::optional<std::size_t> text_offset;
        try {
            index.snippet_query("a", "body", query);
        } catch (const luxcore::QueryError& error) {
            stored_offset = error.offset();
        }
        try {
            index.snippet_text_query("some body text", query);
        } catch (const luxcore::QueryError& error) {
            text_offset = error.offset();
        }
        CHECK(stored_offset.has_value());
        CHECK_EQ(stored_offset, text_offset);
    }
}

TEST(a_loaded_index_with_stored_bodies_keeps_working) {
    const std::vector<Doc> docs = make_docs(120);
    luxcore::Index original;
    for (const Doc& doc : docs) {
        original.add(doc.key, doc.title, doc.body);
    }
    const TempFile file("stored_bodies");
    original.save(file.str());
    luxcore::Index loaded = luxcore::Index::load(file.str());

    for (std::size_t i = 0; i < docs.size(); i += 11) {
        const std::optional<std::string> body = loaded.stored(docs[i].key, "body");
        CHECK(body.has_value());
        for (const std::string& query : fuzzy_queries) {
            CHECK_EQ(loaded.snippet_text(*body, query), *original.snippet(docs[i].key, "body", query));
            CHECK_EQ(*loaded.snippet(docs[i].key, "body", query), *original.snippet(docs[i].key, "body", query));
        }
    }
    loaded.commit("fresh", loaded.prepare("Fresh", "a brand new unstored raincoat", {.store_body = false}));
    CHECK(!loaded.stored("fresh", "body"));
    CHECK_EQ(loaded.search("brand new").front().key, std::string("fresh"));
}

TEST(commit_rejects_a_spent_or_mismatched_prepared_document) {
    luxcore::Index index;
    luxcore::Prepared prepared = index.prepare("t", "b");
    luxcore::Prepared moved = std::move(prepared);
    bool spent = false;
    try {
        index.commit("k", std::move(prepared));
    } catch (const std::invalid_argument&) {
        spent = true;
    }
    CHECK(spent);

    bool empty_key = false;
    try {
        index.commit("", std::move(moved));
    } catch (const std::invalid_argument&) {
        empty_key = true;
    }
    CHECK(empty_key);

    luxcore::Index positional(luxcore::IndexConfig{.positions = true});
    bool mismatched = false;
    try {
        positional.commit("k", index.prepare("t", "b"));
    } catch (const std::invalid_argument&) {
        mismatched = true;
    }
    CHECK(mismatched);
    CHECK_EQ(index.size(), static_cast<std::size_t>(0));
    CHECK_EQ(positional.size(), static_cast<std::size_t>(0));
}
