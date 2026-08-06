#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

// Cache line size on x86/ARM is typically 64 bytes.
// If two Sequence objects share a cache line, a write to one invalidates
// the other in every other core's L1 cache — "false sharing".
// Padding ensures each Sequence owns its cache line exclusively.
constexpr size_t CACHE_LINE_SIZE = 64;

struct alignas(CACHE_LINE_SIZE) Sequence {
    std::atomic<int64_t> value;

    explicit Sequence(int64_t initial = -1) : value(initial) {}

    // No copy — sequences are identity objects
    Sequence(const Sequence&)            = delete;
    Sequence& operator=(const Sequence&) = delete;

    inline int64_t get() const noexcept {
        return value.load(std::memory_order_acquire);
    }

    inline void set(int64_t v) noexcept {
        value.store(v, std::memory_order_release);
    }

    inline bool compareAndSet(int64_t expected, int64_t desired) noexcept {
        return value.compare_exchange_strong(expected, desired,
            std::memory_order_acq_rel, std::memory_order_acquire);
    }
};
