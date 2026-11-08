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

constexpr int overtaking_rounds = 200;
constexpr long overtaking_bound = 3;
constexpr int tolerated_outliers = 10;
constexpr long section_cap = 1000000;
constexpr long warmup = 1000;
constexpr long starvation_limit = 100000;
constexpr int write_prepare = 3000;
constexpr int write_hold = 15000;
constexpr int read_hold = 15000;

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

void busy_work(int units) {
    volatile int sink = 0;
    for (int i = 0; i < units; i++) {
        sink = sink + 1;
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
    constexpr int rounds = 20000;

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

TEST(fair_mutex_reader_that_arrives_after_a_waiting_writer_enters_after_it) {
    Mutex mutex;
    std::atomic<int> sequence{0};
    std::atomic<int> writer_turn{0};
    std::atomic<int> reader_turn{0};
    std::atomic<bool> writer_queued{false};

    mutex.lock_shared();
    std::thread writer([&] {
        writer_queued.store(true);
        std::unique_lock lock(mutex);
        writer_turn.store(sequence.fetch_add(1) + 1);
    });
    wait_until([&] { return writer_queued.load(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::thread reader([&] {
        std::shared_lock lock(mutex);
        reader_turn.store(sequence.fetch_add(1) + 1);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    mutex.unlock_shared();
    writer.join();
    reader.join();

    CHECK_EQ(writer_turn.load(), 1);
    CHECK_EQ(reader_turn.load(), 2);
}

TEST(fair_mutex_a_reader_waits_for_at_most_the_writes_queued_before_it) {
    Mutex mutex;
    std::atomic<long> counter{0};
    std::atomic<bool> stop{false};

    std::thread writer([&] {
        for (long i = 0; i < section_cap * 5 && !stop.load(); i++) {
            busy_work(write_prepare);
            std::unique_lock lock(mutex);
            busy_work(write_hold);
            counter.store(counter.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        }
    });

    wait_until([&] { return counter.load() > warmup; });

    int outliers = 0;
    long worst = 0;
    for (int round = 0; round < overtaking_rounds; round++) {
        const long before = counter.load();
        long during = 0;
        {
            std::shared_lock lock(mutex);
            busy_work(read_hold);
            during = counter.load();
        }
        const long overtaken = during - before;
        if (overtaken > overtaking_bound) {
            outliers++;
        }
        if (overtaken > worst) {
            worst = overtaken;
        }
    }
    stop.store(true);
    writer.join();

    std::cout << "    reader overtaken by at most " << worst << " writes, " << outliers
              << " rounds above " << overtaking_bound << "\n";
    CHECK(outliers <= tolerated_outliers);
}

TEST(fair_mutex_a_writer_waits_for_at_most_the_reads_queued_before_it) {
    constexpr int reader_count = 4;
    constexpr long bound = 2 * reader_count;

    Mutex mutex;
    std::atomic<long> entered{0};
    std::atomic<bool> stop{false};

    std::vector<std::thread> readers;
    for (int t = 0; t < reader_count; t++) {
        readers.emplace_back([&] {
            for (long i = 0; i < section_cap && !stop.load(); i++) {
                std::shared_lock lock(mutex);
                entered.fetch_add(1);
                busy_work(read_hold);
            }
        });
    }

    wait_until([&] { return entered.load() > warmup; });

    int outliers = 0;
    long worst = 0;
    for (int round = 0; round < overtaking_rounds; round++) {
        const long before = entered.load();
        long during = 0;
        {
            std::unique_lock lock(mutex);
            during = entered.load();
        }
        const long overtaken = during - before;
        if (overtaken > bound) {
            outliers++;
        }
        if (overtaken > worst) {
            worst = overtaken;
        }
    }
    stop.store(true);
    for (std::thread& reader : readers) {
        reader.join();
    }

    std::cout << "    writer overtaken by at most " << worst << " reads, " << outliers
              << " rounds above " << bound << "\n";
    CHECK(outliers <= tolerated_outliers);
    CHECK(worst <= starvation_limit);
}
