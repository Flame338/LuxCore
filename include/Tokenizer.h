#pragma once

#include "Token.h"

#include <string>
#include <vector>

class Tokenizer {
public:
    virtual ~Tokenizer() = default;

    virtual std::vector<Token> tokenize_tokens(const std::string& text) const = 0;

    std::vector<std::string> tokenize(const std::string& text) const {
        return terms_of(tokenize_tokens(text));
    }
};

class StandardTokenizer : public Tokenizer {
public:
    std::vector<Token> tokenize_tokens(const std::string& text) const override;
};
