#include "TestFramework.h"

#include "EditDistance.h"
#include "FuzzyTerms.h"

#include <algorithm>
#include <random>
#include <set>

namespace {

std::string describe(const std::vector<FuzzyMatch>& matches) {
    std::string text;
    for (const FuzzyMatch& match : matches) {
        text += match.term + ":" + std::to_string(match.distance) + ",";
    }
    return text;
}

std::vector<FuzzyMatch> brute_force(const std::vector<std::string>& terms, const std::string& query,
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
    matches.erase(std::ranges::unique(matches, {}, &FuzzyMatch::term).begin(), matches.end());
    return matches;
}

std::string random_term(std::mt19937& rng, std::size_t max_length) {
    static const std::vector<std::string> alphabet{"a", "b", "c", "e", "\xC3\xA9", "\xC3\xB1", "\xE6\x9D\xB1"};
    std::uniform_int_distribution<std::size_t> length_pick(0, max_length);
    std::uniform_int_distribution<std::size_t> letter_pick(0, alphabet.size() - 1);
    std::string term;
    const std::size_t length = length_pick(rng);
    for (std::size_t i = 0; i < length; i++) {
        term += alphabet[letter_pick(rng)];
    }
    return term;
}

}

TEST(edit_distance_counts_insert_delete_and_substitute) {
    CHECK_EQ(edit_distance("kitten", "sitting"), static_cast<std::size_t>(3));
    CHECK_EQ(edit_distance("serch", "search"), static_cast<std::size_t>(1));
    CHECK_EQ(edit_distance("engin", "engine"), static_cast<std::size_t>(1));
    CHECK_EQ(edit_distance("search", "serch"), static_cast<std::size_t>(1));
    CHECK_EQ(edit_distance("cat", "cut"), static_cast<std::size_t>(1));
    CHECK_EQ(edit_distance("same", "same"), static_cast<std::size_t>(0));
}

TEST(edit_distance_treats_a_transposition_as_two_edits) {
    CHECK_EQ(edit_distance("ab", "ba"), static_cast<std::size_t>(2));
    CHECK_EQ(edit_distance("search", "saerch"), static_cast<std::size_t>(2));
}

TEST(edit_distance_handles_empty_strings) {
    CHECK_EQ(edit_distance("", ""), static_cast<std::size_t>(0));
    CHECK_EQ(edit_distance("", "abc"), static_cast<std::size_t>(3));
    CHECK_EQ(edit_distance("abc", ""), static_cast<std::size_t>(3));
}

TEST(edit_distance_counts_code_points_not_bytes) {
    CHECK_EQ(edit_distance("caf\xC3\xA9", "cafe"), static_cast<std::size_t>(1));
    CHECK_EQ(edit_distance("\xE6\x9D\xB1\xE4\xBA\xAC", "\xE6\x9D\xB1\xE5\xA4\xA7"), static_cast<std::size_t>(1));
    CHECK_EQ(edit_distance("\xF0\x9F\x98\x80", "a"), static_cast<std::size_t>(1));
    CHECK_EQ(edit_distance("\xC3\xA9", ""), static_cast<std::size_t>(1));
}

TEST(edit_distance_with_a_limit_returns_exact_values_within_the_limit) {
    CHECK_EQ(edit_distance("kitten", "sitting", 3), static_cast<std::size_t>(3));
    CHECK_EQ(edit_distance("kitten", "sitting", 5), static_cast<std::size_t>(3));
    CHECK_EQ(edit_distance("same", "same", 0), static_cast<std::size_t>(0));
}

TEST(edit_distance_with_a_limit_exits_early_when_exceeded) {
    CHECK_EQ(edit_distance("kitten", "sitting", 2), static_cast<std::size_t>(3));
    CHECK_EQ(edit_distance("abc", "xyz", 1), static_cast<std::size_t>(2));
    CHECK_EQ(edit_distance("a", "abcdefgh", 2), static_cast<std::size_t>(3));
    CHECK_EQ(edit_distance("abcdefgh", "a", 0), static_cast<std::size_t>(1));
    CHECK_EQ(edit_distance("abcdefghij", "klmnopqrst", 2), static_cast<std::size_t>(3));
}

TEST(edit_distance_with_a_limit_agrees_with_the_full_distance) {
    std::mt19937 rng(7);
    for (int round = 0; round < 500; round++) {
        const std::string a = random_term(rng, 7);
        const std::string b = random_term(rng, 7);
        const std::size_t full = edit_distance(a, b);
        for (std::size_t limit = 0; limit <= 3; limit++) {
            CHECK_EQ(edit_distance(a, b, limit), std::min(full, limit + 1));
        }
    }
}

TEST(fuzzy_max_distance_grows_with_term_length) {
    CHECK_EQ(fuzzy_max_distance(0), static_cast<std::size_t>(0));
    CHECK_EQ(fuzzy_max_distance(2), static_cast<std::size_t>(0));
    CHECK_EQ(fuzzy_max_distance(3), static_cast<std::size_t>(1));
    CHECK_EQ(fuzzy_max_distance(5), static_cast<std::size_t>(1));
    CHECK_EQ(fuzzy_max_distance(6), static_cast<std::size_t>(2));
    CHECK_EQ(fuzzy_max_distance(20), static_cast<std::size_t>(2));
}

TEST(fuzzy_score_factor_ranks_closer_matches_higher) {
    CHECK_EQ(fuzzy_score_factor(0), 1.0);
    CHECK(fuzzy_score_factor(0) > fuzzy_score_factor(1));
    CHECK(fuzzy_score_factor(1) > fuzzy_score_factor(2));
    CHECK(fuzzy_score_factor(2) > 0.0);
}

