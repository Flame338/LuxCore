#pragma once

#include <string>
#include <unordered_set>
#include <vector>

class Tokenizer {
private:
    std::unordered_set<std::string> stop_words;

    void load_stop_words(const std::string& filepath);

public:
    explicit Tokenizer(const std::string& stop_words_path = "");

    std::vector<std::string> tokenize(const std::string& text) const;
};
