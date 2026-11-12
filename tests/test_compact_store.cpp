#include "TestFramework.h"

#include "DocumentKeys.h"
#include "DocumentStore.h"
#include "Indexer.h"

#include <iterator>
#include <map>
#include <random>
#include <string>
#include <unordered_map>

namespace {

std::string big_body(int seed, std::size_t size) {
    std::string body;
    while (body.size() < size) {
        body += "word" + std::to_string(seed * 7919 + static_cast<int>(body.size())) + " ";
    }
    return body;
}

std::string snapshot(const Indexer& indexer, int id_limit) {
    std::string text;
    for (int id = 0; id < id_limit; id++) {
        if (!indexer.contains(id)) {
            text += std::to_string(id) + ":-\n";
            continue;
        }
        text += std::to_string(id) + ":" + indexer.get_key(id);
        for (const Field& field : indexer.get_document(id).get_fields()) {
            text += "|" + field.name + "=" + field.value;
        }
        text += "\n";
    }
    return text;
}

}

TEST(document_keys_agree_with_a_map_through_bind_unbind_and_growth) {
    std::mt19937 rng(11);
    DocumentKeys keys;
    std::unordered_map<std::string, int> expected;
    std::map<int, std::string> bound;
    int next_id = 0;

    for (int step = 0; step < 6000; step++) {
        if (!bound.empty() && rng() % 3 == 0) {
            auto victim = bound.begin();
            std::advance(victim, static_cast<long>(rng() % bound.size()));
            keys.unbind(victim->first);
            expected.erase(victim->second);
            bound.erase(victim);
        } else {
            const std::string key = "key-" + std::to_string(step) + "-" + std::string(rng() % 20, 'x');
            keys.bind(key, next_id);
            expected[key] = next_id;
            bound[next_id] = key;
            next_id++;
        }

        if (step % 250 == 0) {
            for (const auto& [key, id] : expected) {
                CHECK_EQ(keys.find(key), std::optional<int>(id));
                CHECK_EQ(keys.key_of(id), key);
            }
            CHECK(!keys.find("never-bound").has_value());
        }
    }

    for (const auto& [id, key] : bound) {
        CHECK_EQ(keys.find(key), std::optional<int>(id));
        keys.unbind(id);
        CHECK(!keys.find(key).has_value());
        CHECK_EQ(keys.key_of(id), std::string());
    }
}

TEST(document_keys_reuse_a_key_after_it_is_unbound) {
    DocumentKeys keys;
    keys.bind("a", 0);
    keys.bind("b", 1);
    keys.unbind(0);
    CHECK(!keys.find("a").has_value());
    CHECK_EQ(keys.find("b"), std::optional<int>(1));

    keys.bind("a", 2);
    CHECK_EQ(keys.find("a"), std::optional<int>(2));
    keys.unbind(7);
    keys.unbind(-1);
    CHECK_EQ(keys.find("a"), std::optional<int>(2));
}

TEST(document_store_keeps_every_survivor_intact_across_compaction) {
    Indexer indexer;
    for (int i = 0; i < 300; i++) {
        indexer.add_document("doc-" + std::to_string(i),
                             make_text_document("title " + std::to_string(i), big_body(i, 2000)));
    }
    for (int i = 0; i < 300; i++) {
        if (i % 5 != 0) {
            CHECK(indexer.remove_document("doc-" + std::to_string(i)));
        }
    }
    for (int round = 0; round < 4; round++) {
        for (int i = 0; i < 300; i += 10) {
            indexer.add_document("doc-" + std::to_string(i),
                                 make_text_document("round " + std::to_string(round), big_body(i + round, 1500)));
        }
    }

    for (int i = 0; i < 300; i += 10) {
        const std::optional<int> id = indexer.find_id("doc-" + std::to_string(i));
        CHECK(id.has_value());
        CHECK_EQ(indexer.stored_value(*id, "title"), std::optional<std::string_view>("round 3"));
        CHECK_EQ(std::string(*indexer.stored_value(*id, "body")), big_body(i + 3, 1500));
    }
    for (int i = 5; i < 300; i += 10) {
        const std::optional<int> id = indexer.find_id("doc-" + std::to_string(i));
        CHECK(id.has_value());
        CHECK_EQ(std::string(*indexer.stored_value(*id, "body")), big_body(i, 2000));
    }
    CHECK_EQ(indexer.get_index().get_total_documents(), 60);

    const std::string before = snapshot(indexer, 300);
    const Indexer reloaded = Indexer::deserialize(indexer.serialize());
    CHECK_EQ(snapshot(reloaded, 300), before);
}

TEST(document_store_value_of_finds_the_first_field_with_a_name) {
    DocumentStore store;
    Document document;
    document.add({"tag", "first", FieldOptions::keyword()});
    document.add({"tag", "second", FieldOptions::keyword()});
    document.add({"note", "", FieldOptions::stored_only()});
    const int id = store.add(document);

    CHECK_EQ(store.value_of(id, "tag"), std::optional<std::string_view>("first"));
    CHECK_EQ(store.value_of(id, "note"), std::optional<std::string_view>(""));
    CHECK(!store.value_of(id, "missing").has_value());
    CHECK(!store.value_of(id + 1, "tag").has_value());
    CHECK(!store.value_of(-1, "tag").has_value());

    const Document copy = store.get(id);
    CHECK_EQ(copy.get_fields().size(), static_cast<std::size_t>(3));
    CHECK(copy.find("note")->options == FieldOptions::stored_only());
    CHECK(copy.find("tag")->options == FieldOptions::keyword());
}

TEST(document_store_replace_and_remove_leave_other_documents_alone) {
    DocumentStore store;
    const int a = store.add(make_text_document("alpha", "one"));
    const int b = store.add(make_text_document("beta", "two"));
    const int c = store.add(make_text_document("gamma", "three"));

    store.replace(b, make_text_document("beta 2", "a much longer replacement body than before"));
    CHECK(store.remove(a));

    CHECK_EQ(store.value_of(b, "title"), std::optional<std::string_view>("beta 2"));
    CHECK_EQ(store.value_of(c, "body"), std::optional<std::string_view>("three"));
    CHECK(!store.contains(a));
    CHECK_EQ(store.size(), static_cast<std::size_t>(2));
    CHECK_EQ(store.id_limit(), static_cast<std::size_t>(3));

    bool rejected = false;
    try {
        store.replace(a, make_text_document("x", "y"));
    } catch (const std::out_of_range&) {
        rejected = true;
    }
    CHECK(rejected);
}
