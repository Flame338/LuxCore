#include "TokenFilter.h"

#include "CodePointRanges.h"
#include "Utf8.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string_view>

namespace {

struct FoldText {
    char32_t first;
    char32_t last;
    std::u32string_view replacement;
};

struct FoldShift {
    char32_t first;
    char32_t last;
    char32_t delta;
    char32_t step;
};

constexpr FoldText fold_text_rules[] = {
    {0x00C0, 0x00C5, U"a"},
    {0x00C6, 0x00C6, U"ae"},
    {0x00C7, 0x00C7, U"c"},
    {0x00C8, 0x00CB, U"e"},
    {0x00CC, 0x00CF, U"i"},
    {0x00D0, 0x00D0, U"d"},
    {0x00D1, 0x00D1, U"n"},
    {0x00D2, 0x00D6, U"o"},
    {0x00D8, 0x00D8, U"o"},
    {0x00D9, 0x00DC, U"u"},
    {0x00DD, 0x00DD, U"y"},
    {0x00DE, 0x00DE, U"th"},
    {0x00DF, 0x00DF, U"ss"},
    {0x00E0, 0x00E5, U"a"},
    {0x00E6, 0x00E6, U"ae"},
    {0x00E7, 0x00E7, U"c"},
    {0x00E8, 0x00EB, U"e"},
    {0x00EC, 0x00EF, U"i"},
    {0x00F0, 0x00F0, U"d"},
    {0x00F1, 0x00F1, U"n"},
    {0x00F2, 0x00F6, U"o"},
    {0x00F8, 0x00F8, U"o"},
    {0x00F9, 0x00FC, U"u"},
    {0x00FD, 0x00FD, U"y"},
    {0x00FE, 0x00FE, U"th"},
    {0x00FF, 0x00FF, U"y"},
    {0x0100, 0x0105, U"a"},
    {0x0106, 0x010D, U"c"},
    {0x010E, 0x0111, U"d"},
    {0x0112, 0x011B, U"e"},
    {0x011C, 0x0123, U"g"},
    {0x0124, 0x0127, U"h"},
    {0x0128, 0x0131, U"i"},
    {0x0132, 0x0133, U"ij"},
    {0x0134, 0x0135, U"j"},
    {0x0136, 0x0137, U"k"},
    {0x0138, 0x0138, U"q"},
    {0x0139, 0x0142, U"l"},
    {0x0143, 0x014B, U"n"},
    {0x014C, 0x0151, U"o"},
    {0x0152, 0x0153, U"oe"},
    {0x0154, 0x0159, U"r"},
    {0x015A, 0x0161, U"s"},
    {0x0162, 0x0167, U"t"},
    {0x0168, 0x0173, U"u"},
    {0x0174, 0x0175, U"w"},
    {0x0176, 0x0178, U"y"},
    {0x0179, 0x017E, U"z"},
    {0x017F, 0x017F, U"s"},
    {0x0300, 0x036F, U""},
    {0x0386, 0x0386, U"α"},
    {0x0388, 0x0388, U"ε"},
    {0x0389, 0x0389, U"η"},
    {0x038A, 0x038A, U"ι"},
    {0x038C, 0x038C, U"ο"},
    {0x038E, 0x038E, U"υ"},
    {0x038F, 0x038F, U"ω"},
    {0x0390, 0x0390, U"ι"},
    {0x03AA, 0x03AA, U"ι"},
    {0x03AB, 0x03AB, U"υ"},
    {0x03AC, 0x03AC, U"α"},
    {0x03AD, 0x03AD, U"ε"},
    {0x03AE, 0x03AE, U"η"},
    {0x03AF, 0x03AF, U"ι"},
    {0x03B0, 0x03B0, U"υ"},
    {0x03C2, 0x03C2, U"σ"},
    {0x03CA, 0x03CA, U"ι"},
    {0x03CB, 0x03CB, U"υ"},
    {0x03CC, 0x03CC, U"ο"},
    {0x03CD, 0x03CD, U"υ"},
    {0x03CE, 0x03CE, U"ω"},
};

constexpr FoldShift fold_shift_rules[] = {
    {0x0391, 0x03A1, 0x20, 1},
    {0x03A3, 0x03A9, 0x20, 1},
    {0x0400, 0x040F, 0x50, 1},
    {0x0410, 0x042F, 0x20, 1},
    {0x0460, 0x0481, 1, 2},
    {0x048A, 0x04BF, 1, 2},
    {0x04C0, 0x04C0, 0x0F, 1},
    {0x04C1, 0x04CE, 1, 2},
    {0x04D0, 0x052F, 1, 2},
};

static_assert(code_point_ranges::is_well_formed(fold_text_rules));
static_assert(code_point_ranges::is_well_formed(fold_shift_rules));

char ascii_lower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

void to_lower(std::string& word) {
    std::ranges::transform(word, word.begin(), ascii_lower);
}

bool is_ascii(std::string_view text) {
    return std::ranges::all_of(text, [](char c) { return static_cast<unsigned char>(c) < 0x80; });
}

void append_folded(std::string& out, std::string_view original, char32_t code_point) {
    if (code_point < 0x80) {
        out += ascii_lower(static_cast<char>(code_point));
    } else if (const FoldText* rule = code_point_ranges::find(fold_text_rules, code_point)) {
        for (const char32_t replacement : rule->replacement) {
            utf8::append(out, replacement);
        }
    } else if (const FoldShift* rule = code_point_ranges::find(fold_shift_rules, code_point);
               rule && (code_point - rule->first) % rule->step == 0) {
        utf8::append(out, code_point + rule->delta);
    } else {
        out.append(original);
    }
}

void fold(std::string& token) {
    if (is_ascii(token)) {
        return;
    }

    std::string folded;
    folded.reserve(token.size());
    for (std::size_t i = 0; i < token.size();) {
        const utf8::Decoded current = utf8::decode(token, i);
        append_folded(folded, std::string_view(token).substr(i, current.length), current.code_point);
        i += current.length;
    }
    token = std::move(folded);
}

}

void LowercaseFilter::apply(std::vector<std::string>& tokens) const {
    for (std::string& token : tokens) {
        to_lower(token);
    }
}

void UnicodeFoldingFilter::apply(std::vector<std::string>& tokens) const {
    for (std::string& token : tokens) {
        fold(token);
    }
    std::erase_if(tokens, [](const std::string& token) { return token.empty(); });
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
        fold(word);
        if (!word.empty()) {
            stop_words.insert(word);
        }
    }
}

void StopWordFilter::apply(std::vector<std::string>& tokens) const {
    std::erase_if(tokens, [this](const std::string& token) { return stop_words.contains(token); });
}
