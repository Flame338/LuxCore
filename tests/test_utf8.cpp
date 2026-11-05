#include "TestFramework.h"

#include "Utf8.h"

#include <array>

namespace {

utf8::Decoded decoded(const std::string& text) {
    return utf8::decode(std::string_view(text.data(), text.size()), 0);
}

bool is_invalid(const std::string& text) {
    const utf8::Decoded result = decoded(text);
    return result.code_point == utf8::replacement_character && result.length == 1;
}

}

TEST(utf8_decodes_one_to_four_byte_sequences) {
    CHECK_EQ(decoded("a").code_point, static_cast<char32_t>('a'));
    CHECK_EQ(decoded("a").length, static_cast<std::size_t>(1));
    CHECK_EQ(decoded("\xC3\xA9").code_point, static_cast<char32_t>(0xE9));
    CHECK_EQ(decoded("\xC3\xA9").length, static_cast<std::size_t>(2));
    CHECK_EQ(decoded("\xE6\x9D\xB1").code_point, static_cast<char32_t>(0x6771));
    CHECK_EQ(decoded("\xE6\x9D\xB1").length, static_cast<std::size_t>(3));
    CHECK_EQ(decoded("\xF0\x9F\x98\x80").code_point, static_cast<char32_t>(0x1F600));
    CHECK_EQ(decoded("\xF0\x9F\x98\x80").length, static_cast<std::size_t>(4));
}

TEST(utf8_decodes_at_an_offset_and_only_one_character) {
    const std::string text = "a\xC3\xA9z";
    const utf8::Decoded second = utf8::decode(text, 1);
    CHECK_EQ(second.code_point, static_cast<char32_t>(0xE9));
    CHECK_EQ(second.length, static_cast<std::size_t>(2));
    CHECK_EQ(utf8::decode(text, 3).code_point, static_cast<char32_t>('z'));
}

TEST(utf8_decodes_the_boundaries_of_each_length) {
    CHECK_EQ(decoded("\xC2\x80").code_point, static_cast<char32_t>(0x80));
    CHECK_EQ(decoded("\xDF\xBF").code_point, static_cast<char32_t>(0x7FF));
    CHECK_EQ(decoded("\xE0\xA0\x80").code_point, static_cast<char32_t>(0x800));
    CHECK_EQ(decoded("\xED\x9F\xBF").code_point, static_cast<char32_t>(0xD7FF));
    CHECK_EQ(decoded("\xEE\x80\x80").code_point, static_cast<char32_t>(0xE000));
    CHECK_EQ(decoded("\xEF\xBF\xBF").code_point, static_cast<char32_t>(0xFFFF));
    CHECK_EQ(decoded("\xF0\x90\x80\x80").code_point, static_cast<char32_t>(0x10000));
    CHECK_EQ(decoded("\xF4\x8F\xBF\xBF").code_point, static_cast<char32_t>(0x10FFFF));
}

TEST(utf8_rejects_malformed_input_as_one_replacement_byte) {
    CHECK(is_invalid("\x80"));
    CHECK(is_invalid("\xBF"));
    CHECK(is_invalid("\xC0\x80"));
    CHECK(is_invalid("\xC1\xBF"));
    CHECK(is_invalid("\xE0\x80\x80"));
    CHECK(is_invalid("\xF0\x80\x80\x80"));
    CHECK(is_invalid("\xED\xA0\x80"));
    CHECK(is_invalid("\xED\xBF\xBF"));
    CHECK(is_invalid("\xF4\x90\x80\x80"));
    CHECK(is_invalid("\xF5\x80\x80\x80"));
    CHECK(is_invalid("\xF8\x88\x80\x80\x80"));
    CHECK(is_invalid("\xFE"));
    CHECK(is_invalid("\xFF"));
}

TEST(utf8_rejects_truncated_sequences_without_reading_past_the_end) {
    CHECK(is_invalid("\xC3"));
    CHECK(is_invalid("\xE6"));
    CHECK(is_invalid("\xE6\x9D"));
    CHECK(is_invalid("\xF0"));
    CHECK(is_invalid("\xF0\x9F"));
    CHECK(is_invalid("\xF0\x9F\x98"));
    CHECK(is_invalid("\xC3x"));
    CHECK(is_invalid("\xE6\x9Dx"));
    CHECK(is_invalid("\xF0\x9F\x98x"));
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

TEST(utf8_append_writes_the_expected_bytes) {
    std::string text;
    utf8::append(text, 'a');
    utf8::append(text, 0xE9);
    utf8::append(text, 0x6771);
    utf8::append(text, 0x1F600);
    CHECK_EQ(text, "a\xC3\xA9\xE6\x9D\xB1\xF0\x9F\x98\x80");
}

TEST(utf8_counts_code_points_not_bytes) {
    CHECK_EQ(utf8::count_code_points(""), static_cast<std::size_t>(0));
    CHECK_EQ(utf8::count_code_points("abc"), static_cast<std::size_t>(3));
    CHECK_EQ(utf8::count_code_points("\xC3\xA9"), static_cast<std::size_t>(1));
    CHECK_EQ(utf8::count_code_points("\xD0\x9C\xD0\x9E"), static_cast<std::size_t>(2));
    CHECK_EQ(utf8::count_code_points("\xE6\x9D\xB1\xE4\xBA\xAC"), static_cast<std::size_t>(2));
    CHECK_EQ(utf8::count_code_points("\xF0\x9F\x98\x80"), static_cast<std::size_t>(1));
}

TEST(utf8_counts_each_malformed_byte_as_one_code_point) {
    CHECK_EQ(utf8::count_code_points("a\xFF" "b"), static_cast<std::size_t>(3));
    CHECK_EQ(utf8::count_code_points("\xC3"), static_cast<std::size_t>(1));
    CHECK_EQ(utf8::count_code_points("\xE6\x9D"), static_cast<std::size_t>(2));
}
