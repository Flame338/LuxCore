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
    {0x0180, 0x0183, U"b"},
    {0x0187, 0x0188, U"c"},
    {0x0189, 0x018C, U"d"},
    {0x0191, 0x0192, U"f"},
    {0x0193, 0x0193, U"g"},
    {0x0195, 0x0195, U"hv"},
    {0x0197, 0x0197, U"i"},
    {0x0198, 0x0199, U"k"},
    {0x019A, 0x019A, U"l"},
    {0x019D, 0x019E, U"n"},
    {0x019F, 0x01A1, U"o"},
    {0x01A2, 0x01A3, U"oi"},
    {0x01A4, 0x01A5, U"p"},
    {0x01AB, 0x01AE, U"t"},
    {0x01AF, 0x01B0, U"u"},
    {0x01B2, 0x01B2, U"v"},
    {0x01B3, 0x01B4, U"y"},
    {0x01B5, 0x01B6, U"z"},
    {0x01C4, 0x01C6, U"dz"},
    {0x01C7, 0x01C9, U"lj"},
    {0x01CA, 0x01CC, U"nj"},
    {0x01CD, 0x01CE, U"a"},
    {0x01CF, 0x01D0, U"i"},
    {0x01D1, 0x01D2, U"o"},
    {0x01D3, 0x01DC, U"u"},
    {0x01DE, 0x01E1, U"a"},
    {0x01E2, 0x01E3, U"ae"},
    {0x01E4, 0x01E7, U"g"},
    {0x01E8, 0x01E9, U"k"},
    {0x01EA, 0x01ED, U"o"},
    {0x01F0, 0x01F0, U"j"},
    {0x01F1, 0x01F3, U"dz"},
    {0x01F4, 0x01F5, U"g"},
    {0x01F6, 0x01F6, U"hv"},
    {0x01F8, 0x01F9, U"n"},
    {0x01FA, 0x01FB, U"a"},
    {0x01FC, 0x01FD, U"ae"},
    {0x01FE, 0x01FF, U"o"},
    {0x0200, 0x0203, U"a"},
    {0x0204, 0x0207, U"e"},
    {0x0208, 0x020B, U"i"},
    {0x020C, 0x020F, U"o"},
    {0x0210, 0x0213, U"r"},
    {0x0214, 0x0217, U"u"},
    {0x0218, 0x0219, U"s"},
    {0x021A, 0x021B, U"t"},
    {0x021E, 0x021F, U"h"},
    {0x0220, 0x0220, U"n"},
    {0x0221, 0x0221, U"d"},
    {0x0222, 0x0223, U"ou"},
    {0x0224, 0x0225, U"z"},
    {0x0226, 0x0227, U"a"},
    {0x0228, 0x0229, U"e"},
    {0x022A, 0x0231, U"o"},
    {0x0232, 0x0233, U"y"},
    {0x0234, 0x0234, U"l"},
    {0x0235, 0x0235, U"n"},
    {0x0236, 0x0236, U"t"},
    {0x0237, 0x0237, U"j"},
    {0x0238, 0x0238, U"db"},
    {0x0239, 0x0239, U"qp"},
    {0x023A, 0x023A, U"a"},
    {0x023B, 0x023C, U"c"},
    {0x023D, 0x023D, U"l"},
    {0x023E, 0x023E, U"t"},
    {0x023F, 0x023F, U"s"},
    {0x0240, 0x0240, U"z"},
    {0x0243, 0x0243, U"b"},
    {0x0244, 0x0244, U"u"},
    {0x0246, 0x0247, U"e"},
    {0x0248, 0x0249, U"j"},
    {0x024A, 0x024B, U"q"},
    {0x024C, 0x024D, U"r"},
    {0x024E, 0x024F, U"y"},
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
    {0x1E00, 0x1E01, U"a"},
    {0x1E02, 0x1E07, U"b"},
    {0x1E08, 0x1E09, U"c"},
    {0x1E0A, 0x1E13, U"d"},
    {0x1E14, 0x1E1D, U"e"},
    {0x1E1E, 0x1E1F, U"f"},
    {0x1E20, 0x1E21, U"g"},
    {0x1E22, 0x1E2B, U"h"},
    {0x1E2C, 0x1E2F, U"i"},
    {0x1E30, 0x1E35, U"k"},
    {0x1E36, 0x1E3D, U"l"},
    {0x1E3E, 0x1E43, U"m"},
    {0x1E44, 0x1E4B, U"n"},
    {0x1E4C, 0x1E53, U"o"},
    {0x1E54, 0x1E57, U"p"},
    {0x1E58, 0x1E5F, U"r"},
    {0x1E60, 0x1E69, U"s"},
    {0x1E6A, 0x1E71, U"t"},
    {0x1E72, 0x1E7B, U"u"},
    {0x1E7C, 0x1E7F, U"v"},
    {0x1E80, 0x1E89, U"w"},
    {0x1E8A, 0x1E8D, U"x"},
    {0x1E8E, 0x1E8F, U"y"},
    {0x1E90, 0x1E95, U"z"},
    {0x1E96, 0x1E96, U"h"},
    {0x1E97, 0x1E97, U"t"},
    {0x1E98, 0x1E98, U"w"},
    {0x1E99, 0x1E99, U"y"},
    {0x1E9A, 0x1E9A, U"a"},
    {0x1E9B, 0x1E9D, U"s"},
    {0x1E9E, 0x1E9E, U"ss"},
    {0x1EA0, 0x1EB7, U"a"},
    {0x1EB8, 0x1EC7, U"e"},
    {0x1EC8, 0x1ECB, U"i"},
    {0x1ECC, 0x1EE3, U"o"},
    {0x1EE4, 0x1EF1, U"u"},
    {0x1EF2, 0x1EF9, U"y"},
    {0x1EFA, 0x1EFB, U"ll"},
    {0x1EFC, 0x1EFD, U"v"},
    {0x1EFE, 0x1EFF, U"y"},
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
    {0xFF10, 0xFF19, 0x30u - 0xFF10u, 1},
    {0xFF21, 0xFF3A, 0x61u - 0xFF21u, 1},
    {0xFF41, 0xFF5A, 0x61u - 0xFF41u, 1},
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

