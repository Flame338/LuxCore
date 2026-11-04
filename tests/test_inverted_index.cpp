#include "TestFramework.h"

#include "InvertedIndex.h"

namespace {

using TermFreqs = std::unordered_map<std::string, int>;

std::string postings_of(const InvertedIndex& index, const std::string& term) {
    const std::vector<Posting>* postings = index.get_postings(term);
    if (postings == nullptr) {
        return "-";
    }

    std::string text;
    for (const Posting& posting : *postings) {
        text += "(" + std::to_string(posting.doc_id) + ":" + std::to_string(posting.term_frequency) + ")";
    }
    return text;
}

}

TEST(index_returns_postings_with_term_frequencies) {
    InvertedIndex index;
    index.add_document(1, TermFreqs{{"cat", 2}, {"dog", 1}});
    index.add_document(2, TermFreqs{{"cat", 1}});

    CHECK_EQ(index.get_total_documents(), 2);
    CHECK_EQ(index.get_unique_terms_count(), static_cast<std::size_t>(2));
    CHECK_EQ(postings_of(index, "cat"), "(1:2)(2:1)");
    CHECK_EQ(postings_of(index, "dog"), "(1:1)");
}

TEST(index_returns_null_for_an_unknown_term) {
    InvertedIndex index;
    index.add_document(1, TermFreqs{{"cat", 1}});

    CHECK(index.get_postings("bird") == nullptr);
}

TEST(index_keeps_postings_sorted_by_doc_id_whatever_the_insert_order) {
    InvertedIndex index;
    index.add_document(5, TermFreqs{{"foo", 1}});
    index.add_document(2, TermFreqs{{"foo", 1}});
    index.add_document(9, TermFreqs{{"foo", 1}});

    CHECK_EQ(postings_of(index, "foo"), "(2:1)(5:1)(9:1)");
}

TEST(index_adding_the_same_document_twice_changes_nothing) {
    InvertedIndex index;
    index.add_document(1, TermFreqs{{"cat", 2}});
    index.add_document(1, TermFreqs{{"cat", 2}});

    CHECK_EQ(index.get_total_documents(), 1);
    CHECK_EQ(postings_of(index, "cat"), "(1:2)");
}

TEST(index_re_adding_a_document_replaces_its_old_terms) {
    InvertedIndex index;
    index.add_document(1, TermFreqs{{"cat", 1}, {"dog", 1}});
    index.add_document(2, TermFreqs{{"dog", 3}});
    index.add_document(1, TermFreqs{{"bird", 2}});

    CHECK_EQ(index.get_total_documents(), 2);
    CHECK(index.get_postings("cat") == nullptr);
    CHECK_EQ(postings_of(index, "dog"), "(2:3)");
    CHECK_EQ(postings_of(index, "bird"), "(1:2)");
    CHECK_EQ(index.get_unique_terms_count(), static_cast<std::size_t>(2));
}

TEST(index_re_adding_a_document_keeps_postings_sorted) {
    InvertedIndex index;
    index.add_document(1, TermFreqs{{"foo", 1}});
    index.add_document(2, TermFreqs{{"foo", 1}});
    index.add_document(3, TermFreqs{{"foo", 1}});
    index.add_document(1, TermFreqs{{"foo", 4}});

    CHECK_EQ(postings_of(index, "foo"), "(1:4)(2:1)(3:1)");
}

TEST(index_after_replacing_a_document_equals_an_index_built_from_scratch) {
    InvertedIndex edited;
    edited.add_document(1, TermFreqs{{"apache", 1}, {"lucene", 1}, {"search", 1}});
    edited.add_document(2, TermFreqs{{"search", 2}, {"engine", 1}});
    edited.add_document(3, TermFreqs{{"lorem", 1}});
    edited.add_document(1, TermFreqs{{"search", 1}, {"zebra", 1}});

    InvertedIndex fresh;
    fresh.add_document(1, TermFreqs{{"search", 1}, {"zebra", 1}});
    fresh.add_document(2, TermFreqs{{"search", 2}, {"engine", 1}});
    fresh.add_document(3, TermFreqs{{"lorem", 1}});

    CHECK_EQ(edited.get_total_documents(), fresh.get_total_documents());
    CHECK_EQ(edited.get_unique_terms_count(), fresh.get_unique_terms_count());
    for (const char* term : {"apache", "lucene", "search", "engine", "zebra", "lorem"}) {
        CHECK_EQ(postings_of(edited, term), postings_of(fresh, term));
    }
}

TEST(index_removing_a_document_drops_its_postings_and_empty_terms) {
    InvertedIndex index;
    index.add_document(1, TermFreqs{{"cat", 1}, {"dog", 1}});
    index.add_document(2, TermFreqs{{"dog", 1}});
    index.remove_document(1);

    CHECK_EQ(index.get_total_documents(), 1);
    CHECK(index.get_postings("cat") == nullptr);
    CHECK_EQ(postings_of(index, "dog"), "(2:1)");
    CHECK_EQ(index.get_unique_terms_count(), static_cast<std::size_t>(1));
}

TEST(index_removing_an_unknown_document_does_nothing) {
    InvertedIndex index;
    index.add_document(1, TermFreqs{{"cat", 1}});
    index.remove_document(42);

    CHECK_EQ(index.get_total_documents(), 1);
    CHECK_EQ(postings_of(index, "cat"), "(1:1)");
}
