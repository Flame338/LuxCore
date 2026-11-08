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

    const PostingList* list = counted.get_index().get_posting_list("body", "cat");
    CHECK(list != nullptr);
    CHECK(list->offsets.empty());
    CHECK(list->positions.empty());
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
    CHECK(counted.get_index().get_postings("days") == nullptr);
    CHECK(counted.get_index().get_postings("chased") == nullptr);
    CHECK(counted.get_index().get_postings("fox") != nullptr);
    CHECK_EQ(counted.get_index().get_field_length("body", 0), 3);
    CHECK_EQ(counted.get_index().get_field_length("body", 1), 0);
    CHECK_EQ(counted.get_index().get_average_field_length("body"), positioned.get_index().get_average_field_length("body"));
    for (const char* query : {"tree", "fox", "cat", "dog", "fx"}) {
        CHECK_EQ(ranking(counted, query), ranking(positioned, query));
    }
}

TEST(a_positioned_file_loads_without_positions_and_a_positionless_one_loads_with_them) {
    Indexer positioned;
    add_corpus(positioned);
    const std::string full = positioned.serialize();

    const Indexer stripped = Indexer::deserialize(full, "", IndexOptions{.positions = false});
    CHECK_EQ(stripped.get_index().memory_usage().position_bytes, static_cast<std::size_t>(0));
    CHECK(stripped.get_index().get_posting_list("body", "cat")->offsets.empty());
    for (const char* query : {"cat", "dog", "tre", "cta"}) {
        CHECK_EQ(ranking(stripped, query), ranking(positioned, query));
    }

    const std::string small = stripped.serialize();
    CHECK(small.size() < full.size());

    const Indexer reloaded = Indexer::deserialize(small);
    CHECK_EQ(reloaded.get_index().memory_usage().position_bytes, static_cast<std::size_t>(0));
    for (const char* query : {"cat", "dog", "tre", "cta"}) {
        CHECK_EQ(ranking(reloaded, query), ranking(positioned, query));
    }
}

TEST(a_list_can_mix_documents_with_and_without_positions) {
    InvertedIndex index;
    index.add_document(0, FieldTermFreqs{{"body", TermFreqs{{"cat", 2}}}});
    index.add_document(1, FieldTermPositions{{"body", TermPositions{{"cat", {4, 9}}}}});
    index.add_document(2, FieldTermFreqs{{"body", TermFreqs{{"cat", 1}}}});
    index.add_document(3, FieldTermPositions{{"body", TermPositions{{"cat", {7}}}}});

    const PostingList* list = index.get_posting_list("body", "cat");
    CHECK_EQ(list->postings.size(), static_cast<std::size_t>(4));
    CHECK(!list->has_positions(0));
    CHECK(list->has_positions(1));
    CHECK(!list->has_positions(2));
    CHECK(list->has_positions(3));
    CHECK_EQ(list->positions_of(1)[0], static_cast<std::uint32_t>(4));
    CHECK_EQ(list->positions_of(1)[1], static_cast<std::uint32_t>(9));
    CHECK_EQ(list->positions_of(3)[0], static_cast<std::uint32_t>(7));

    index.remove_document(1);
    list = index.get_posting_list("body", "cat");
    CHECK_EQ(list->postings.size(), static_cast<std::size_t>(3));
    CHECK(!list->has_positions(0));
    CHECK(!list->has_positions(1));
    CHECK(list->has_positions(2));
    CHECK_EQ(list->positions_of(2)[0], static_cast<std::uint32_t>(7));

    index.remove_document(3);
    index.remove_document(0);
    index.remove_document(2);
    CHECK(index.get_posting_list("body", "cat") == nullptr);
    CHECK_EQ(index.get_total_documents(), 0);
}

TEST(the_public_index_saves_a_file_without_positions) {
    luxcore::Index index;
    index.add("a", "Cat and dog", "the cat chased the dog around the cat tree");
    index.add("b", "Dog days", "a dog sleeps all day, dog dreams of cats");

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "luxcore_positionless_test.lux";
    index.save(path.string());
    const std::string bytes = read_file(path);
    std::filesystem::remove(path);

    const Indexer loaded = Indexer::deserialize(bytes);
    CHECK_EQ(loaded.get_index().memory_usage().position_bytes, static_cast<std::size_t>(0));
    CHECK(loaded.get_index().get_posting_list("body", "cat")->offsets.empty());
    CHECK_EQ(loaded.get_index().get_total_documents(), 2);
}
