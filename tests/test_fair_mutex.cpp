#include "TestFramework.h"

#include "FairSharedMutex.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <vector>

namespace {

using Mutex = FairSharedMutex;

template <typename Body>
void run_threads(int count, Body body) {
    std::atomic<bool> go{false};
    std::vector<std::thread> threads;
    for (int t = 0; t < count; t++) {
        threads.emplace_back([&, t] {
            while (!go.load()) {
                std::this_thread::yield();
            }
            body(t);
        });
    }
    go.store(true);
    for (std::thread& thread : threads) {
        thread.join();
    }
}

template <typename Condition>
bool wait_until(Condition condition) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!condition()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        std::this_thread::yield();
    }
    return true;
}

}

TEST(fair_mutex_writers_exclude_each_other_and_readers) {
    constexpr int writer_count = 4;
    constexpr int reader_count = 4;
    constexpr int rounds = 2000;

    Mutex mutex;
    long first = 0;
    long second = 0;
    std::atomic<int> writers_inside{0};
    std::atomic<int> readers_inside{0};
    std::atomic<int> overlaps{0};
    std::atomic<int> torn_reads{0};

    run_threads(writer_count + reader_count, [&](int t) {
        for (int i = 0; i < rounds; i++) {
            if (t < writer_count) {
                std::lock_guard lock(mutex);
                if (writers_inside.fetch_add(1) != 0 || readers_inside.load() != 0) {
                    overlaps.fetch_add(1);
                }
                first++;
                second++;
                writers_inside.fetch_sub(1);
            } else {
                std::shared_lock lock(mutex);
                readers_inside.fetch_add(1);
                if (writers_inside.load() != 0) {
                    overlaps.fetch_add(1);
                }
                if (first != second) {
                    torn_reads.fetch_add(1);
                }
                readers_inside.fetch_sub(1);
            }
        }
    });

    CHECK_EQ(overlaps.load(), 0);
    CHECK_EQ(torn_reads.load(), 0);
    CHECK_EQ(first, static_cast<long>(writer_count) * rounds);
    CHECK_EQ(second, static_cast<long>(writer_count) * rounds);
}

TEST(fair_mutex_readers_share_the_lock) {
    constexpr int reader_count = 6;

    Mutex mutex;
    std::atomic<int> holding{0};
    std::atomic<int> most_holding{0};
    std::atomic<int> timed_out{0};

    run_threads(reader_count, [&](int) {
        std::shared_lock lock(mutex);
        const int now = holding.fetch_add(1) + 1;
        int seen = most_holding.load();
        while (now > seen && !most_holding.compare_exchange_weak(seen, now)) {
        }
        if (!wait_until([&] { return holding.load() == reader_count; })) {
            timed_out.fetch_add(1);
        }
    });

    CHECK_EQ(timed_out.load(), 0);
    CHECK_EQ(most_holding.load(), reader_count);
}

TEST(fair_mutex_writer_waits_for_readers_to_leave) {
    Mutex mutex;
    std::atomic<bool> writer_done{false};

    mutex.lock_shared();
    std::thread writer([&] {
        std::unique_lock lock(mutex);
        writer_done.store(true);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(!writer_done.load());
    mutex.unlock_shared();
    writer.join();
    CHECK(writer_done.load());
}
