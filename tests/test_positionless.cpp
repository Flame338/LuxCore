#include "TestFramework.h"

#include "Indexer.h"
#include "LuxCore.h"
#include "Searcher.h"

#include <filesystem>
#include <fstream>
#include <iterator>

namespace {

void add_corpus(Indexer& indexer) {
    indexer.add_document("a", make_text_document("Cat and dog", "the cat chased the dog around the cat tree"));
    indexer.add_document("b", make_text_document("Dog days", "a dog sleeps all day, dog dreams of cats"));
    indexer.add_document("c", make_text_document("", "birds fly over the quiet tree"));
    indexer.add_document("d", make_text_document("Tree house", "cat cat cat"));
}

std::string ranking(const Indexer& indexer, const std::string& query) {
    const Searcher searcher(indexer, {.scoring = Scoring::bm25_with(), .fuzziness = Fuzziness::On});
    std::string text;
    for (const SearchHit& hit : searcher.search(query, 10)) {
        text += hit.key + "=" + std::to_string(hit.score) + ",";
    }
    return text;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

}

TEST(positionless_indexer_ranks_exactly_like_a_positioned_one) {
    Indexer positioned;
    Indexer counted("", IndexOptions{.positions = false});
    add_corpus(positioned);
    add_corpus(counted);

    for (const char* query : {"cat", "dog", "cat dog", "tre", "cta", "birds ov", "quiet tree", "nothing"}) {
        CHECK_EQ(ranking(counted, query), ranking(positioned, query));
    }
}

TEST(positionless_indexer_keeps_no_positions) {
    Indexer positioned;
    Indexer counted("", IndexOptions{.positions = false});
    add_corpus(positioned);
    add_corpus(counted);

    CHECK(positioned.get_index().memory_usage().position_bytes > 0);
    CHECK_EQ(counted.get_index().memory_usage().position_bytes, static_cast<std::size_t>(0));

    const std::optional<PostingList> list = counted.get_index().get_posting_list("body", "cat");
    CHECK(list.has_value());
    CHECK(!list->has_positions());
    CHECK_EQ(list->postings.size(), static_cast<std::size_t>(2));
    CHECK(list->positions_of(0).empty());
    CHECK_EQ(list->postings[1].term_frequency, 3);
}

TEST(positionless_indexer_removes_and_replaces_documents) {
    Indexer positioned;
    Indexer counted("", IndexOptions{.positions = false});
    for (Indexer* indexer : {&positioned, &counted}) {
        add_corpus(*indexer);
        CHECK(indexer->remove_document("b"));
        indexer->add_document("a", make_text_document("Plain", "only fox here"));
        indexer->add_document("e", make_text_document("Dog", "cat dog fox"));
    }

    CHECK_EQ(counted.get_index().get_total_documents(), 4);
    CHECK_EQ(counted.get_index().get_document_frequency("days"), static_cast<std::size_t>(0));
    CHECK_EQ(counted.get_index().get_document_frequency("chased"), static_cast<std::size_t>(0));
    CHECK(counted.get_index().get_document_frequency("fox") > 0);
    CHECK_EQ(counted.get_index().get_field_length("body", 0), 3);
    CHECK_EQ(counted.get_index().get_field_length("body", 1), 0);
    CHECK_EQ(counted.get_index().get_average_field_length("body"), positioned.get_index().get_average_field_length("body"));
    for (const char* query : {"tree", "fox", "cat", "dog", "fx"}) {
        CHECK_EQ(ranking(counted, query), ranking(positioned, query));
    }
}

TEST(a_positioned_file_loads_without_positions_and_a_positionless_one_only_without) {
    Indexer positioned;
    add_corpus(positioned);
    const std::string full = positioned.serialize();

    const Indexer stripped = Indexer::deserialize(full, "", IndexOptions{.positions = false});
    CHECK_EQ(stripped.get_index().memory_usage().position_bytes, static_cast<std::size_t>(0));
    CHECK(!stripped.get_index().get_posting_list("body", "cat")->has_positions());
    for (const char* query : {"cat", "dog", "tre", "cta"}) {
        CHECK_EQ(ranking(stripped, query), ranking(positioned, query));
    }

    const std::string small = stripped.serialize();
    CHECK(small.size() < full.size());

    bool missing = false;
    try {
        Indexer::deserialize(small, "", IndexOptions{.positions = true});
    } catch (const MissingPositions&) {
        missing = true;
    }
    CHECK(missing);

    const Indexer reloaded = Indexer::deserialize(small, "", IndexOptions{.positions = false});
    CHECK_EQ(reloaded.get_index().memory_usage().position_bytes, static_cast<std::size_t>(0));
    for (const char* query : {"cat", "dog", "tre", "cta"}) {
        CHECK_EQ(ranking(reloaded, query), ranking(positioned, query));
    }
}

bool rejects(InvertedIndex& index, int doc_id, const auto& document) {
    try {
        index.add_document(doc_id, document);
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

TEST(an_index_rejects_documents_whose_positions_do_not_match_it_and_stays_unchanged) {
    InvertedIndex positioned(Positions::On);
    InvertedIndex counted;
    positioned.add_document(0, FieldTermPositions{{"body", TermPositions{{"cat", {4, 9}}}}});
    counted.add_document(0, FieldTermFreqs{{"body", TermFreqs{{"cat", 2}}}});

    CHECK(rejects(positioned, 0, FieldTermFreqs{{"body", TermFreqs{{"dog", 1}}}}));
    CHECK(rejects(positioned, 1, FieldTermFreqs{{"body", TermFreqs{{"cat", 1}}}}));
    CHECK(rejects(counted, 0, FieldTermPositions{{"body", TermPositions{{"dog", {1}}}}}));
    CHECK(rejects(counted, 1, FieldTermPositions{{"body", TermPositions{{"cat", {3}}}}}));

    CHECK_EQ(positioned.get_total_documents(), 1);
    CHECK_EQ(positioned.get_document_frequency("dog"), static_cast<std::size_t>(0));
    const std::optional<PostingList> kept = positioned.get_posting_list("body", "cat");
    CHECK_EQ(kept->postings.size(), static_cast<std::size_t>(1));
    CHECK_EQ(kept->positions_of(0).size(), static_cast<std::size_t>(2));
    CHECK_EQ(kept->positions_of(0)[1], static_cast<std::uint32_t>(9));

    CHECK_EQ(counted.get_total_documents(), 1);
    CHECK_EQ(counted.get_document_frequency("dog"), static_cast<std::size_t>(0));
    CHECK_EQ(counted.get_posting_list("body", "cat")->postings[0].term_frequency, 2);
    CHECK(!counted.get_posting_list("body", "cat")->has_positions());
}

TEST(a_positioned_list_keeps_each_documents_positions_through_removals) {
    InvertedIndex index(Positions::On);
    index.add_document(0, FieldTermPositions{{"body", TermPositions{{"cat", {1, 2}}}}});
    index.add_document(3, FieldTermPositions{{"body", TermPositions{{"cat", {7}}}}});
    index.add_document(1, FieldTermPositions{{"body", TermPositions{{"cat", {4, 9}}}}});
    index.add_document(2, FieldTermPositions{{"body", TermPositions{{"cat", {5, 6, 8}}}}});

    index.remove_document(1);
    std::optional<PostingList> list = index.get_posting_list("body", "cat");
    CHECK_EQ(list->postings.size(), static_cast<std::size_t>(3));
    CHECK_EQ(list->postings[1].doc_id, 2);
    CHECK_EQ(list->positions_of(0)[1], static_cast<std::uint32_t>(2));
    CHECK_EQ(list->positions_of(1)[2], static_cast<std::uint32_t>(8));
    CHECK_EQ(list->positions_of(2)[0], static_cast<std::uint32_t>(7));

    index.remove_document(0);
    index.remove_document(2);
    index.remove_document(3);
    CHECK(!index.get_posting_list("body", "cat"));
    CHECK_EQ(index.memory_usage().position_bytes, static_cast<std::size_t>(0));
}

TEST(the_public_index_saves_a_file_without_positions) {
    luxcore::Index index;
    index.add("a", "Cat and dog", "the cat chased the dog around the cat tree");
    index.add("b", "Dog days", "a dog sleeps all day, dog dreams of cats");

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "luxcore_positionless_test.lux";
    index.save(path.string());
    const std::string bytes = read_file(path);
    std::filesystem::remove(path);

    bool missing = false;
    try {
        Indexer::deserialize(bytes, "", IndexOptions{.positions = true});
    } catch (const MissingPositions&) {
        missing = true;
    }
    CHECK(missing);

    const Indexer loaded = Indexer::deserialize(bytes, "", IndexOptions{.positions = false});
    CHECK_EQ(loaded.get_index().memory_usage().position_bytes, static_cast<std::size_t>(0));
    CHECK_EQ(loaded.get_index().get_total_documents(), 2);
}
