#pragma once
#include "../model/Model.hpp"
#include <cstdint>
#include <string>
#include <optional>

// What gets written into each ring buffer slot.
//
// Instances are PRE-ALLOCATED in the ring at startup — never heap-allocated
// during the placement day. The producer fills these fields via setRoomFree()
// or setRoomAssigned() before calling ring.publish(seq).
//
// Fields don't need to be atomic because the sequence publish/acquire provides
// the happens-before guarantee between producer and consumers.

enum class EventType {
    EMPTY,         // slot not yet written (initial state)
    ROOM_FREE,     // interviewer marked round done; room available
    ROOM_ASSIGNED  // SlotAssignmentConsumer matched room to a company+round
};

struct RoomEvent {
    EventType           type        = EventType::EMPTY;
    Room*               room        = nullptr;  // pointer into static room table
    Company*            company     = nullptr;  // null when ROOM_FREE
    int                 roundNumber = 0;
    int64_t             timestampMs = 0;
    std::string         notes;                  // optional coordinator note

    // ── Producer writes ──────────────────────────────────────────────────────

    void setRoomFree(Room* r, int64_t ts) noexcept {
        type        = EventType::ROOM_FREE;
        room        = r;
        company     = nullptr;
        roundNumber = 0;
        timestampMs = ts;
        notes.clear();
    }

    void setRoomAssigned(Room* r, Company* c, int round, int64_t ts) noexcept {
        type        = EventType::ROOM_ASSIGNED;
        room        = r;
        company     = c;
        roundNumber = round;
        timestampMs = ts;
        notes.clear();
    }

    void reset() noexcept {
        type        = EventType::EMPTY;
        room        = nullptr;
        company     = nullptr;
        roundNumber = 0;
        timestampMs = 0;
        notes.clear();
    }
};
