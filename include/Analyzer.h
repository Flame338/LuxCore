#pragma once

#include "TokenFilter.h"
#include "Tokenizer.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct AnalyzedQuery {
    std::vector<std::string> terms;
    std::optional<std::string> prefix;
};

struct AnalyzedTokenQuery {
    std::vector<Token> tokens;
    std::optional<Token> prefix;
};

class Analyzer {
private:
    std::unique_ptr<Tokenizer> tokenizer;
    std::vector<std::unique_ptr<TokenFilter>> filters;

public:
    explicit Analyzer(std::unique_ptr<Tokenizer> tokenizer);

    Analyzer& add_filter(std::unique_ptr<TokenFilter> filter);

    std::vector<std::string> analyze(const std::string& text) const;
    AnalyzedQuery analyze_query(const std::string& text) const;
    std::vector<Token> analyze_tokens(std::string_view text) const;
    AnalyzedTokenQuery analyze_query_tokens(const std::string& text) const;
};

Analyzer make_standard_analyzer(const std::string& stop_words_path = "");
