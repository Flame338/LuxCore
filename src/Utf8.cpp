#include "Utf8.h"

#include <array>
#include <bit>

namespace utf8 {

namespace {

constexpr std::array<char32_t, 5> smallest_code_point{0, 0, 0x80, 0x800, 0x10000};
constexpr char32_t largest_code_point = 0x10FFFF;
constexpr char32_t first_surrogate = 0xD800;
constexpr char32_t last_surrogate = 0xDFFF;

}

Decoded decode_multibyte(std::string_view text, std::size_t pos) {
    constexpr Decoded invalid{replacement_character, 1};

    const unsigned char lead = static_cast<unsigned char>(text[pos]);
    const std::size_t length = std::countl_one(lead);
    if (length < 2 || length > 4 || text.size() - pos < length) {
        return invalid;
    }

    char32_t code_point = lead & (0x7F >> length);
    for (std::size_t i = 1; i < length; i++) {
        const unsigned char byte = static_cast<unsigned char>(text[pos + i]);
        if ((byte & 0xC0) != 0x80) {
            return invalid;
        }
        code_point = (code_point << 6) | (byte & 0x3F);
    }

    const bool overlong = code_point < smallest_code_point[length];
    const bool surrogate = code_point >= first_surrogate && code_point <= last_surrogate;
    if (overlong || surrogate || code_point > largest_code_point) {
        return invalid;
    }
    return {code_point, length};
}

void append(std::string& out, char32_t code_point) {
    if (code_point < 0x80) {
        out += static_cast<char>(code_point);
    } else if (code_point < 0x800) {
        out += static_cast<char>(0xC0 | (code_point >> 6));
        out += static_cast<char>(0x80 | (code_point & 0x3F));
    } else if (code_point < 0x10000) {
        out += static_cast<char>(0xE0 | (code_point >> 12));
        out += static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code_point & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (code_point >> 18));
        out += static_cast<char>(0x80 | ((code_point >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code_point & 0x3F));
    }
}

std::size_t count_code_points(std::string_view text) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < text.size(); i += decode(text, i).length) {
        count++;
    }
    return count;
}

}
