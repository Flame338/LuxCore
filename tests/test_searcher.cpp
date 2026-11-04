#include "TestFramework.h"

#include "Searcher.h"

#include <cmath>

namespace {

struct Corpus {
    Analyzer analyzer = make_standard_analyzer("stopwords.txt");
    InvertedIndex index;
    std::unordered_map<int, Document> store;

    void add(int id, const std::string& title, const std::string& body) {
        store[id] = {id, title, body};

        std::unordered_map<std::string, int> term_freqs;
        for (const std::string& token : analyzer.analyze(title + " " + body)) {
            term_freqs[token]++;
        }
        index.add_document(id, term_freqs);
    }

    std::vector<SearchHit> search(const std::string& query, int top_k = 10) const {
        return Searcher(index, analyzer, store).search(query, top_k);
    }
};

std::string ids_of(const std::vector<SearchHit>& hits) {
    std::string text;
    for (const SearchHit& hit : hits) {
        text += "[" + std::to_string(hit.doc_id) + "]";
    }
    return text;
}

}

TEST(searcher_ranks_a_document_with_the_rarer_term_first) {
    Corpus corpus;
    corpus.add(1, "a", "foo bar");
    corpus.add(2, "b", "foo baz");
    corpus.add(3, "c", "foo qux");

    CHECK_EQ(ids_of(corpus.search("foo bar")).substr(0, 3), "[1]");
}

TEST(searcher_scores_with_tf_times_lucene_classic_idf) {
    Corpus corpus;
    corpus.add(1, "x", "apple apple pie");
    corpus.add(2, "y", "pie");

    const std::vector<SearchHit> hits = corpus.search("apple");
    CHECK_EQ(hits.size(), static_cast<std::size_t>(1));
    const double idf = std::log(3.0 / 2.0) + 1.0;
    CHECK_NEAR(hits[0].score, 2.0 * idf, 1e-9);
}

TEST(searcher_still_scores_a_term_that_appears_in_every_document) {
    Corpus corpus;
    corpus.add(1, "a", "foo");
    corpus.add(2, "b", "foo");
    corpus.add(3, "c", "foo");

    const std::vector<SearchHit> hits = corpus.search("foo");
    CHECK_EQ(hits.size(), static_cast<std::size_t>(3));
    CHECK(hits[0].score > 0.0);
}

TEST(searcher_orders_equal_scores_by_doc_id) {
    Corpus corpus;
    corpus.add(5, "a", "foo");
    corpus.add(2, "b", "foo");
    corpus.add(9, "c", "foo");

    CHECK_EQ(ids_of(corpus.search("foo")), "[2][5][9]");
}

TEST(searcher_returns_nothing_for_unknown_words_stop_words_or_an_empty_query) {
    Corpus corpus;
    corpus.add(1, "a", "foo bar");

    CHECK(corpus.search("zzz").empty());
    CHECK(corpus.search("the and").empty());
    CHECK(corpus.search("").empty());
}

TEST(searcher_limits_the_number_of_results_to_top_k) {
    Corpus corpus;
    for (int id = 1; id <= 5; id++) {
        corpus.add(id, "t", "foo");
    }

    CHECK_EQ(ids_of(corpus.search("foo", 2)), "[1][2]");
}

TEST(searcher_analyzes_the_query_the_same_way_as_documents) {
    Corpus corpus;
    corpus.add(1, "Notes", "John's book");

    CHECK_EQ(ids_of(corpus.search("JOHN")), "[1]");
    CHECK_EQ(ids_of(corpus.search("John\xE2\x80\x99s")), "[1]");
}