TEST(fuzzy_index_finds_the_pillar_example) {
    const FuzzyTerms index(std::vector<std::string>{"search", "engine", "searcher", "banana", "seal"});
    CHECK_EQ(describe(index.candidates("serch", 1)), std::string("search:1,"));
    CHECK_EQ(describe(index.candidates("engin", 1)), std::string("engine:1,"));
}

TEST(fuzzy_index_returns_the_exact_term_at_distance_zero) {
    FuzzyTerms index;
    index.add("search");
    index.add("search");
    index.add("sea");
    CHECK_EQ(index.size(), static_cast<std::size_t>(2));
    CHECK_EQ(describe(index.candidates("search", 0)), std::string("search:0,"));
    CHECK_EQ(describe(index.candidates("missing", 0)), std::string(""));
}

TEST(fuzzy_index_orders_matches_by_distance_then_term) {
    const FuzzyTerms index(std::vector<std::string>{"cat", "cut", "cart", "car", "dog"});
    CHECK_EQ(describe(index.candidates("cat", 1)), std::string("cat:0,car:1,cart:1,cut:1,"));
}

TEST(fuzzy_index_matches_accented_terms_across_code_points) {
    const FuzzyTerms index(std::vector<std::string>{"caf\xC3\xA9", "cafes", "coffee"});
    CHECK_EQ(describe(index.candidates("cafe", 1)), std::string("cafes:1,caf\xC3\xA9:1,"));
    CHECK_EQ(describe(index.candidates("caf\xC3\xA9", 0)), std::string("caf\xC3\xA9:0,"));
}

TEST(fuzzy_index_handles_empty_and_very_short_terms) {
    const FuzzyTerms index(std::vector<std::string>{"", "a", "ab", "abc", "b"});
    CHECK_EQ(describe(index.candidates("", 0)), std::string(":0,"));
    CHECK_EQ(describe(index.candidates("", 1)), std::string(":0,a:1,b:1,"));
    CHECK_EQ(describe(index.candidates("a", 1)), std::string("a:0,:1,ab:1,b:1,"));
    CHECK_EQ(describe(FuzzyTerms().candidates("abc", 2)), std::string(""));
}

TEST(fuzzy_index_agrees_with_a_brute_force_scan) {
    std::mt19937 rng(42);
    std::vector<std::string> terms;
    for (int i = 0; i < 400; i++) {
        terms.push_back(random_term(rng, 8));
    }
    const FuzzyTerms index(terms);

    for (int round = 0; round < 150; round++) {
        const std::string query = random_term(rng, 8);
        for (std::size_t max_distance = 0; max_distance <= 2; max_distance++) {
            CHECK_EQ(describe(index.candidates(query, max_distance)),
                     describe(brute_force(terms, query, max_distance)));
        }
    }
}

TEST(fuzzy_index_agrees_with_a_brute_force_scan_after_adds_and_removes) {
    std::mt19937 rng(7);
    FuzzyTerms index;
    std::set<std::string> live;

    for (int step = 0; step < 900; step++) {
        const std::string term = random_term(rng, 6);
        if (rng() % 3 == 0) {
            index.remove(term);
            live.erase(term);
        } else {
            index.add(term);
            live.insert(term);
        }

        if (step % 30 == 0) {
            const std::vector<std::string> terms(live.begin(), live.end());
            CHECK_EQ(index.size(), terms.size());
            for (int round = 0; round < 6; round++) {
                const std::string query = random_term(rng, 6);
                for (std::size_t max_distance = 0; max_distance <= 2; max_distance++) {
                    CHECK_EQ(describe(index.candidates(query, max_distance)),
                             describe(brute_force(terms, query, max_distance)));
                }
            }
        }
    }

    for (const std::string& term : std::vector<std::string>(live.begin(), live.end())) {
        index.remove(term);
    }
    CHECK_EQ(index.size(), static_cast<std::size_t>(0));
    CHECK_EQ(describe(index.candidates("abc", 2)), std::string(""));

    index.add("abc");
    CHECK_EQ(describe(index.candidates("abd", 1)), std::string("abc:1,"));
}

TEST(fuzzy_index_agrees_with_a_brute_force_scan_on_longer_terms_and_larger_distances) {
    std::mt19937 rng(99);
    std::vector<std::string> terms;
    for (int i = 0; i < 500; i++) {
        terms.push_back(random_term(rng, 14));
    }
    const FuzzyTerms index(terms);

    for (int round = 0; round < 120; round++) {
        const std::string query = random_term(rng, 14);
        for (std::size_t max_distance = 0; max_distance <= 4; max_distance++) {
            CHECK_EQ(describe(index.candidates(query, max_distance)),
                     describe(brute_force(terms, query, max_distance)));
        }
    }
}

TEST(fuzzy_index_finds_terms_that_differ_only_at_the_ends) {
    const std::vector<std::string> terms{"search", "earch", "searc", "xsearch", "searchx", "xsearchx", "sarch",
                                         "searchxx", "xxsearch", "seaxrch"};
    const FuzzyTerms index(terms);

    for (std::size_t max_distance = 0; max_distance <= 3; max_distance++) {
        CHECK_EQ(describe(index.candidates("search", max_distance)), describe(brute_force(terms, "search", max_distance)));
        CHECK_EQ(describe(index.candidates("earc", max_distance)), describe(brute_force(terms, "earc", max_distance)));
        CHECK_EQ(describe(index.candidates("", max_distance)), describe(brute_force(terms, "", max_distance)));
    }
}
