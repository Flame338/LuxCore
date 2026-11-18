#pragma once

#include "PostingStore.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

inline std::size_t list_gap_size(std::uint32_t gap) {
    std::size_t size = 1;
    for (; gap >= 0x80; gap >>= 7) {
        size++;
    }
    return size;
}

inline char* write_list_gap(char* out, std::uint32_t gap) {
    for (; gap >= 0x80; gap >>= 7) {
        *out++ = static_cast<char>(gap | 0x80);
    }
    *out++ = static_cast<char>(gap);
    return out;
}

inline void append_list_gap(std::string& out, std::uint32_t gap) {
    char bytes[5];
    out.append(bytes, write_list_gap(bytes, gap));
}

inline void decode_list_gaps(std::string_view encoded, std::vector<ListId>& out) {
    out.clear();
    ListId id = 0;
    for (std::size_t at = 0; at < encoded.size();) {
        std::uint32_t gap = 0;
        for (int shift = 0;; shift += 7) {
            const std::uint8_t byte = static_cast<std::uint8_t>(encoded[at++]);
            gap |= static_cast<std::uint32_t>(byte & 0x7F) << shift;
            if ((byte & 0x80) == 0) {
                break;
            }
        }
        id += gap;
        out.push_back(id);
    }
}
