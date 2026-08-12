#include "core/RingBuffer.hpp"
#include "core/Sequence.hpp"
#include "core/WaitStrategy.hpp"
#include "event/RoomEvent.hpp"
#include <cassert>
#include <cstdio>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

// ── Test 1: Basic single-threaded produce + consume ──────────────────────────
static void test_basic_produce_consume() {
    Sequence seqConsumer(-1);
    RingBuffer<RoomEvent> ring(8, { &seqConsumer });

    // Produce one event
    int64_t seq = ring.next();
    assert(seq == 0);
    ring.get(seq).type = EventType::ROOM_FREE;
    ring.publish(seq);

    // Consumer reads it
    assert(ring.getCursor() == 0);
    assert(ring.get(0).type == EventType::ROOM_FREE);
    seqConsumer.set(0);

    std::printf("[PASS] test_basic_produce_consume\n");
}

// ── Test 2: Ring wraps around correctly ──────────────────────────────────────
static void test_ring_wraparound() {
    Sequence seqConsumer(-1);
    RingBuffer<RoomEvent> ring(4, { &seqConsumer });

    // Fill all 4 slots
    for (int i = 0; i < 4; ++i) {
        int64_t s = ring.next();
        ring.get(s).roundNumber = i;
        ring.publish(s);
        seqConsumer.set(s);  // consumer keeps up
    }

    // Next sequence (4) should map to slot 0 (4 & 3 == 0)
    int64_t s4 = ring.next();
    assert(s4 == 4);
    ring.get(s4).roundNumber = 99;
    ring.publish(s4);
    seqConsumer.set(s4);

    assert(ring.get(4).roundNumber == 99);

    std::printf("[PASS] test_ring_wraparound\n");
}

// ── Test 3: Producer blocks when ring is full ────────────────────────────────
static void test_producer_blocks_when_full() {
    Sequence seqConsumer(-1);
    RingBuffer<RoomEvent> ring(4, { &seqConsumer });

    // Fill all 4 slots without consumer advancing
    for (int i = 0; i < 4; ++i) {
        int64_t s = ring.next();
        ring.publish(s);
    }

    // Producer should block - release consumer after short delay
    std::atomic<bool> produced(false);
    std::thread producer([&]{
        int64_t s = ring.next();  // should block until consumer moves
        ring.publish(s);
        produced = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(!produced.load());   // should still be blocked

    seqConsumer.set(0);  // consumer advances by 1

    producer.join();
    assert(produced.load());

    std::printf("[PASS] test_producer_blocks_when_full\n");
}

// ── Test 4: Multiple consumers, each sees every event ───────────────────────
static void test_multiple_consumers_each_see_all_events() {
    Sequence seqA(-1), seqB(-1);
    RingBuffer<RoomEvent> ring(8, { &seqA, &seqB });

    constexpr int N = 6;

    // Consumer A thread
    std::vector<int> seenByA, seenByB;
    std::thread tA([&]{
        for (int i = 0; i < N; ++i) {
            while (ring.getCursor() < i) BusySpinWaitStrategy::wait();
            seenByA.push_back(ring.get(i).roundNumber);
            seqA.set(i);
        }
    });
    // Consumer B thread
    std::thread tB([&]{
        for (int i = 0; i < N; ++i) {
            while (ring.getCursor() < i) BusySpinWaitStrategy::wait();
            seenByB.push_back(ring.get(i).roundNumber);
            seqB.set(i);
        }
    });

    // Producer
    for (int i = 0; i < N; ++i) {
        int64_t s = ring.next();
        ring.get(s).roundNumber = i * 10;
        ring.publish(s);
    }

    tA.join(); tB.join();

    assert(seenByA.size() == N);
    assert(seenByB.size() == N);
    for (int i = 0; i < N; ++i) {
        assert(seenByA[i] == i * 10);
        assert(seenByB[i] == i * 10);
    }

    std::printf("[PASS] test_multiple_consumers_each_see_all_events\n");
}

// ── Test 5: Ring size must be power of 2 ────────────────────────────────────
static void test_invalid_ring_size_throws() {
    Sequence s(-1);
    bool threw = false;
    try {
        RingBuffer<RoomEvent> ring(6, { &s });  // 6 is not power of 2
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);
    std::printf("[PASS] test_invalid_ring_size_throws\n");
}

int main() {
    std::printf("=== RingBuffer Tests ===\n");
    test_basic_produce_consume();
    test_ring_wraparound();
    test_producer_blocks_when_full();
    test_multiple_consumers_each_see_all_events();
    test_invalid_ring_size_throws();
    std::printf("All RingBuffer tests passed.\n");
    return 0;
}
