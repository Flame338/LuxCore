#include "Tokenizer.h"

#include "CodePointRanges.h"
#include "Utf8.h"

#include <cctype>

namespace {

enum class CharClass { word, separator, standalone };

struct ClassRange {
    char32_t first;
    char32_t last;
    CharClass kind;
};

constexpr char32_t curly_apostrophe = 0x2019;

constexpr ClassRange class_ranges[] = {
    {0x0080, 0x00A9, CharClass::separator},
    {0x00AB, 0x00B4, CharClass::separator},
    {0x00B6, 0x00B9, CharClass::separator},
    {0x00BB, 0x00BF, CharClass::separator},
    {0x00D7, 0x00D7, CharClass::separator},
    {0x00F7, 0x00F7, CharClass::separator},
    {0x2000, 0x2BFF, CharClass::separator},
    {0x3000, 0x303F, CharClass::separator},
    {0x3040, 0x309F, CharClass::standalone},
    {0x3400, 0x4DBF, CharClass::standalone},
    {0x4E00, 0x9FFF, CharClass::standalone},
    {0xE000, 0xF8FF, CharClass::separator},
    {0xF900, 0xFAFF, CharClass::standalone},
    {0xFE30, 0xFE6F, CharClass::separator},
    {0xFEFF, 0xFEFF, CharClass::separator},
    {0xFF01, 0xFF0F, CharClass::separator},
    {0xFF1A, 0xFF20, CharClass::separator},
    {0xFF3B, 0xFF40, CharClass::separator},
    {0xFF5B, 0xFF65, CharClass::separator},
    {0xFFFD, 0xFFFD, CharClass::separator},
    {0x1F000, 0x1FAFF, CharClass::separator},
    {0x20000, 0x2A6DF, CharClass::standalone},
};

static_assert(code_point_ranges::is_well_formed(class_ranges));

CharClass classify(char32_t code_point) {
    if (code_point < 0x80) {
        return std::isalnum(static_cast<int>(code_point)) ? CharClass::word : CharClass::separator;
    }
    const ClassRange* range = code_point_ranges::find(class_ranges, code_point);
    return range ? range->kind : CharClass::word;
}

bool is_inner_apostrophe(const std::string& text, utf8::Decoded current, std::size_t next,
                         bool inside_word) {
    const bool is_apostrophe = current.code_point == U'\'' || current.code_point == curly_apostrophe;
    return is_apostrophe && inside_word && next < text.size() &&
           classify(utf8::decode(text, next).code_point) == CharClass::word;
}

void flush(std::vector<std::string>& tokens, std::string& token) {
    if (!token.empty()) {
        tokens.push_back(std::move(token));
        token.clear();
    }
}

}

std::vector<std::string> StandardTokenizer::tokenize(const std::string& text) const {
    std::vector<std::string> tokens;
    std::string current_token;

    for (std::size_t i = 0; i < text.size();) {
        const utf8::Decoded current = utf8::decode(text, i);
        const std::size_t next = i + current.length;

        switch (classify(current.code_point)) {
            case CharClass::word:
                current_token.append(text, i, current.length);
                break;
            case CharClass::standalone:
                flush(tokens, current_token);
                tokens.emplace_back(text, i, current.length);
                break;
            case CharClass::separator:
                if (is_inner_apostrophe(text, current, next, !current_token.empty())) {
                    current_token.append(text, i, current.length);
                } else {
                    flush(tokens, current_token);
                }
                break;
        }
        i = next;
    }

    flush(tokens, current_token);
    return tokens;
}
