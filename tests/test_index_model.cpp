#include "TestFramework.h"

#include "EditDistance.h"
#include "Indexer.h"
#include "InvertedIndex.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace {

using ModelTerms = std::map<std::string, std::vector<std::uint32_t>>;
using ModelDocument = std::map<std::string, ModelTerms>;
using Model = std::map<int, ModelDocument>;

std::string random_term(std::mt19937& rng) {
    static const std::vector<std::string> alphabet{"a", "b", "c", "\xC3\xA9", "\xE6\x9D\xB1"};
    std::string term;
    const std::size_t length = rng() % 5;
    for (std::size_t i = 0; i < length; i++) {
        term += alphabet[rng() % alphabet.size()];
    }
    return term;
}

std::uint32_t random_frequency(std::mt19937& rng) {
    switch (rng() % 48) {
    case 0:
        return 250 + rng() % 20;
    case 1:
        return 65530 + rng() % 20;
    default:
        return 1 + rng() % 4;
    }
}

ModelDocument random_document(std::mt19937& rng) {
    ModelDocument document;
    for (const char* field : {"body", "tags", "title"}) {
        if (rng() % 3 == 0) {
            continue;
        }
        ModelTerms& terms = document[field];
        const std::size_t count = rng() % 7;
        for (std::size_t i = 0; i < count; i++) {
            std::vector<std::uint32_t> positions(random_frequency(rng));
            for (std::uint32_t& position : positions) {
                position = rng() % 100000;
            }
            std::ranges::sort(positions);
            terms[random_term(rng)] = std::move(positions);
        }
        if (terms.empty()) {
            document.erase(field);
        }
    }
    return document;
}

void add(InvertedIndex& index, int doc_id, const ModelDocument& document, bool positioned) {
    if (positioned) {
        FieldTermPositions fields;
        for (const auto& [field, terms] : document) {
            for (const auto& [term, positions] : terms) {
                fields[field][term] = positions;
            }
        }
        index.add_document(doc_id, fields);
    } else {
        FieldTermFreqs fields;
        for (const auto& [field, terms] : document) {
            for (const auto& [term, positions] : terms) {
                fields[field][term] = static_cast<int>(positions.size());
            }
        }
        index.add_document(doc_id, fields);
    }
}

std::string describe(const std::vector<TermPostings>& matches) {
    std::string text;
    for (const TermPostings& match : matches) {
        text += std::string(match.term) + "=" + std::to_string(match.document_frequency) + ",";
    }
    return text;
}

std::string describe(const std::map<std::string, std::size_t>& frequencies, const std::string& prefix) {
    std::string text;
    for (const auto& [term, frequency] : frequencies) {
        if (term.starts_with(prefix)) {
            text += term + "=" + std::to_string(frequency) + ",";
        }
    }
    return text;
}

std::string describe(const std::vector<FuzzyMatch>& matches) {
    std::string text;
    for (const FuzzyMatch& match : matches) {
        text += match.term + ":" + std::to_string(match.distance) + ",";
    }
    return text;
}

