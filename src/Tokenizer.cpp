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

void emit(std::vector<Token>& tokens, const std::string& text, std::size_t start, std::size_t end) {
    tokens.push_back({text.substr(start, end - start), tokens.size(), start, end});
}

}

std::vector<Token> StandardTokenizer::tokenize_tokens(const std::string& text) const {
    std::vector<Token> tokens;
    std::size_t word_start = 0;
    std::size_t word_end = 0;

    const auto flush = [&] {
        if (word_end > word_start) {
            emit(tokens, text, word_start, word_end);
        }
        word_start = word_end = 0;
    };

    for (std::size_t i = 0; i < text.size();) {
        const utf8::Decoded current = utf8::decode(text, i);
        const std::size_t next = i + current.length;

        switch (classify(current.code_point)) {
            case CharClass::word:
                if (word_end == word_start) {
                    word_start = i;
                }
                word_end = next;
                break;
            case CharClass::standalone:
                flush();
                emit(tokens, text, i, next);
                break;
            case CharClass::separator:
                if (is_inner_apostrophe(text, current, next, word_end > word_start)) {
                    word_end = next;
                } else {
                    flush();
                }
                break;
        }
        i = next;
    }

    flush();
    return tokens;
}
