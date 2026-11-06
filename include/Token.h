#pragma once

#include <cstddef>
#include <string>
#include <vector>

struct Token {
    std::string term;
    std::size_t position;
    std::size_t start_offset;
    std::size_t end_offset;

    bool operator==(const Token&) const = default;
};

inline std::vector<std::string> terms_of(const std::vector<Token>& tokens) {
    std::vector<std::string> terms;
    terms.reserve(tokens.size());
    for (const Token& token : tokens) {
        terms.push_back(token.term);
    }
    return terms;
}
