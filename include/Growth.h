#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

// Grows by an eighth instead of doubling, so a large vector wastes at most 12.5%.
template <typename T>
void make_room_for(std::vector<T>& values, std::size_t extra) {
    if (values.size() + extra > values.capacity()) {
        values.reserve(values.size() + std::max(extra, std::max<std::size_t>(16, values.size() / 8)));
    }
}
