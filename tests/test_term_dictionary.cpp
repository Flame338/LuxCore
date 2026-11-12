#include "TestFramework.h"

#include "EditDistance.h"
#include "TermDictionary.h"

#include <algorithm>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace {

// Raw bytes that are not valid UTF-8 on their own are included on purpose: documents can hold
// binary text, and the trie must order, find and fuzzy-match such terms like any other.
std::string random_bytes_term(std::mt19937& rng, std::size_t max_length, bool shared_prefix) {
    static const std::vector<std::string> alphabet{"a",        "b",        "c",        "z",        "\xC3\xA9",
                                                   "\xC3\xB1", "\xE6\x9D\xB1", "\xF0\x9F\x98\x80", "\x80", "\xC3",
                                                   "\xE6\x9D", "\xFF",     "0"};
    std::string term = shared_prefix ? "common-prefix-" : "";
    const std::size_t length = rng() % (max_length + 1);
    for (std::size_t i = 0; i < length; i++) {
        term += alphabet[rng() % alphabet.size()];
    }
    return term;
}

std::string describe(const std::vector<FuzzyMatch>& matches) {
    std::string text;
    for (const FuzzyMatch& match : matches) {
        text += match.term + ":" + std::to_string(match.distance) + ",";
    }
    return text;
}

std::vector<FuzzyMatch> brute_force(const std::set<std::string>& terms, const std::string& query,
                                    std::size_t max_distance) {
    std::vector<FuzzyMatch> matches;
    for (const std::string& term : terms) {
        const std::size_t distance = edit_distance(query, term);
        if (distance <= max_distance) {
            matches.push_back({term, distance});
        }
    }
    std::ranges::sort(matches, [](const FuzzyMatch& a, const FuzzyMatch& b) {
        return a.distance != b.distance ? a.distance < b.distance : a.term < b.term;
    });
    return matches;
}

struct Reference {
    std::set<std::string> terms;
    std::map<std::string, TermId> ids;
};

void check_dictionary(const TermDictionary& dictionary, const Reference& reference, std::mt19937& rng,
                      bool check_fuzzy) {
    CHECK_EQ(dictionary.size(), reference.terms.size());

    for (const auto& [term, id] : reference.ids) {
        CHECK_EQ(dictionary.find(term), id);
        CHECK_EQ(std::string(dictionary.text_of(id)), term);
        CHECK_EQ(dictionary.document_frequency(id), static_cast<std::uint32_t>(id * 7 + 1));
        CHECK_EQ(dictionary.first_list(id), static_cast<std::uint32_t>(id * 3));
    }

    std::vector<std::string> prefixes{"", "a", "\xC3", "\xE6\x9D", "common-prefix-", "\xFF", "zzzz"};
    for (int i = 0; i < 6; i++) {
        prefixes.push_back(random_bytes_term(rng, 3, rng() % 2 == 0));
    }
    if (!reference.terms.empty()) {
        const std::string& any = *std::next(reference.terms.begin(), static_cast<long>(rng() % reference.terms.size()));
        prefixes.push_back(any.substr(0, rng() % (any.size() + 1)));
        prefixes.push_back(any);
    }
    for (const std::string& prefix : prefixes) {
        std::vector<TermId> found;
        dictionary.with_prefix(prefix, found);
        std::vector<std::string> expected;
        for (const std::string& term : reference.terms) {
            if (term.starts_with(prefix)) {
                expected.push_back(term);
            }
        }
        std::vector<std::string> actual;
        for (const TermId id : found) {
            actual.emplace_back(dictionary.text_of(id));
        }
        CHECK_EQ(actual, expected);
    }

    for (int i = 0; i < 4; i++) {
        const std::string absent = random_bytes_term(rng, 6, rng() % 2 == 0) + "~";
        CHECK_EQ(dictionary.find(absent) == TermDictionary::none, !reference.terms.contains(absent));
    }

    if (check_fuzzy) {
        for (int i = 0; i < 4; i++) {
            std::string query = random_bytes_term(rng, 6, rng() % 2 == 0);
            if (!reference.terms.empty() && rng() % 2 == 0) {
                query = *std::next(reference.terms.begin(), static_cast<long>(rng() % reference.terms.size()));
                if (!query.empty() && rng() % 2 == 0) {
                    query[rng() % query.size()] = 'q';
                }
            }
            for (std::size_t max_distance = 0; max_distance <= 2; max_distance++) {
                CHECK_EQ(describe(dictionary.fuzzy(query, max_distance)),
                         describe(brute_force(reference.terms, query, max_distance)));
            }
        }
    }
}

void intern_term(TermDictionary& dictionary, Reference& reference, const std::string& term) {
    const bool known = reference.terms.contains(term);
    const TermId id = dictionary.intern(term);
    if (known) {
        CHECK_EQ(id, reference.ids[term]);
    } else {
        reference.terms.insert(term);
        reference.ids[term] = id;
        dictionary.document_frequency(id) = static_cast<std::uint32_t>(id * 7 + 1);
        dictionary.first_list(id) = static_cast<std::uint32_t>(id * 3);
    }
}

void erase_term(TermDictionary& dictionary, Reference& reference, const std::string& term) {
    const auto found = reference.ids.find(term);
    if (found == reference.ids.end()) {
        CHECK(dictionary.find(term) == TermDictionary::none);
        return;
    }
    dictionary.erase(found->second);
    reference.ids.erase(found);
    reference.terms.erase(term);
}

}

TEST(term_dictionary_matches_a_sorted_set_through_random_churn_with_invalid_utf8_terms) {
    for (const unsigned seed : {1u, 2u, 3u}) {
        std::mt19937 rng(seed);
        TermDictionary dictionary;
        Reference reference;
        for (int step = 0; step < 6000; step++) {
            const std::string term = random_bytes_term(rng, 9, rng() % 4 == 0);
            if (rng() % 5 < 2 && !reference.terms.empty()) {
                const std::string victim = rng() % 2 == 0
                                               ? *std::next(reference.terms.begin(),
                                                            static_cast<long>(rng() % reference.terms.size()))
                                               : term;
                erase_term(dictionary, reference, victim);
            } else {
                intern_term(dictionary, reference, term);
            }
            if (step % 400 == 0) {
                check_dictionary(dictionary, reference, rng, true);
            }
        }
        check_dictionary(dictionary, reference, rng, true);

        const std::vector<std::string> remaining(reference.terms.begin(), reference.terms.end());
        for (const std::string& term : remaining) {
            erase_term(dictionary, reference, term);
        }
        check_dictionary(dictionary, reference, rng, true);
        intern_term(dictionary, reference, "again");
        check_dictionary(dictionary, reference, rng, true);
    }
}

TEST(term_dictionary_keeps_every_term_after_hash_growth_and_text_compaction) {
    std::mt19937 rng(77);
    TermDictionary dictionary;
    Reference reference;
    for (int round = 0; round < 3; round++) {
        for (int i = 0; i < 2500; i++) {
            std::string term = "long-shared-stem-" + std::to_string(rng() % 100000) + std::string(60, 'x') +
                               std::to_string(rng());
            intern_term(dictionary, reference, term);
        }
        std::vector<std::string> doomed;
        for (const std::string& term : reference.terms) {
            if (rng() % 10 < 8) {
                doomed.push_back(term);
            }
        }
        for (const std::string& term : doomed) {
            erase_term(dictionary, reference, term);
        }
        check_dictionary(dictionary, reference, rng, false);
    }
}
