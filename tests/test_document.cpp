#include "TestFramework.h"

#include "Document.h"
#include "DocumentStore.h"

#include <stdexcept>

TEST(document_find_returns_null_for_a_missing_field) {
    Document document;
    document.add({"title", "Apache Lucene"});

    CHECK(document.find("body") == nullptr);
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
