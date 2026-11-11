#pragma once

#include "Document.h"
#include "MatchPlan.h"

#include <cstddef>
#include <string>
#include <string_view>

struct HighlightOptions {
    std::size_t length = 160;
    std::string start_marker = "<em>";
    std::string end_marker = "</em>";
    std::string ellipsis = "...";
};

class Highlighter {
private:
    HighlightOptions options;

public:
    explicit Highlighter(HighlightOptions options = {});

    std::string snippet(std::string_view stored_text, const MatchPlan& plan) const;
    std::string snippet(const Document& document, std::string_view field, const MatchPlan& plan) const;
};
