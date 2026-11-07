#pragma once

#include "Analyzer.h"
#include "EditDistance.h"
#include "InvertedIndex.h"
#include "MatchPlan.h"
#include "Query.h"
#include "TermScorer.h"

#include <optional>
#include <string>
#include <string_view>

class MatchPlanner {
public:
    static TextPlan for_text(const Scope& scope, const Analyzer& analyzer, Fuzziness fuzziness,
                             const std::string& query_string);
    static MatchPlan for_query(const Analyzer& analyzer, const Query& query,
                               std::optional<std::string_view> field);
};

bool more_common(const TermPostings& a, const TermPostings& b);
