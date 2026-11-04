#include "Tokenizer.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>

Tokenizer::Tokenizer(const std::string& stop_words_path) {
    if (!stop_words_path.empty()) {
        load_stop_words(stop_words_path);
    }
}

void Tokenizer::load_stop_words(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Warning: Could not open stop word file: " << filepath << '\n';
        return;
    }

    std::string word;
    while (file >> word) {
        std::transform(word.begin(), word.end(), word.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        });
        stop_words.insert(word);
    }
}

std::vector<std::string> Tokenizer::tokenize(const std::string& text) const {
    std::vector<std::string> tokens;
    std::string current_token;

    for (char c : text) {
        const unsigned char u_c = static_cast<unsigned char>(c);
        if (std::isalnum(u_c)) {
            current_token += static_cast<char>(std::tolower(u_c));
        } else if (!current_token.empty()) {
            if (stop_words.find(current_token) == stop_words.end()) {
                tokens.push_back(current_token);
            }
            current_token.clear();
        }
    }

    if (!current_token.empty() && stop_words.find(current_token) == stop_words.end()) {
        tokens.push_back(current_token);
    }

    return tokens;
}
