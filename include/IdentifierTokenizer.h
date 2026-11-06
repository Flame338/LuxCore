#pragma once

#include "Analyzer.h"
#include "Tokenizer.h"

#include <string>
#include <vector>

struct IdentifierOptions {
    bool keep_original = false;
    bool split_letters_digits = false;
};

class IdentifierTokenizer : public Tokenizer {
private:
    StandardTokenizer base;
    IdentifierOptions options;

public:
    explicit IdentifierTokenizer(IdentifierOptions options = {});

    std::vector<std::string> tokenize(const std::string& text) const override;
};

Analyzer make_identifier_analyzer(IdentifierOptions options = {});
