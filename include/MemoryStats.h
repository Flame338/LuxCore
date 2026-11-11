#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct MemoryRow {
    std::string name;
    std::size_t items = 0;
    std::size_t used = 0;
    std::size_t reserved = 0;
    std::size_t allocations = 0;
    std::size_t heap = 0;

    // Windows heap blocks carry an 8-byte header and round to 16 bytes.
    void allocation(std::size_t bytes, std::size_t live_bytes) {
        if (bytes == 0) {
            return;
        }
        used += live_bytes;
        reserved += bytes;
        allocations++;
        heap += (bytes + 8 + 15) / 16 * 16;
    }

    void string(const std::string& value) {
        if (value.capacity() > 15) {
            allocation(value.capacity() + 1, value.size() + 1);
        }
    }

    template <typename T>
    void vector(const std::vector<T>& values) {
        allocation(values.capacity() * sizeof(T), values.size() * sizeof(T));
    }
};

using MemoryStats = std::vector<MemoryRow>;

struct PhaseClock {
    std::atomic<std::uint64_t> calls = 0;
    std::atomic<std::uint64_t> nanos = 0;

    struct Scope {
        PhaseClock& clock;
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        ~Scope() {
            clock.calls.fetch_add(1, std::memory_order_relaxed);
            clock.nanos.fetch_add(static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start)
                    .count()),
                                  std::memory_order_relaxed);
        }
    };

    Scope time() { return Scope{*this}; }
};

struct IndexClocks {
    PhaseClock lock_wait;
    PhaseClock analyze;
    PhaseClock store;
    PhaseClock invert;
    PhaseClock invert_remove;
    PhaseClock invert_terms;
    PhaseClock invert_doc_lists;
    PhaseClock keys;
    PhaseClock serialize;
    PhaseClock write;
    PhaseClock read;
    PhaseClock deserialize;
};

IndexClocks& index_clocks();
