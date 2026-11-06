#include "EditDistance.h"

#include "Utf8.h"

#include <algorithm>
#include <utility>

std::vector<char32_t> to_code_points(std::string_view text) {
    std::vector<char32_t> code_points;
    code_points.reserve(text.size());
    std::size_t pos = 0;
    while (pos < text.size()) {
        const utf8::Decoded decoded = utf8::decode(text, pos);
        code_points.push_back(decoded.code_point);
        pos += decoded.length;
    }
    return code_points;
}

std::size_t edit_distance(std::string_view a, std::string_view b) {
    return edit_distance(a, b, a.size() + b.size());
}

std::size_t edit_distance(std::string_view a, std::string_view b, std::size_t max_distance) {
    const std::vector<char32_t> left = to_code_points(a);
    const std::vector<char32_t> right = to_code_points(b);
    const std::size_t over = max_distance + 1;

    const std::size_t length_gap =
        left.size() > right.size() ? left.size() - right.size() : right.size() - left.size();
    if (length_gap > max_distance) {
        return over;
    }

    std::vector<std::size_t> previous(right.size() + 1);
    std::vector<std::size_t> current(right.size() + 1);
    for (std::size_t j = 0; j <= right.size(); j++) {
        previous[j] = j;
    }

    for (std::size_t i = 1; i <= left.size(); i++) {
        current[0] = i;
        std::size_t row_minimum = current[0];
        for (std::size_t j = 1; j <= right.size(); j++) {
            const std::size_t substitution = previous[j - 1] + (left[i - 1] == right[j - 1] ? 0 : 1);
            current[j] = std::min({previous[j] + 1, current[j - 1] + 1, substitution});
            row_minimum = std::min(row_minimum, current[j]);
        }
        if (row_minimum > max_distance) {
            return over;
        }
        std::swap(previous, current);
    }

    return std::min(previous[right.size()], over);
}

std::size_t fuzzy_max_distance(std::size_t code_point_count) {
    if (code_point_count <= 2) {
        return 0;
    }
    if (code_point_count <= 5) {
        return 1;
    }
    return 2;
}

double fuzzy_score_factor(std::size_t distance) {
    return 1.0 / (1.0 + static_cast<double>(distance));
}
