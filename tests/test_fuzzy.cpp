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

TEST(edit_distance_counts_code_points_not_bytes) {
    CHECK_EQ(edit_distance("caf\xC3\xA9", "cafe"), static_cast<std::size_t>(1));
    CHECK_EQ(edit_distance("\xE6\x9D\xB1\xE4\xBA\xAC", "\xE6\x9D\xB1\xE5\xA4\xA7"), static_cast<std::size_t>(1));
    CHECK_EQ(edit_distance("\xF0\x9F\x98\x80", "a"), static_cast<std::size_t>(1));
    CHECK_EQ(edit_distance("\xC3\xA9", ""), static_cast<std::size_t>(1));
}

TEST(edit_distance_with_a_limit_exits_early_when_exceeded) {
    CHECK_EQ(edit_distance("kitten", "sitting", 2), static_cast<std::size_t>(3));
    CHECK_EQ(edit_distance("abc", "xyz", 1), static_cast<std::size_t>(2));
    CHECK_EQ(edit_distance("a", "abcdefgh", 2), static_cast<std::size_t>(3));
    CHECK_EQ(edit_distance("abcdefgh", "a", 0), static_cast<std::size_t>(1));
    CHECK_EQ(edit_distance("abcdefghij", "klmnopqrst", 2), static_cast<std::size_t>(3));
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
