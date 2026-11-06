#include "Analyzer.h"

Analyzer::Analyzer(std::unique_ptr<Tokenizer> tokenizer) : tokenizer(std::move(tokenizer)) {}

Analyzer& Analyzer::add_filter(std::unique_ptr<TokenFilter> filter) {
    filters.push_back(std::move(filter));
    return *this;
}

std::vector<std::string> Analyzer::analyze(const std::string& text) const {
    return terms_of(analyze_tokens(text));
}

AnalyzedQuery Analyzer::analyze_query(const std::string& text) const {
    AnalyzedTokenQuery query = analyze_query_tokens(text);

    AnalyzedQuery result{terms_of(query.tokens), std::nullopt};
    if (query.prefix) {
        result.prefix = std::move(query.prefix->term);
    }
    return result;
}

std::vector<Token> Analyzer::analyze_tokens(const std::string& text) const {
    std::vector<Token> tokens = tokenizer->tokenize_tokens(text);
    for (const auto& filter : filters) {
        filter->filter(tokens);
    }
    return tokens;
}

AnalyzedTokenQuery Analyzer::analyze_query_tokens(const std::string& text) const {
    std::vector<Token> tokens = tokenizer->tokenize_tokens(text);
    const bool ends_inside_word = tokenizer->tokenize_tokens(text + 'x').size() == tokens.size();

    std::vector<Token> open_word;
    if (ends_inside_word) {
        open_word.push_back(std::move(tokens.back()));
        tokens.pop_back();
    }

    for (const auto& filter : filters) {
        filter->filter(tokens);
        if (filter->applies_to_partial_words()) {
            filter->filter(open_word);
        }
    }

    AnalyzedTokenQuery query{std::move(tokens), std::nullopt};
    if (!open_word.empty()) {
        query.prefix = std::move(open_word.front());
    }
    return query;
}

Analyzer make_standard_analyzer(const std::string& stop_words_path) {
    Analyzer analyzer(std::make_unique<StandardTokenizer>());
    analyzer.add_filter(std::make_unique<LowercaseFilter>());
    analyzer.add_filter(std::make_unique<UnicodeFoldingFilter>());
    analyzer.add_filter(std::make_unique<NormalizeApostropheFilter>());
    analyzer.add_filter(std::make_unique<EnglishPossessiveFilter>());
    analyzer.add_filter(std::make_unique<StopWordFilter>(stop_words_path));
    return analyzer;
}
