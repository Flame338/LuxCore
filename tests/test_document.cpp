#include "TestFramework.h"

#include "Document.h"
#include "DocumentStore.h"

#include <stdexcept>

TEST(document_holds_named_fields_with_their_options) {
    Document document;
    document.add({"title", "Apache Lucene", FieldOptions::text()});
    document.add({"path", "notes/lucene.txt", FieldOptions::keyword()});
    document.add({"thumbnail", "lucene.png", FieldOptions::stored_only()});

    CHECK_EQ(document.get_fields().size(), static_cast<std::size_t>(3));

    const Field* title = document.find("title");
    CHECK(title != nullptr);
    if (title == nullptr) {
        return;
    }
    CHECK_EQ(title->value, "Apache Lucene");
    CHECK(title->options.stored);
    CHECK(title->options.indexed);
    CHECK(title->options.analyzed);

    const Field* path = document.find("path");
    CHECK(path != nullptr);
    if (path == nullptr) {
        return;
    }
    CHECK(path->options.stored);
    CHECK(path->options.indexed);
    CHECK(!path->options.analyzed);

    const Field* thumbnail = document.find("thumbnail");
    CHECK(thumbnail != nullptr);
    if (thumbnail == nullptr) {
        return;
    }
    CHECK(thumbnail->options.stored);
    CHECK(!thumbnail->options.indexed);
    CHECK(!thumbnail->options.analyzed);
}

TEST(document_find_returns_null_for_a_missing_field) {
    Document document;
    document.add({"title", "Apache Lucene"});

    CHECK(document.find("body") == nullptr);
}

TEST(document_find_returns_the_first_field_with_that_name) {
    Document document;
    document.add({"tag", "search"});
    document.add({"tag", "engine"});

    CHECK_EQ(document.get_fields().size(), static_cast<std::size_t>(2));
    CHECK_EQ(document.find("tag")->value, "search");
}

TEST(make_text_document_has_a_stored_indexed_analyzed_title_and_body) {
    const Document document = make_text_document("Apache Lucene", "A search library");

    CHECK_EQ(document.find("title")->value, "Apache Lucene");
    CHECK_EQ(document.find("body")->value, "A search library");
    CHECK(document.find("title")->options == FieldOptions::text());
    CHECK(document.find("body")->options == FieldOptions::text());
}

TEST(document_store_assigns_sequential_ids_starting_at_zero) {
    DocumentStore store;

    CHECK_EQ(store.add(make_text_document("a", "")), 0);
    CHECK_EQ(store.add(make_text_document("b", "")), 1);
    CHECK_EQ(store.add(make_text_document("c", "")), 2);
    CHECK_EQ(store.size(), static_cast<std::size_t>(3));
}

TEST(document_store_returns_the_document_added_under_an_id) {
    DocumentStore store;
    const int first = store.add(make_text_document("first", "one"));
    const int second = store.add(make_text_document("second", "two"));

    CHECK_EQ(store.get(first).find("title")->value, "first");
    CHECK_EQ(store.get(second).find("body")->value, "two");
}

TEST(document_store_rejects_an_unknown_id) {
    DocumentStore store;
    store.add(make_text_document("only", ""));

    bool past_the_end = false;
    try {
        store.get(1);
    } catch (const std::out_of_range&) {
        past_the_end = true;
    }
    CHECK(past_the_end);

    bool negative = false;
    try {
        store.get(-1);
    } catch (const std::out_of_range&) {
        negative = true;
    }
    CHECK(negative);
}