void check_against_model(const InvertedIndex& index, const Model& model, bool positioned, std::mt19937& rng) {
    std::map<std::string, std::size_t> document_frequency;
    std::map<std::string, std::map<std::string, std::size_t>> field_frequency;
    std::map<std::string, std::map<std::string, std::string>> postings;
    std::map<std::string, long long> field_length;
    std::map<std::string, int> field_documents;
    for (const auto& [doc_id, document] : model) {
        std::set<std::string> seen;
        for (const auto& [field, terms] : document) {
            field_documents[field]++;
            long long length = 0;
            for (const auto& [term, positions] : terms) {
                seen.insert(term);
                field_frequency[field][term]++;
                length += static_cast<long long>(positions.size());
                std::string& text = postings[field][term];
                text += "(" + std::to_string(doc_id) + "x" + std::to_string(positions.size());
                for (const std::uint32_t position : positions) {
                    text += positioned ? "@" + std::to_string(position) : "";
                }
                text += ")";
            }
            field_length[field] += length;
            CHECK_EQ(index.get_field_length(field, doc_id), static_cast<int>(length));
        }
        for (const std::string& term : seen) {
            document_frequency[term]++;
        }
    }

    CHECK_EQ(index.get_total_documents(), static_cast<int>(model.size()));
    CHECK_EQ(index.get_unique_terms_count(), document_frequency.size());

    std::string expected_fields;
    for (const auto& entry : field_documents) {
        expected_fields += entry.first + ",";
    }
    std::string actual_fields;
    for (const std::string_view name : index.field_names()) {
        actual_fields += std::string(name) + ",";
    }
    CHECK_EQ(actual_fields, expected_fields);

    for (const std::string& prefix : {std::string(), random_term(rng), std::string("a"), std::string("\xC3")}) {
        CHECK_EQ(describe(index.terms_with_prefix(prefix)), describe(document_frequency, prefix));
        for (const char* field : {"body", "tags", "title", "absent"}) {
            CHECK_EQ(describe(index.terms_with_prefix(field, prefix)), describe(field_frequency[field], prefix));
        }
    }

    for (const auto& [term, frequency] : document_frequency) {
        CHECK_EQ(index.get_document_frequency(term), frequency);
        std::string expected_order;
        std::string actual_order;
        for (const char* field : {"body", "tags", "title"}) {
            expected_order += postings[field].contains(term) ? std::string(field) + "," : "";
        }
        for (const FieldPostings& entry : index.get_field_postings(term)) {
            actual_order += std::string(entry.field) + ",";
        }
        CHECK_EQ(actual_order, expected_order);
    }
    CHECK_EQ(index.get_document_frequency(random_term(rng) + "zz"), static_cast<std::size_t>(0));

    for (const char* field : {"body", "tags", "title"}) {
        CHECK_EQ(index.get_field_documents(field), field_documents[field]);
        if (field_documents[field] > 0) {
            CHECK_NEAR(index.get_average_field_length(field),
                       static_cast<double>(field_length[field]) / field_documents[field], 1e-12);
        }
        for (const auto& [term, expected] : postings[field]) {
            const std::optional<PostingList> list = index.get_posting_list(field, term);
            CHECK(list.has_value());
            if (!list) {
                continue;
            }
            std::string actual;
            for (std::size_t i = 0; i < list->postings.size(); i++) {
                const Posting posting = list->postings[i];
                actual += "(" + std::to_string(posting.doc_id) + "x" + std::to_string(posting.term_frequency);
                for (const std::uint32_t position : list->positions_of(i)) {
                    actual += "@" + std::to_string(position);
                }
                actual += ")";
            }
            CHECK_EQ(actual, expected);
        }
        CHECK(!index.get_postings(field, random_term(rng) + "zz"));
    }

    const std::string query = random_term(rng);
    for (std::size_t max_distance = 0; max_distance <= 2; max_distance++) {
        std::vector<FuzzyMatch> expected;
        for (const auto& entry : document_frequency) {
            const std::size_t distance = edit_distance(query, entry.first);
            if (distance <= max_distance) {
                expected.push_back({entry.first, distance});
            }
        }
        std::ranges::sort(expected, [](const FuzzyMatch& a, const FuzzyMatch& b) {
            return a.distance != b.distance ? a.distance < b.distance : a.term < b.term;
        });
        CHECK_EQ(describe(index.fuzzy_terms(query, max_distance)), describe(expected));
    }
}

void run_random_edits(bool positioned, unsigned seed, unsigned documents = 30, int steps = 1500) {
    std::mt19937 rng(seed);
    InvertedIndex index;
    Model model;
    for (int step = 0; step < steps; step++) {
        const int doc_id = static_cast<int>(rng() % documents);
        const std::uint64_t before = index.state();
        if (rng() % 3 == 0) {
            index.remove_document(doc_id);
            CHECK_EQ(index.state() != before, model.contains(doc_id));
            model.erase(doc_id);
        } else {
            ModelDocument document = random_document(rng);
            add(index, doc_id, document, positioned);
            CHECK(index.state() != before);
            model[doc_id] = std::move(document);
        }
        if (step % 100 == 0) {
            check_against_model(index, model, positioned, rng);
        }
    }
    check_against_model(index, model, positioned, rng);

    while (!model.empty()) {
        index.remove_document(model.begin()->first);
        model.erase(model.begin());
    }
    check_against_model(index, model, positioned, rng);
    CHECK_EQ(index.memory_usage().position_bytes, static_cast<std::size_t>(0));
}

std::string random_text(std::mt19937& rng) {
    static const std::vector<std::string> words{"alpha", "alpine", "beta",  "bet",  "gamma", "caf\xC3\xA9",
                                                "delta", "del",    "omega", "zeta", "eta",   "theta"};
    std::string text;
    const std::size_t count = rng() % 12;
    for (std::size_t i = 0; i < count; i++) {
        text += words[rng() % words.size()] + (rng() % 5 == 0 ? std::to_string(rng() % 50) : "") + " ";
    }
    return text;
}

}

TEST(random_adds_replacements_and_removals_match_a_reference_model) {
    run_random_edits(false, 31, 400, 600);
    run_random_edits(true, 32, 250, 400);
}

TEST(a_loaded_index_takes_the_same_edits_as_the_one_it_was_saved_from) {
    for (const bool positions : {false, true}) {
        Indexer original("", IndexOptions{.positions = positions});
        const auto edit = [](Indexer& indexer, unsigned seed) {
            std::mt19937 local(seed);
            for (int step = 0; step < 300; step++) {
                const std::string key = "k" + std::to_string(local() % 60);
                if (local() % 4 == 0) {
                    indexer.remove_document(key);
                } else {
                    Document document;
                    document.add({"title", random_text(local), FieldOptions::text()});
                    document.add({"body", random_text(local), FieldOptions::text()});
                    indexer.add_document(key, document);
                }
            }
        };
        edit(original, 1);

        const std::string saved = original.serialize();
        Indexer loaded = Indexer::deserialize(saved, "", IndexOptions{.positions = positions});
        CHECK_EQ(loaded.serialize() == saved, true);

        edit(original, 2);
        edit(loaded, 2);
        CHECK_EQ(loaded.serialize() == original.serialize(), true);
        CHECK_EQ(loaded.get_index().get_unique_terms_count(), original.get_index().get_unique_terms_count());
    }
}
