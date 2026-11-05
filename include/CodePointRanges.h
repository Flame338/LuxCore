#pragma once

#include <algorithm>
#include <iterator>
#include <ranges>

namespace code_point_ranges {

template <std::ranges::random_access_range Rules>
constexpr bool is_well_formed(const Rules& rules) {
    for (std::size_t i = 0; i < std::ranges::size(rules); i++) {
        if (rules[i].first > rules[i].last) {
            return false;
        }
        if (i > 0 && rules[i - 1].last >= rules[i].first) {
            return false;
        }
    }
    return true;
}

template <std::ranges::random_access_range Rules>
const std::ranges::range_value_t<Rules>* find(const Rules& rules, char32_t code_point) {
    using Rule = std::ranges::range_value_t<Rules>;

    const auto above = std::ranges::upper_bound(rules, code_point, {}, &Rule::first);
    if (above == std::ranges::begin(rules)) {
        return nullptr;
    }
    const Rule& candidate = *std::prev(above);
    return code_point <= candidate.last ? &candidate : nullptr;
}

}
