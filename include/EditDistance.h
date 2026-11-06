#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

std::vector<char32_t> to_code_points(std::string_view text);

std::size_t edit_distance(std::string_view a, std::string_view b);

std::size_t edit_distance(std::string_view a, std::string_view b, std::size_t max_distance);

std::size_t fuzzy_max_distance(std::size_t code_point_count);

double fuzzy_score_factor(std::size_t distance);
