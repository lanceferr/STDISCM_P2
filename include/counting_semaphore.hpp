#pragma once

#include <condition_variable>
#include <cstddef>
#include <limits>
#include <mutex>
#include <stdexcept>

namespace dungeon {

// Permits persist even when release() happens before any waiter arrives.
class CountingSemaphore {
public:
    explicit CountingSemaphore(std::size_t permits = 0) : permits_(permits) {}

    CountingSemaphore(const CountingSemaphore&) = delete;
    CountingSemaphore& operator=(const CountingSemaphore&) = delete;

    void acquire() {
        std::unique_lock<std::mutex> lock(mutex_);
        available_.wait(lock, [this] { return permits_ != 0; });
        --permits_;
    }

    void release(std::size_t count = 1) {
        if (count == 0) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (count > std::numeric_limits<std::size_t>::max() - permits_) {
                throw std::overflow_error("Semaphore permit count overflow");
            }
            permits_ += count;
        }
        if (count == 1) {
            available_.notify_one();
        } else {
            available_.notify_all();
        }
    }

private:
    std::mutex mutex_;
    std::condition_variable available_;
    std::size_t permits_;
};

} // namespace dungeon

