#include "TestFramework.h"

#include "Utf8.h"

#include <array>

namespace {

}

TEST(utf8_decoding_any_short_byte_string_always_advances_and_stays_in_bounds) {
    constexpr std::array<unsigned char, 12> pool{0x00, 0x41, 0x7F, 0x80, 0xBF, 0xC0, 0xC3, 0xE0, 0xED, 0xF0, 0xF4, 0xFF};
    int violations = 0;
    for (const unsigned char a : pool) {
        for (const unsigned char b : pool) {
            for (const unsigned char c : pool) {
                for (const unsigned char d : pool) {
                    const std::string text{static_cast<char>(a), static_cast<char>(b), static_cast<char>(c), static_cast<char>(d)};
                    for (std::size_t length = 1; length <= text.size(); length++) {
                        const std::string input = text.substr(0, length);
                        std::size_t consumed = 0;
                        while (consumed < input.size()) {
                            const utf8::Decoded step = utf8::decode(input, consumed);
                            if (step.length == 0 || consumed + step.length > input.size()) {
                                violations++;
                                break;
                            }
                            consumed += step.length;
                        }
                    }
                }
            }
        }
    }
    CHECK_EQ(violations, 0);
}

TEST(utf8_append_round_trips_every_scalar_value) {
    int mismatches = 0;
    for (char32_t code_point = 0; code_point <= 0x10FFFF; code_point++) {
        if (code_point >= 0xD800 && code_point <= 0xDFFF) {
            continue;
        }
        std::string encoded;
        utf8::append(encoded, code_point);
        const utf8::Decoded back = utf8::decode(encoded, 0);
        if (back.code_point != code_point || back.length != encoded.size()) {
            mismatches++;
        }
    }
    CHECK_EQ(mismatches, 0);
}

TEST(utf8_counts_code_points_not_bytes) {
    CHECK_EQ(utf8::count_code_points(""), static_cast<std::size_t>(0));
    CHECK_EQ(utf8::count_code_points("abc"), static_cast<std::size_t>(3));
    CHECK_EQ(utf8::count_code_points("\xC3\xA9"), static_cast<std::size_t>(1));
    CHECK_EQ(utf8::count_code_points("\xD0\x9C\xD0\x9E"), static_cast<std::size_t>(2));
    CHECK_EQ(utf8::count_code_points("\xE6\x9D\xB1\xE4\xBA\xAC"), static_cast<std::size_t>(2));
    CHECK_EQ(utf8::count_code_points("\xF0\x9F\x98\x80"), static_cast<std::size_t>(1));
}
