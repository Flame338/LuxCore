#include "Tokenizer.h"

#include <cctype>

namespace {

bool is_word_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c));
}

std::size_t apostrophe_length(const std::string& text, std::size_t i) {
    if (text[i] == '\'') {
        return 1;
    }
    if (text.compare(i, 3, "\xE2\x80\x99") == 0) {
        return 3;
    }
    return 0;
}

}

std::vector<std::string> StandardTokenizer::tokenize(const std::string& text) const {
    std::vector<std::string> tokens;
    std::string current_token;

    for (std::size_t i = 0; i < text.size(); i++) {
        const std::size_t apostrophe = apostrophe_length(text, i);
        const bool is_inner_apostrophe = apostrophe > 0 && !current_token.empty() &&
                                         i + apostrophe < text.size() &&
                                         is_word_char(text[i + apostrophe]);

        if (is_word_char(text[i])) {
            current_token += text[i];
        } else if (is_inner_apostrophe) {
            current_token.append(text, i, apostrophe);
            i += apostrophe - 1;
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
