#include "MatchPlanner.h"

#include "Utf8.h"

#include <algorithm>
#include <utility>

namespace {

constexpr std::size_t max_fuzzy_candidates = 20;

std::size_t max_completions(std::size_t prefix_length) {
    if (prefix_length <= 1) {
        return 0;
    }
    if (prefix_length == 2) {
        return 4;
    }
    return 32;
}

std::vector<std::string> kept_completions(const Scope& scope, const std::string& prefix) {
    const std::size_t limit = max_completions(utf8::count_code_points(prefix));
    if (limit == 0) {
        return {};
    }

    std::vector<TermPostings> completions = scope.terms_with_prefix(prefix);
    std::erase_if(completions, [&prefix](const TermPostings& match) { return match.term == prefix; });

    if (completions.size() > limit) {
        std::partial_sort(completions.begin(), completions.begin() + limit, completions.end(), more_common);
        completions.resize(limit);
    }

    std::vector<std::string> kept;
    kept.reserve(completions.size());
    for (const TermPostings& completion : completions) {
        kept.emplace_back(completion.term);
    }
    return kept;
}

std::vector<FuzzyMatch> fuzzy_candidates(const InvertedIndex& index, const std::string& term) {
    const std::size_t max_distance = fuzzy_max_distance(utf8::count_code_points(term));
    std::vector<FuzzyMatch> matches;
    if (max_distance == 0) {
        return matches;
    }
    for (FuzzyMatch& match : index.fuzzy_terms(term, max_distance)) {
        if (match.distance > 0) {
            matches.push_back(std::move(match));
        }
    }
    auto frequency = [&index](const FuzzyMatch& match) {
        const std::vector<Posting>* postings = index.get_postings(match.term);
        return postings ? postings->size() : std::size_t{0};
    };
    std::ranges::stable_sort(matches, [&](const FuzzyMatch& a, const FuzzyMatch& b) {
        return a.distance != b.distance ? a.distance < b.distance : frequency(a) > frequency(b);
    });
    if (matches.size() > max_fuzzy_candidates) {
        matches.resize(max_fuzzy_candidates);
    }
    return matches;
}

bool same_phrase(const std::vector<PhraseTerm>& a, const std::vector<PhraseTerm>& b) {
    return std::ranges::equal(a, b, [](const PhraseTerm& left, const PhraseTerm& right) {
        return left.term == right.term && left.position == right.position;
    });
}

bool in_scope(const std::optional<std::string>& leaf_field, const std::optional<std::string_view>& field) {
    return !field || !leaf_field || *leaf_field == *field;
}

struct NearestSource {
    std::size_t distance;
    std::size_t unit;
};

}

bool more_common(const TermPostings& a, const TermPostings& b) {
    if (a.postings->size() != b.postings->size()) {
        return a.postings->size() > b.postings->size();
    }
    return a.term < b.term;
}

MatchPlan::MatchPlan(const Analyzer& analyzer) : plan_analyzer(&analyzer) {}

const Analyzer& MatchPlan::analyzer() const {
    return *plan_analyzer;
}

std::optional<std::size_t> MatchPlan::unit_of(const std::string& term) const {
    const auto found = unit_by_term.find(term);
    if (found == unit_by_term.end()) {
        return std::nullopt;
    }
    return found->second;
}

const std::vector<PlanPhrase>& MatchPlan::phrases() const {
    return phrase_list;
}

TextPlan::TextPlan(const Analyzer& analyzer, std::optional<std::string> field, std::vector<Word> words,
                   std::optional<Prefix> prefix)
    : MatchPlan(analyzer), word_list(std::move(words)), open_prefix(std::move(prefix)),
      scope_field(std::move(field)) {
    std::size_t units = 0;
    for (const Word& word : word_list) {
        if (unit_by_term.try_emplace(word.term, units).second) {
            units++;
        }
    }

    if (open_prefix) {
        const auto [slot, inserted] = unit_by_term.try_emplace(open_prefix->term, units);
        const std::size_t prefix_unit = slot->second;
        if (inserted) {
            units++;
        }
        for (const std::string& completion : open_prefix->completions) {
            unit_by_term.try_emplace(completion, prefix_unit);
        }
    }

    std::unordered_map<std::string, NearestSource> nearest;
    for (const Word& word : word_list) {
        const std::size_t unit = unit_by_term.at(word.term);
        for (const FuzzyMatch& match : word.fuzzy) {
            const auto [slot, inserted] = nearest.try_emplace(match.term, NearestSource{match.distance, unit});
            if (!inserted && match.distance < slot->second.distance) {
                slot->second = {match.distance, unit};
            }
        }
    }
    for (const auto& [term, source] : nearest) {
        unit_by_term.try_emplace(term, source.unit);
    }
}

const std::vector<TextPlan::Word>& TextPlan::words() const {
    return word_list;
}

const std::optional<TextPlan::Prefix>& TextPlan::prefix() const {
    return open_prefix;
}

const std::optional<std::string>& TextPlan::field() const {
    return scope_field;
}

TextPlan MatchPlanner::for_text(const Scope& scope, const Analyzer& analyzer, Fuzziness fuzziness,
                                const std::string& query_string) {
    AnalyzedQuery query = analyzer.analyze_query(query_string);

    std::vector<TextPlan::Word> words;
    words.reserve(query.terms.size());
    for (std::string& term : query.terms) {
        std::vector<FuzzyMatch> fuzzy;
        if (fuzziness == Fuzziness::On) {
            fuzzy = fuzzy_candidates(scope.index, term);
        }
        words.push_back({std::move(term), std::move(fuzzy)});
    }

    std::optional<TextPlan::Prefix> prefix;
    if (query.prefix) {
        std::vector<std::string> completions = kept_completions(scope, *query.prefix);
        prefix = TextPlan::Prefix{std::move(*query.prefix), std::move(completions)};
    }

    std::optional<std::string> field;
    if (scope.field) {
        field = std::string(*scope.field);
    }
    return TextPlan(analyzer, std::move(field), std::move(words), std::move(prefix));
}

MatchPlan MatchPlanner::for_query(const Analyzer& analyzer, const Query& query,
                                  std::optional<std::string_view> field) {
    QueryLeaves leaves;
    query.add_leaves(leaves);

    MatchPlan plan(analyzer);
    std::size_t units = 0;
    for (const LeafTerm& leaf : leaves.terms) {
        if (in_scope(leaf.field, field) && plan.unit_by_term.try_emplace(leaf.term, units).second) {
            units++;
        }
    }
    for (const LeafPhrase& leaf : leaves.phrases) {
        const bool known = std::ranges::any_of(
            plan.phrase_list, [&leaf](const PlanPhrase& phrase) { return same_phrase(phrase.terms, leaf.terms); });
        if (in_scope(leaf.field, field) && !leaf.terms.empty() && !known) {
            plan.phrase_list.push_back({leaf.terms, units++});
        }
    }
    return plan;
}

MatchPlan make_match_plan(const Analyzer& analyzer, const Query& query, std::optional<std::string_view> field) {
    return MatchPlanner::for_query(analyzer, query, field);
}
