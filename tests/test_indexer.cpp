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

TEST(indexer_assigns_sequential_internal_ids) {
    Indexer indexer("stopwords.txt");

    CHECK_EQ(indexer.add_document(make_text_document("First", "")), 0);
    CHECK_EQ(indexer.add_document(make_text_document("Second", "")), 1);
    CHECK_EQ(indexer.get_index().get_total_documents(), 2);
}

TEST(indexer_indexes_both_title_and_body) {
    Indexer indexer("stopwords.txt");
    const int id = indexer.add_document(make_text_document("Apache Lucene", "LuxCore search engine"));

    CHECK_EQ(docs_for(indexer, "lucene"), "(0:1)");
    CHECK_EQ(docs_for(indexer, "search"), "(0:1)");
    CHECK_EQ(indexer.get_document(id).find("title")->value, "Apache Lucene");
    CHECK_EQ(indexer.get_document(id).find("body")->value, "LuxCore search engine");
}

TEST(indexer_counts_repeated_terms_across_fields) {
    Indexer indexer("stopwords.txt");
    indexer.add_document(make_text_document("Search", "search search engine"));

    CHECK_EQ(docs_for(indexer, "search"), "(0:3)");
}

TEST(indexer_runs_documents_through_the_analyzer) {
    Indexer indexer("stopwords.txt");
    indexer.add_document(make_text_document("The Cat", "Don\xE2\x80\x99t panic, it\xE2\x80\x99s John\xE2\x80\x99s"));

    CHECK_EQ(docs_for(indexer, "the"), "-");
    CHECK_EQ(docs_for(indexer, "don't"), "(0:1)");
    CHECK_EQ(docs_for(indexer, "john"), "(0:1)");
}

TEST(indexer_without_a_stop_word_file_indexes_every_word) {
    Indexer indexer;
    indexer.add_document(make_text_document("The Cat", ""));

    CHECK_EQ(docs_for(indexer, "the"), "(0:1)");
}

TEST(indexer_does_not_keep_a_field_that_is_not_stored) {
    Indexer indexer("stopwords.txt");
    Document document;
    document.add({"title", "Notes", FieldOptions::text()});
    document.add({"body", "zebra crossing", {false, true, true}});
    const int id = indexer.add_document(document);

    CHECK_EQ(docs_for(indexer, "zebra"), "(0:1)");
    CHECK(indexer.get_document(id).find("title") != nullptr);
    CHECK(indexer.get_document(id).find("body") == nullptr);
}

TEST(indexer_does_not_search_a_field_that_is_not_indexed) {
    Indexer indexer("stopwords.txt");
    Document document;
    document.add({"title", "Notes", FieldOptions::text()});
    document.add({"thumbnail", "secret picture", FieldOptions::stored_only()});
    const int id = indexer.add_document(document);

    CHECK_EQ(docs_for(indexer, "secret"), "-");
    CHECK_EQ(indexer.get_document(id).find("thumbnail")->value, "secret picture");
}

TEST(indexer_indexes_a_field_that_is_not_analyzed_as_one_exact_term) {
    Indexer indexer("stopwords.txt");
    Document document;
    document.add({"title", "Notes", FieldOptions::text()});
    document.add({"path", "Reports/Q3-2024.pdf", FieldOptions::keyword()});
    indexer.add_document(document);

    CHECK_EQ(docs_for(indexer, "Reports/Q3-2024.pdf"), "(0:1)");
    CHECK_EQ(docs_for(indexer, "reports"), "-");
    CHECK_EQ(docs_for(indexer, "pdf"), "-");
}

TEST(indexer_does_not_index_an_empty_exact_field) {
    Indexer indexer("stopwords.txt");
    Document document;
    document.add({"path", "", FieldOptions::keyword()});
    indexer.add_document(document);

    CHECK_EQ(docs_for(indexer, ""), "-");
    CHECK_EQ(indexer.get_index().get_unique_terms_count(), static_cast<std::size_t>(0));
}

TEST(indexer_adds_a_document_with_no_fields) {
    Indexer indexer("stopwords.txt");
    const int id = indexer.add_document(Document{});

    CHECK_EQ(id, 0);
    CHECK_EQ(indexer.get_index().get_total_documents(), 1);
    CHECK(indexer.get_document(id).get_fields().empty());
}
