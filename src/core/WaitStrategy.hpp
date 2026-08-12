#pragma once
#include <thread>
#include <chrono>

// Pluggable wait strategy used by consumers while spinning for the next
// published sequence. Trade-off: latency vs CPU burn.
//
//  BusySpinWaitStrategy   - lowest latency, burns 100% of one CPU core
//                           Use for SlotAssignmentConsumer (latency-critical)
//
//  YieldingWaitStrategy   - calls std::this_thread::yield() each spin
//                           Gives up the CPU timeslice; ~1-10µs extra latency
//                           Use for StudentNotifier and OcsDashboard
//
//  SleepingWaitStrategy   - sleeps for a fixed duration each spin
//                           Lowest CPU use, highest latency (~1ms+)
//                           Use for AuditLogConsumer (throughput, not latency)

struct BusySpinWaitStrategy {
    static void wait() noexcept {
#if defined(__x86_64__) || defined(__i386__)
        __builtin_ia32_pause();   // PAUSE instruction: reduces pipeline flush penalty
#elif defined(__aarch64__) || defined(__arm__)
        __asm__ volatile("yield" ::: "memory");
#endif
    }
};

struct YieldingWaitStrategy {
    static void wait() noexcept {
        std::this_thread::yield();
    }
};

struct SleepingWaitStrategy {
    static void wait() noexcept {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
};
