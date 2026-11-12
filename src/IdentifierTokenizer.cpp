#include "IdentifierTokenizer.h"

#include "CodePointRanges.h"
#include "Utf8.h"

#include <cstddef>

namespace {

enum class Kind { upper, lower, digit, apostrophe };

struct KindRange {
    char32_t first;
    char32_t last;
    Kind even;
    Kind odd;
};

constexpr KindRange kind_ranges[] = {
    {0x00C0, 0x00D6, Kind::upper, Kind::upper},
    {0x00D8, 0x00DE, Kind::upper, Kind::upper},
    {0x00DF, 0x00F6, Kind::lower, Kind::lower},
    {0x00F8, 0x00FF, Kind::lower, Kind::lower},
    {0x0100, 0x0137, Kind::upper, Kind::lower},
    {0x0138, 0x0138, Kind::lower, Kind::lower},
    {0x0139, 0x0148, Kind::upper, Kind::lower},
    {0x0149, 0x0149, Kind::lower, Kind::lower},
    {0x014A, 0x0177, Kind::upper, Kind::lower},
    {0x0178, 0x0178, Kind::upper, Kind::upper},
    {0x0179, 0x017E, Kind::upper, Kind::lower},
    {0x017F, 0x017F, Kind::lower, Kind::lower},
    {0x0386, 0x0386, Kind::upper, Kind::upper},
    {0x0388, 0x038A, Kind::upper, Kind::upper},
    {0x038C, 0x038C, Kind::upper, Kind::upper},
    {0x038E, 0x038F, Kind::upper, Kind::upper},
    {0x0390, 0x0390, Kind::lower, Kind::lower},
    {0x0391, 0x03A1, Kind::upper, Kind::upper},
    {0x03A3, 0x03AB, Kind::upper, Kind::upper},
    {0x03AC, 0x03CE, Kind::lower, Kind::lower},
    {0x0400, 0x042F, Kind::upper, Kind::upper},
    {0x0430, 0x045F, Kind::lower, Kind::lower},
    {0x0460, 0x0481, Kind::upper, Kind::lower},
    {0x048A, 0x04BF, Kind::upper, Kind::lower},
    {0x04C0, 0x04C0, Kind::upper, Kind::upper},
    {0x04C1, 0x04CE, Kind::upper, Kind::lower},
    {0x04CF, 0x04CF, Kind::lower, Kind::lower},
    {0x04D0, 0x052F, Kind::upper, Kind::lower},
    {0x0531, 0x0556, Kind::upper, Kind::upper},
    {0x0561, 0x0586, Kind::lower, Kind::lower},
    {0x0660, 0x0669, Kind::digit, Kind::digit},
    {0x06F0, 0x06F9, Kind::digit, Kind::digit},
    {0x0966, 0x096F, Kind::digit, Kind::digit},
    {0x1E00, 0x1E95, Kind::upper, Kind::lower},
    {0x1E96, 0x1E9D, Kind::lower, Kind::lower},
    {0x1E9E, 0x1E9E, Kind::upper, Kind::upper},
    {0x1E9F, 0x1E9F, Kind::lower, Kind::lower},
    {0x1EA0, 0x1EFF, Kind::upper, Kind::lower},
    {0xFF10, 0xFF19, Kind::digit, Kind::digit},
    {0xFF21, 0xFF3A, Kind::upper, Kind::upper},
    {0xFF41, 0xFF5A, Kind::lower, Kind::lower},
};

static_assert(code_point_ranges::is_well_formed(kind_ranges));

constexpr char32_t curly_apostrophe = 0x2019;

Kind kind_of(char32_t code_point) {
    if (code_point < 0x80) {
        if (code_point >= U'A' && code_point <= U'Z') {
            return Kind::upper;
        }
        if (code_point >= U'0' && code_point <= U'9') {
            return Kind::digit;
        }
        return code_point == U'\'' ? Kind::apostrophe : Kind::lower;
    }
    if (code_point == curly_apostrophe) {
        return Kind::apostrophe;
    }
    const KindRange* range = code_point_ranges::find(kind_ranges, code_point);
    if (range == nullptr) {
        return Kind::lower;
    }
    return (code_point - range->first) % 2 == 0 ? range->even : range->odd;
}

struct Unit {
    std::size_t offset;
    Kind kind;
};

std::vector<Unit> classify_units(const std::string& token) {
    std::vector<Unit> units;
    for (std::size_t i = 0; i < token.size();) {
        const utf8::Decoded current = utf8::decode(token, i);
        units.push_back({i, kind_of(current.code_point)});
        i += current.length;
    }
    return units;
}

bool splits_before(const std::vector<Unit>& units, std::size_t i, const IdentifierOptions& options) {
    const Kind previous = units[i - 1].kind;
    const Kind current = units[i].kind;
    if (previous == Kind::apostrophe || current == Kind::apostrophe) {
        return false;
    }
    if (options.split_letters_digits && (previous == Kind::digit) != (current == Kind::digit)) {
        return true;
    }
    if (current != Kind::upper) {
        return false;
    }
    if (previous == Kind::lower) {
        return true;
    }
    const bool lower_follows = i + 1 < units.size() && units[i + 1].kind == Kind::lower;
    return lower_follows && (previous == Kind::upper || previous == Kind::digit);
}

struct Span {
    std::size_t start;
    std::size_t end;
};

std::vector<Span> split_words(const std::string& token, const IdentifierOptions& options) {
    const std::vector<Unit> units = classify_units(token);

    std::vector<Span> words;
    std::size_t start = 0;
    for (std::size_t i = 1; i < units.size(); i++) {
        if (splits_before(units, i, options)) {
            words.push_back({start, units[i].offset});
            start = units[i].offset;
        }
    }
    words.push_back({start, token.size()});
    return words;
}

}

IdentifierTokenizer::IdentifierTokenizer(IdentifierOptions options) : options(options) {}

std::vector<Token> IdentifierTokenizer::tokenize_tokens(std::string_view text) const {
    std::vector<Token> tokens;
    std::size_t position = 0;
    for (const Token& token : base.tokenize_tokens(text)) {
        const std::vector<Span> words = split_words(token.term, options);
        if (words.size() > 1 && options.keep_original) {
            tokens.push_back({token.term, position, token.start_offset, token.end_offset});
        }
        for (std::size_t i = 0; i < words.size(); i++) {
            tokens.push_back({token.term.substr(words[i].start, words[i].end - words[i].start),
                              position + i, token.start_offset + words[i].start,
                              token.start_offset + words[i].end});
        }
        position += words.size();
    }
    return tokens;
}

Analyzer make_identifier_analyzer(IdentifierOptions options) {
    Analyzer analyzer(std::make_unique<IdentifierTokenizer>(options));
    analyzer.add_filter(std::make_unique<LowercaseFilter>());
    analyzer.add_filter(std::make_unique<UnicodeFoldingFilter>());
    return analyzer;
}
