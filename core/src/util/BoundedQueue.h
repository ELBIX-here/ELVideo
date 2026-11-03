#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>

namespace elv {

// Fixed-capacity MPMC queue. When full, push() drops the oldest item so the
// producer (capture callback) never blocks.
template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity) : capacity_(capacity) {}

    // Returns false if an old item had to be dropped.
    bool push(T item) {
        bool dropped = false;
        {
            std::lock_guard lock(mutex_);
            if (closed_) return false;
            if (items_.size() >= capacity_) {
                items_.pop_front();
                dropped = true;
            }
            items_.push_back(std::move(item));
        }
        cv_.notify_one();
        return !dropped;
    }

    std::optional<T> pop(std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex_);
        if (!cv_.wait_for(lock, timeout, [&] { return closed_ || !items_.empty(); }))
            return std::nullopt;
        if (items_.empty()) return std::nullopt;
        T item = std::move(items_.front());
        items_.pop_front();
        return item;
    }

    void close() {
        {
            std::lock_guard lock(mutex_);
            closed_ = true;
        }
        cv_.notify_all();
    }

    void clear() {
        std::lock_guard lock(mutex_);
        items_.clear();
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<T> items_;
    std::size_t capacity_;
    bool closed_ = false;
};

}  // namespace elv
