#pragma once

#include "Token.h"

#include <string>
#include <string_view>
#include <vector>

class Tokenizer {
public:
    virtual ~Tokenizer() = default;

    virtual std::vector<Token> tokenize_tokens(std::string_view text) const = 0;

    std::vector<std::string> tokenize(const std::string& text) const {
        return terms_of(tokenize_tokens(text));
    }
};

class StandardTokenizer : public Tokenizer {
public:
    std::vector<Token> tokenize_tokens(std::string_view text) const override;
};
