#pragma once

#include "Analyzer.h"
#include "TermTrie.h"
#include "Query.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct PlanPhrase {
    std::vector<PhraseTerm> terms;
    std::size_t unit;
};

class MatchPlan {
public:
    const Analyzer& analyzer() const;
    std::optional<std::size_t> unit_of(const std::string& term) const;
    const std::vector<PlanPhrase>& phrases() const;

private:
    friend class MatchPlanner;
    friend class TextPlan;
    explicit MatchPlan(const Analyzer& analyzer);

    const Analyzer* plan_analyzer;
    std::unordered_map<std::string, std::size_t> unit_by_term;
    std::vector<PlanPhrase> phrase_list;
};

class TextPlan : public MatchPlan {
public:
    struct Word {
        std::string term;
        std::vector<FuzzyMatch> fuzzy;
    };

    struct Prefix {
        std::string term;
        std::vector<std::string> completions;
    };

    const std::vector<Word>& words() const;
    const std::optional<Prefix>& prefix() const;
    const std::optional<std::string>& field() const;

private:
    friend class MatchPlanner;
    TextPlan(const Analyzer& analyzer, std::optional<std::string> field, std::vector<Word> words,
             std::optional<Prefix> prefix);

    std::vector<Word> word_list;
    std::optional<Prefix> open_prefix;
    std::optional<std::string> scope_field;
};

MatchPlan make_match_plan(const Analyzer& analyzer, const Query& query,
                          std::optional<std::string_view> field = std::nullopt);
