#include "TestFramework.h"

#include "Indexer.h"
#include "Searcher.h"

#include <cmath>

namespace {

struct BodyIndex {
    Analyzer analyzer = make_standard_analyzer("stopwords.txt");
    InvertedIndex index;

    void add(int id, const std::string& body) {
        TermFreqs term_freqs;
        for (const std::string& token : analyzer.analyze(body)) {
            term_freqs[token]++;
        }
        index.add_document(id, FieldTermFreqs{{"body", term_freqs}});
    }

    std::vector<SearchHit> search(const std::string& query, Scoring scoring, int top_k = 10) const {
        return Searcher(index, analyzer, FieldBoosts(), nullptr, scoring).search(query, top_k);
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

TEST(index_tracks_field_length_and_average_length) {
    InvertedIndex index;
    index.add_document(1, FieldTermFreqs{{"title", {{"cat", 1}}}, {"body", {{"cat", 2}, {"dog", 1}}}});
    index.add_document(2, FieldTermFreqs{{"body", {{"cat", 1}}}});

    CHECK_EQ(index.get_field_length("body", 1), 3);
    CHECK_EQ(index.get_field_length("body", 2), 1);
    CHECK_EQ(index.get_field_length("title", 1), 1);
    CHECK_EQ(index.get_field_length("title", 2), 0);
    CHECK_EQ(index.get_field_length("missing", 1), 0);
    CHECK_NEAR(index.get_average_field_length("body"), 2.0, 1e-12);
    CHECK_NEAR(index.get_average_field_length("title"), 1.0, 1e-12);
    CHECK_NEAR(index.get_average_field_length("missing"), 0.0, 1e-12);
}

TEST(field_lengths_follow_removal_and_update) {
    InvertedIndex index;
    index.add_document(1, FieldTermFreqs{{"body", {{"cat", 3}}}});
    index.add_document(2, FieldTermFreqs{{"body", {{"cat", 1}}}});
    CHECK_NEAR(index.get_average_field_length("body"), 2.0, 1e-12);

    index.add_document(1, FieldTermFreqs{{"body", {{"cat", 5}}}});
    CHECK_EQ(index.get_field_length("body", 1), 5);
    CHECK_NEAR(index.get_average_field_length("body"), 3.0, 1e-12);

    index.remove_document(1);
    CHECK_EQ(index.get_field_length("body", 1), 0);
    CHECK_NEAR(index.get_average_field_length("body"), 1.0, 1e-12);

    index.remove_document(2);
    CHECK_NEAR(index.get_average_field_length("body"), 0.0, 1e-12);
}

TEST(the_default_scorer_is_tf_idf) {
    BodyIndex corpus;
    corpus.add(1, "apple apple pie");
    corpus.add(2, "pie");

    const std::vector<SearchHit> hits = Searcher(corpus.index, corpus.analyzer).search("apple ");
    CHECK_EQ(hits.size(), static_cast<std::size_t>(1));
    CHECK_NEAR(hits[0].score, 2.0 * (std::log(3.0 / 2.0) + 1.0), 1e-9);
}

TEST(bm25_scores_a_term_with_saturated_and_length_normalised_tf) {
    BodyIndex corpus;
    corpus.add(1, "foo foo bar");
    corpus.add(2, "bar");

    const std::vector<SearchHit> hits = corpus.search("foo ", Scoring::bm25_with(1.2, 0.75));
    CHECK_EQ(hits.size(), static_cast<std::size_t>(1));
    const double idf = std::log(1.0 + (2.0 - 1.0 + 0.5) / (1.0 + 0.5));
    const double tf = 2.0 / (1.0 - 0.75 + 0.75 * 3.0 / 2.0);
    CHECK_NEAR(hits[0].score, idf * tf / (1.2 + tf), 1e-9);
}

TEST(bm25_prefers_the_shorter_field_when_term_frequency_is_equal) {
    BodyIndex corpus;
    corpus.add(1, "rocket alpha beta gamma delta epsilon zeta eta theta");
    corpus.add(2, "rocket");
    corpus.add(3, "other words here");

    CHECK_EQ(ids_of(corpus.search("rocket ", Scoring::tf_idf())), "[1][2]");
    CHECK_EQ(ids_of(corpus.search("rocket ", Scoring::bm25_with())), "[2][1]");
}

TEST(bm25_saturates_term_frequency) {
    BodyIndex corpus;
    corpus.add(1, "rocket rocket rocket rocket rocket rocket rocket rocket");
    corpus.add(2, "rocket rocket rocket rocket");
    corpus.add(3, "pad");

    const double tf_idf_ratio = corpus.search("rocket ", Scoring::tf_idf())[0].score /
                                corpus.search("rocket ", Scoring::tf_idf())[1].score;
    const std::vector<SearchHit> bm25 = corpus.search("rocket ", Scoring::bm25_with(1.2, 0.0));
    CHECK_NEAR(tf_idf_ratio, 2.0, 1e-9);
    CHECK_EQ(ids_of(bm25), "[1][2]");
    CHECK(bm25[0].score / bm25[1].score < 1.2);
}

TEST(bm25_with_b_zero_ignores_length_and_with_k1_zero_ignores_frequency) {
    BodyIndex corpus;
    corpus.add(1, "rocket rocket alpha beta gamma delta epsilon zeta");
    corpus.add(2, "rocket");
    corpus.add(3, "pad");

    const std::vector<SearchHit> no_length = corpus.search("rocket ", Scoring::bm25_with(1.2, 0.0));
    CHECK_EQ(ids_of(no_length), "[1][2]");

    const std::vector<SearchHit> binary = corpus.search("rocket ", Scoring::bm25_with(0.0, 0.0));
    CHECK_EQ(ids_of(binary), "[1][2]");
    CHECK_NEAR(binary[0].score, binary[1].score, 1e-12);
}

TEST(bm25_still_scores_a_term_that_appears_in_every_document) {
    BodyIndex corpus;
    corpus.add(1, "foo");
    corpus.add(2, "foo");

    const std::vector<SearchHit> hits = corpus.search("foo ", Scoring::bm25_with());
    CHECK_EQ(hits.size(), static_cast<std::size_t>(2));
    CHECK(hits[0].score > 0.0);
}

TEST(bm25_keeps_field_boosts_working) {
    Indexer indexer("stopwords.txt");
    indexer.add_document(make_text_document("Notes", "rocket"));
    indexer.add_document(make_text_document("Rocket", "notes"));

    const Searcher standard(indexer.get_index(), indexer.get_analyzer(), FieldBoosts::standard(), nullptr,
                            Scoring::bm25_with());
    const Searcher flipped(indexer.get_index(), indexer.get_analyzer(), FieldBoosts().set("body", 5.0), nullptr,
                           Scoring::bm25_with());
    CHECK_EQ(ids_of(standard.search("rocket ")), "[1][0]");
    CHECK_EQ(ids_of(flipped.search("rocket ")), "[0][1]");
}

TEST(bm25_scoped_search_uses_the_chosen_field_statistics) {
    Indexer indexer("stopwords.txt");
    indexer.add_document(make_text_document("Rocket", "alpha beta gamma delta epsilon zeta"));
    indexer.add_document(make_text_document("Rocket launch pad", "alpha"));

    const Searcher searcher(indexer.get_index(), indexer.get_analyzer(), FieldBoosts(), nullptr,
                            Scoring::bm25_with());
    CHECK_EQ(ids_of(searcher.search_in("title", "rocket ")), "[0][1]");
    const std::vector<SearchHit> hits = searcher.search_in("title", "rocket ");
    const double idf = std::log(1.0 + (2.0 - 2.0 + 0.5) / (2.0 + 0.5));
    const double short_norm = 1.0 - 0.75 + 0.75 * 1.0 / 2.0;
    const double long_norm = 1.0 - 0.75 + 0.75 * 3.0 / 2.0;
    CHECK_NEAR(hits[0].score, idf * (1.0 / short_norm) / (1.2 + 1.0 / short_norm), 1e-9);
    CHECK_NEAR(hits[1].score, idf * (1.0 / long_norm) / (1.2 + 1.0 / long_norm), 1e-9);
}

TEST(top_k_returns_the_best_hits_in_rank_order_with_ties_by_doc_id) {
    BodyIndex corpus;
    corpus.add(9, "foo");
    corpus.add(4, "foo");
    corpus.add(7, "foo foo");
    corpus.add(2, "foo");
    corpus.add(5, "bar");

    CHECK_EQ(ids_of(corpus.search("foo ", Scoring::tf_idf(), 3)), "[7][2][4]");
    CHECK_EQ(ids_of(corpus.search("foo ", Scoring::tf_idf(), 100)), "[7][2][4][9]");
    CHECK_EQ(ids_of(corpus.search("foo ", Scoring::tf_idf(), 1)), "[7]");
    CHECK(corpus.search("foo ", Scoring::tf_idf(), 0).empty());
    CHECK(corpus.search("foo ", Scoring::tf_idf(), -1).empty());
}

TEST(top_k_agrees_with_a_full_sort_on_many_documents) {
    BodyIndex corpus;
    for (int id = 0; id < 200; id++) {
        std::string body;
        for (int n = 0; n < 1 + id % 7; n++) {
            body += "foo ";
        }
        corpus.add(id, body + std::string(static_cast<std::size_t>(id % 5), 'x') + " pad");
    }

    const std::vector<SearchHit> all = corpus.search("foo ", Scoring::bm25_with(), 1000);
    const std::vector<SearchHit> top = corpus.search("foo ", Scoring::bm25_with(), 10);
    CHECK_EQ(all.size(), static_cast<std::size_t>(200));
    CHECK_EQ(top.size(), static_cast<std::size_t>(10));
    for (std::size_t i = 0; i < top.size(); i++) {
        CHECK_EQ(top[i].doc_id, all[i].doc_id);
    }
    for (std::size_t i = 1; i < all.size(); i++) {
        CHECK(all[i - 1].score > all[i].score || (all[i - 1].score == all[i].score && all[i - 1].doc_id < all[i].doc_id));
    }
}
