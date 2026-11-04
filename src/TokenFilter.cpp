#include "TokenFilter.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>

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
