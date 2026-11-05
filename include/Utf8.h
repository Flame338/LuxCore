#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace utf8 {

constexpr char32_t replacement_character = 0xFFFD;

struct Decoded {
    char32_t code_point;
    std::size_t length;
};

Decoded decode_multibyte(std::string_view text, std::size_t pos);

inline Decoded decode(std::string_view text, std::size_t pos) {
    const unsigned char lead = static_cast<unsigned char>(text[pos]);
    if (lead < 0x80) {
        return {lead, 1};
    }
    return decode_multibyte(text, pos);
}

void append(std::string& out, char32_t code_point);

std::size_t count_code_points(std::string_view text);

}
