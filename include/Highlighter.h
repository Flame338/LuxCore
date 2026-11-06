#pragma once

#include "Analyzer.h"
#include "Document.h"
#include "EditDistance.h"
#include "Query.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

struct HighlightOptions {
    std::size_t length = 160;
    std::string start_marker = "<em>";
    std::string end_marker = "</em>";
    std::string ellipsis = "...";
    Fuzziness fuzziness = Fuzziness::Off;
};

class Highlighter {
private:
    const Analyzer& analyzer;
    HighlightOptions options;

public:
    explicit Highlighter(const Analyzer& analyzer, HighlightOptions options = {});

    std::string snippet(const std::string& stored_text, const std::string& query_string) const;
    std::string snippet(const Document& document, std::string_view field,
                        const std::string& query_string) const;
    std::string snippet(const std::string& stored_text, const Query& query,
                        std::optional<std::string_view> field = std::nullopt) const;
    std::string snippet(const Document& document, std::string_view field, const Query& query) const;
};
