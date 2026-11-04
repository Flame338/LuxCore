#pragma once

#include "TokenFilter.h"
#include "Tokenizer.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

struct AnalyzedQuery {
    std::vector<std::string> terms;
    std::optional<std::string> prefix;
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
};

Analyzer make_standard_analyzer(const std::string& stop_words_path = "");
