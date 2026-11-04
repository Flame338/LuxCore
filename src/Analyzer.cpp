#include "Analyzer.h"

Analyzer::Analyzer(std::unique_ptr<Tokenizer> tokenizer) : tokenizer(std::move(tokenizer)) {}

Analyzer& Analyzer::add_filter(std::unique_ptr<TokenFilter> filter) {
    filters.push_back(std::move(filter));
    return *this;
}

std::vector<std::string> Analyzer::analyze(const std::string& text) const {
    std::vector<std::string> tokens = tokenizer->tokenize(text);
    for (const auto& filter : filters) {
        filter->apply(tokens);
    }
    return tokens;
}

Analyzer make_standard_analyzer(const std::string& stop_words_path) {
    Analyzer analyzer(std::make_unique<StandardTokenizer>());
    analyzer.add_filter(std::make_unique<LowercaseFilter>());
    analyzer.add_filter(std::make_unique<NormalizeApostropheFilter>());
    analyzer.add_filter(std::make_unique<EnglishPossessiveFilter>());
    analyzer.add_filter(std::make_unique<StopWordFilter>(stop_words_path));
    return analyzer;
}