void TokenFilter::apply(std::vector<std::string>& terms) const {
    std::vector<Token> tokens;
    tokens.reserve(terms.size());
    for (std::size_t i = 0; i < terms.size(); i++) {
        tokens.push_back({std::move(terms[i]), i, 0, 0});
    }
    filter(tokens);
    terms = terms_of(tokens);
}

void LowercaseFilter::filter(std::vector<Token>& tokens) const {
    for (Token& token : tokens) {
        to_lower(token.term);
    }
}

void UnicodeFoldingFilter::filter(std::vector<Token>& tokens) const {
    for (Token& token : tokens) {
        fold(token.term);
    }
    std::erase_if(tokens, [](const Token& token) { return token.term.empty(); });
}

void NormalizeApostropheFilter::filter(std::vector<Token>& tokens) const {
    constexpr std::string_view curly_apostrophe = "\xE2\x80\x99";

    for (Token& token : tokens) {
        std::string& term = token.term;
        for (std::size_t pos = term.find(curly_apostrophe); pos != std::string::npos;
             pos = term.find(curly_apostrophe, pos + 1)) {
            term.replace(pos, curly_apostrophe.size(), "'");
        }
    }
}

void EnglishPossessiveFilter::filter(std::vector<Token>& tokens) const {
    for (Token& token : tokens) {
        std::string& term = token.term;
        if (term.size() > 2 && (term.ends_with("'s") || term.ends_with("'S"))) {
            term.resize(term.size() - 2);
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

void StopWordFilter::filter(std::vector<Token>& tokens) const {
    std::erase_if(tokens, [this](const Token& token) { return stop_words.contains(token.term); });
}
