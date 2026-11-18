#include "Analyzer.h"

#include <string_view>

namespace {

constexpr std::uint64_t analysis_revision = 1;

constexpr std::string_view probe_text =
    "The QUICK brown fox's den. John\xE2\x80\x99s caf\xC3\xA9, r\xC3\xA9sum\xC3\xA9 and na"
    "\xC3\xAFve Stra\xC3\x9F" "e e\xCC\x81te\xCC\x81 \xC3\x85ngstr\xC3\xB6m \xD0\xB4\xD0\xBB\xD1\x8F"
    " \xD0\xA2\xD0\x9E \xCE\xA4\xCE\x9F \xCF\x83\xCE\xBF\xCF\x86\xCE\xAF\xCE\xB1 \xE6\x9D\xB1\xE4\xBA\xAC\xE5\xA4\xA7\xE5\xAD\xA6"
    " \xE3\x81\x93\xE3\x82\x93\xE3\x81\xAB\xE3\x81\xA1\xE3\x81\xAF \xEC\x95\x88\xEB\x85\x95"
    " wi-fi don't it's rock'n'roll getUserById parse_http_request HTTPServer2 foo.bar v1.2.3 x86_64 C++ #tag @user 3.14 1,000 "
    "\xF0\x9F\x98\x80 \xC2\xBD \xE2\x85\xA0 \xEF\xBC\xA6\xEF\xBD\x95\xEF\xBD\x8C\xEF\xBD\x8C"
    " \xC4\xB0stanbul \xD8\xA7\xD9\x84\xD8\xB9\xD8\xB1\xD8\xA8\xD9\x8A\xD8\xA9";

constexpr std::uint64_t fnv_offset = 0xcbf29ce484222325;
constexpr std::uint64_t fnv_prime = 0x100000001b3;

void mix(std::uint64_t& hash, std::string_view bytes) {
    for (const char byte : bytes) {
        hash = (hash ^ static_cast<unsigned char>(byte)) * fnv_prime;
    }
}

void mix(std::uint64_t& hash, std::uint64_t number) {
    for (int shift = 0; shift < 64; shift += 8) {
        hash = (hash ^ ((number >> shift) & 0xFF)) * fnv_prime;
    }
}

}

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

std::vector<Token> Analyzer::analyze_tokens(std::string_view text) const {
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

std::uint64_t Analyzer::fingerprint() const {
    std::uint64_t hash = fnv_offset;
    mix(hash, analysis_revision);
    for (const Token& token : analyze_tokens(probe_text)) {
        mix(hash, static_cast<std::uint64_t>(token.position));
        mix(hash, static_cast<std::uint64_t>(token.term.size()));
        mix(hash, token.term);
    }
    mix(hash, static_cast<std::uint64_t>(filters.size()));
    for (const auto& filter : filters) {
        const std::string settings = filter->settings();
        mix(hash, static_cast<std::uint64_t>(settings.size()));
        mix(hash, settings);
    }
    return hash;
}

Analyzer make_standard_analyzer(const std::string& stop_words_path) {
    Analyzer analyzer(std::make_unique<StandardTokenizer>());
    analyzer.add_filter(std::make_unique<UnicodeFoldingFilter>());
    analyzer.add_filter(std::make_unique<NormalizeApostropheFilter>());
    analyzer.add_filter(std::make_unique<EnglishPossessiveFilter>());
    analyzer.add_filter(std::make_unique<StopWordFilter>(stop_words_path));
    return analyzer;
}
