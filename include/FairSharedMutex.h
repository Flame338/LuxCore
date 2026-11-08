#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>

class FairSharedMutex {
public:
    FairSharedMutex() = default;
    FairSharedMutex(const FairSharedMutex&) = delete;
    FairSharedMutex& operator=(const FairSharedMutex&) = delete;

    void lock_shared() {
        std::unique_lock guard(state);
        const std::size_t ticket = next_ticket++;
        turn.wait(guard, [&] { return now_serving == ticket; });
        active_readers++;
        now_serving++;
        turn.notify_all();
    }

    void unlock_shared() {
        std::unique_lock guard(state);
        active_readers--;
        if (active_readers == 0) {
            turn.notify_all();
        }
    }

    void lock() {
        std::unique_lock guard(state);
        const std::size_t ticket = next_ticket++;
        turn.wait(guard, [&] { return now_serving == ticket && active_readers == 0; });
    }

    void unlock() {
        std::unique_lock guard(state);
        now_serving++;
        turn.notify_all();
    }

private:
    std::mutex state;
    std::condition_variable turn;
    std::size_t next_ticket = 0;
    std::size_t now_serving = 0;
    std::size_t active_readers = 0;
};
