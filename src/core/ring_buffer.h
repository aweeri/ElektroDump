// ElektroDump — bounded single-producer / single-consumer lock-free ring.
// Used for the IQ flow (SDR callback -> DSP). The producer never blocks: when
// full, the oldest items are dropped and the caller counts the loss. Capacity is
// rounded up to a power of two so index masking is a single AND.
#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

namespace ed {

template <typename T>
class SpscRing {
public:
    SpscRing() { resize(2); }
    explicit SpscRing(std::size_t capacity) { resize(capacity); }

    void resize(std::size_t capacity) {
        std::size_t cap = 2;
        while (cap < capacity) {
            cap <<= 1;
        }
        buffer_.assign(cap, T{});
        mask_ = cap - 1;
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

    std::size_t capacity() const { return mask_ + 1; }

    std::size_t size() const {
        const std::size_t head = head_.load(std::memory_order_acquire);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        return head - tail;
    }

    bool empty() const { return size() == 0; }

    // Producer side: writes up to `n` items, returns the number written. On
    // overflow the oldest items are discarded first so the callback never blocks.
    std::size_t write(const T* data, std::size_t n) {
        const std::size_t cap = capacity();
        if (n > cap) {
            data += (n - cap);
            n = cap;
        }
        const std::size_t head = head_.load(std::memory_order_relaxed);
        std::size_t tail = tail_.load(std::memory_order_acquire);
        if (head - tail + n > cap) {
            std::size_t new_tail = tail + (head - tail + n - cap);
            while (!tail_.compare_exchange_weak(tail, new_tail,
                                                std::memory_order_acq_rel,
                                                std::memory_order_acquire)) {
                if (head - tail + n <= cap) {
                    break;
                }
                new_tail = tail + (head - tail + n - cap);
            }
        }
        for (std::size_t i = 0; i < n; ++i) {
            buffer_[(head + i) & mask_] = data[i];
        }
        head_.store(head + n, std::memory_order_release);
        return n;
    }

    // Consumer side. Reads up to `n` items and returns the number read.
    std::size_t read(T* out, std::size_t n) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t head = head_.load(std::memory_order_acquire);
        const std::size_t avail = head - tail;
        if (n > avail) {
            n = avail;
        }
        for (std::size_t i = 0; i < n; ++i) {
            out[i] = buffer_[(tail + i) & mask_];
        }
        tail_.store(tail + n, std::memory_order_release);
        return n;
    }

    // Consumer side: discard everything currently buffered.
    void clear() {
        tail_.store(head_.load(std::memory_order_acquire),
                    std::memory_order_release);
    }

private:
    std::vector<T> buffer_;
    std::size_t mask_ = 0;
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
};

}  // namespace ed
