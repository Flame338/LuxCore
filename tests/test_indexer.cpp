#include "TestFramework.h"

#include "Indexer.h"

namespace {

std::string docs_for(const Indexer& indexer, const std::string& term) {
    const std::vector<Posting>* postings = indexer.get_index().get_postings(term);
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

TEST(indexer_indexes_both_title_and_body) {
    Indexer indexer("stopwords.txt");
    indexer.add_document(1, "Apache Lucene", "LuxCore search engine");

    CHECK_EQ(docs_for(indexer, "lucene"), "(1:1)");
    CHECK_EQ(docs_for(indexer, "search"), "(1:1)");
    CHECK_EQ(indexer.get_document(1).title, "Apache Lucene");
    CHECK_EQ(indexer.get_document(1).body, "LuxCore search engine");
}

TEST(indexer_counts_repeated_terms_across_title_and_body) {
    Indexer indexer("stopwords.txt");
    indexer.add_document(7, "Search", "search search engine");

    CHECK_EQ(docs_for(indexer, "search"), "(7:3)");
}

TEST(indexer_runs_documents_through_the_analyzer) {
    Indexer indexer("stopwords.txt");
    indexer.add_document(1, "The Cat", "Don\xE2\x80\x99t panic, it\xE2\x80\x99s John\xE2\x80\x99s");

    CHECK_EQ(docs_for(indexer, "the"), "-");
    CHECK_EQ(docs_for(indexer, "don't"), "(1:1)");
    CHECK_EQ(docs_for(indexer, "john"), "(1:1)");
}

TEST(indexer_without_a_stop_word_file_indexes_every_word) {
    Indexer indexer;
    indexer.add_document(1, "The Cat", "");

    CHECK_EQ(docs_for(indexer, "the"), "(1:1)");
}

TEST(indexer_re_adding_an_id_replaces_the_document_and_its_terms) {
    Indexer indexer("stopwords.txt");
    indexer.add_document(1, "Old title", "zebra");
    indexer.add_document(2, "Other", "zebra");
    indexer.add_document(1, "New title", "giraffe");

    CHECK_EQ(indexer.get_index().get_total_documents(), 2);
    CHECK_EQ(indexer.get_document(1).title, "New title");
    CHECK_EQ(docs_for(indexer, "old"), "-");
    CHECK_EQ(docs_for(indexer, "zebra"), "(2:1)");
    CHECK_EQ(docs_for(indexer, "giraffe"), "(1:1)");
}
