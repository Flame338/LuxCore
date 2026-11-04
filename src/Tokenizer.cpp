#include "Tokenizer.h"

#include <cctype>

std::vector<std::string> StandardTokenizer::tokenize(const std::string& text) const {
    std::vector<std::string> tokens;
    std::string current_token;

    for (char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            current_token += c;
        } else if (!current_token.empty()) {
            tokens.push_back(std::move(current_token));
            current_token.clear();
        }
    }

    if (!current_token.empty()) {
        tokens.push_back(std::move(current_token));
    }

    return tokens;
}
