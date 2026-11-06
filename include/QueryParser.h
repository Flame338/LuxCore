#pragma once

#include "Analyzer.h"
#include "Query.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

struct ParseError {
    std::string message;
    std::size_t offset;
};

struct ParseResult {
    std::unique_ptr<Query> query;
    std::optional<ParseError> error;

    bool ok() const { return !error.has_value(); }
};

class QueryParser {
private:
    const Analyzer& analyzer;

public:
    explicit QueryParser(const Analyzer& analyzer) : analyzer(analyzer) {}

    ParseResult parse(const std::string& text) const;
};
