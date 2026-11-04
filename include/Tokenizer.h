#pragma once

#include <string>
#include <vector>

class Tokenizer {
public:
    virtual ~Tokenizer() = default;

    virtual std::vector<std::string> tokenize(const std::string& text) const = 0;
};

class StandardTokenizer : public Tokenizer {
public:
    std::vector<std::string> tokenize(const std::string& text) const override;
};
