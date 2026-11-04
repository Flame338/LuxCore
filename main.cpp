#include <unordered_set>
#include <string>
#include <iostream>
#include <vector>


class Tokenizer {
    private:
        std::unordered_set<std::string> stop_words;

    public:
        Tokenizer() {
            stop_words = {"the", "is", "at", "which", "and", "a", "an", "in", "to", "of"};
        }

        std::vector<std::string> tokenize(const std::string& text) {
            std::vector<std::string> tokens;
            std::string current_token = "";

            for (char c : text) {
                unsigned char u_c = static_cast<unsigned char>(c);
                if (std::isalnum(u_c)) {
                    current_token += static_cast<char>(std::tolower(u_c));
                } else if (!current_token.empty()) {
                    // Assigning non-alphanumeric character to act as delimiters
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
};

int main() {
    Tokenizer tokenizer;
    std::string test = "The quick brown fox jumped over the brown bear!";

    std::vector<std::string> result = tokenizer.tokenize(test);

    std::cout << "Tokens: \n";
    for (const auto& token : result) {
        std::cout << " - " << token << "\n";
    }

    return 0;
}