#include "TestFramework.h"

#include "InvertedIndex.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

std::vector<Posting> collect_runs(const Postings& postings, std::size_t first, std::size_t last) {
    std::vector<Posting> seen;
    postings.for_each_run(first, last, [&](const std::uint32_t* docs, const auto& frequencies, std::size_t length) {
        for (std::size_t i = 0; i < length; i++) {
            seen.push_back({static_cast<int>(docs[i]), frequencies[i]});
        }
    });
    return seen;
}

void check_runs_match_indexing(const Postings& postings) {
    for (const std::size_t first : {std::size_t{0}, std::size_t{1}, postings.size() / 2}) {
        for (const std::size_t last : {first, first + 1, postings.size()}) {
            if (first > last || last > postings.size()) {
                continue;
            }
            const std::vector<Posting> runs = collect_runs(postings, first, last);
            CHECK_EQ(runs.size(), last - first);
            for (std::size_t i = 0; i < runs.size(); i++) {
                CHECK_EQ(runs[i].doc_id, postings[first + i].doc_id);
                CHECK_EQ(runs[i].term_frequency, postings[first + i].term_frequency);
            }
        }
    }
}

}

TEST(posting_runs_hand_over_every_frequency_whatever_width_the_list_stores) {
    const std::vector<int> widest_frequency{1, 200, 255, 256, 65535, 65536, 3000000};
    for (const int widest : widest_frequency) {
        for (const int count : {1, 2, 3, 5, 9, 40, 300}) {
            InvertedIndex index;
            for (int doc = 0; doc < count; doc++) {
                const int frequency = doc == count / 2 ? widest : 1 + doc % 7;
                index.add_document(doc * 3, FieldTermFreqs{{"body", {{"term", frequency}}}});
            }
            const std::optional<Postings> postings = index.get_postings("body", "term");
            CHECK(postings.has_value());
            CHECK_EQ(postings->size(), static_cast<std::size_t>(count));
            check_runs_match_indexing(*postings);
            for (int doc = 0; doc < count; doc++) {
                const int frequency = doc == count / 2 ? widest : 1 + doc % 7;
                CHECK_EQ((*postings)[static_cast<std::size_t>(doc)].term_frequency, frequency);
                CHECK_EQ(postings->lower_bound(doc * 3 + 1), static_cast<std::size_t>(doc) + 1);
                CHECK(postings->contains(doc * 3));
                CHECK(!postings->contains(doc * 3 + 1));
            }
        }
    }
}

TEST(a_posting_list_widens_its_frequencies_when_a_larger_one_arrives_and_keeps_the_old_ones) {
    InvertedIndex index;
    index.add_document(1, FieldTermFreqs{{"body", {{"term", 3}}}});
    index.add_document(2, FieldTermFreqs{{"body", {{"term", 4}}}});
    index.add_document(5, FieldTermFreqs{{"body", {{"term", 70000}}}});
    index.add_document(3, FieldTermFreqs{{"body", {{"term", 300}}}});
    const std::vector<Posting> runs = collect_runs(*index.get_postings("body", "term"), 0, 4);
    CHECK_EQ(runs.size(), static_cast<std::size_t>(4));
    CHECK_EQ(runs[0].doc_id, 1);
    CHECK_EQ(runs[0].term_frequency, 3);
    CHECK_EQ(runs[1].doc_id, 2);
    CHECK_EQ(runs[1].term_frequency, 4);
    CHECK_EQ(runs[2].doc_id, 3);
    CHECK_EQ(runs[2].term_frequency, 300);
    CHECK_EQ(runs[3].doc_id, 5);
    CHECK_EQ(runs[3].term_frequency, 70000);
}
