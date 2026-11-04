#include "TokenFilter.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <string_view>

namespace {

void to_lower(std::string& word) {
    std::transform(word.begin(), word.end(), word.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
}

}

void LowercaseFilter::apply(std::vector<std::string>& tokens) const {
    for (std::string& token : tokens) {
        to_lower(token);
    }
}

void NormalizeApostropheFilter::apply(std::vector<std::string>& tokens) const {
    constexpr std::string_view curly_apostrophe = "\xE2\x80\x99";

    for (std::string& token : tokens) {
        for (std::size_t pos = token.find(curly_apostrophe); pos != std::string::npos;
             pos = token.find(curly_apostrophe, pos + 1)) {
            token.replace(pos, curly_apostrophe.size(), "'");
        }
    }
}

void EnglishPossessiveFilter::apply(std::vector<std::string>& tokens) const {
    for (std::string& token : tokens) {
        if (token.size() > 2 && (token.ends_with("'s") || token.ends_with("'S"))) {
            token.resize(token.size() - 2);
        }
    }
}

StopWordFilter::StopWordFilter(const std::string& stop_words_path) {
    if (stop_words_path.empty()) {
        return;
    }

    std::ifstream file(stop_words_path);
    if (!file.is_open()) {
        std::cerr << "Warning: Could not open stop word file: " << stop_words_path << '\n';
        return;
    }

    std::string word;
    while (file >> word) {
        to_lower(word);
        stop_words.insert(word);
    }
}

void StopWordFilter::apply(std::vector<std::string>& tokens) const {
    std::erase_if(tokens, [this](const std::string& token) { return stop_words.contains(token); });
}
