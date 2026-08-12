#pragma once
#include "../core/RingBuffer.hpp"
#include "../event/RoomEvent.hpp"
#include <chrono>

// The producer side of the ring buffer.
// Called by the HTTP handler (or simulation) when an interviewer taps
// "Round done" in the portal - which frees the room for reallocation.
//
// Thread-safety: in the single-producer model used here, only ONE thread
// should call publishRoomFree() at a time. For multi-producer (multiple
// coordinator tablets), add CAS on the cursor in RingBuffer::next().

class RoomAllocatorProducer {
public:
    explicit RoomAllocatorProducer(RingBuffer<RoomEvent>& ring)
        : ring_(ring) {}

    void publishRoomFree(Room* room) {
        int64_t seq    = ring_.next();       // claim slot (spins if ring full)
        RoomEvent& evt = ring_.get(seq);     // get pre-allocated event object
        int64_t    now = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()
                         ).count();
        evt.setRoomFree(room, now);
        ring_.publish(seq);                  // memory barrier - consumers can now see it
    }

private:
    RingBuffer<RoomEvent>& ring_;
};
