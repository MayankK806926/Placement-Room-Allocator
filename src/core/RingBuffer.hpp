#pragma once
#include "Sequence.hpp"
#include <vector>
#include <cassert>
#include <stdexcept>
#include <algorithm>

// Fixed-size circular buffer - all slots pre-allocated at construction time.
// T must be default-constructible. Producers fill slots via get(seq) before
// calling publish(seq). Consumers read via get(seq) after they see the cursor
// advance past their target sequence.
//
// Ring size MUST be a power of 2 - enables (seq & indexMask) instead of
// expensive modulo for slot lookup.

template<typename T>
class RingBuffer {
public:
    // gatingSequences: one Sequence* per consumer. Producer spins until
    // the slowest consumer has moved past the wrap-around point.
    RingBuffer(size_t size, std::vector<Sequence*> gatingSequences)
        : slots_(size)
        , indexMask_(size - 1)
        , cursor_(-1)
        , gatingSequences_(std::move(gatingSequences))
    {
        if (size == 0 || (size & (size - 1)) != 0)
            throw std::invalid_argument("Ring size must be a power of 2");
    }

    // Producer: claim the next sequence number.
    // Busy-spins ONLY if the ring would lap the slowest consumer.
    int64_t next() noexcept {
        int64_t nextSeq  = cursor_.get() + 1;
        int64_t wrapPoint = nextSeq - static_cast<int64_t>(slots_.size());

        int64_t minGating;
        do {
            minGating = INT64_MAX;
            for (std::size_t i = 0; i < gatingSequences_.size(); ++i)
                minGating = std::min(minGating, gatingSequences_[i]->get());
        } while (wrapPoint > minGating);

        return nextSeq;
    }

    // Producer: after writing into the slot, publish so consumers can see it.
    void publish(int64_t sequence) noexcept {
        cursor_.set(sequence);
    }

    // Get the pre-allocated event object at this sequence.
    // Producer fills it before publish(); consumers read it after.
    T& get(int64_t sequence) noexcept {
        return slots_[sequence & indexMask_];
    }

    int64_t getCursor() const noexcept { return cursor_.get(); }
    size_t  size()      const noexcept { return slots_.size(); }

private:
    std::vector<T>          slots_;
    const size_t            indexMask_;
    Sequence                cursor_;
    std::vector<Sequence*>  gatingSequences_;
};
