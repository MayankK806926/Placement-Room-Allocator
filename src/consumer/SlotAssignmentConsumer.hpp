#pragma once
#include "../core/RingBuffer.hpp"
#include "../core/Sequence.hpp"
#include "../core/WaitStrategy.hpp"
#include "../event/RoomEvent.hpp"
#include "../scheduler/CompanyQueue.hpp"
#include "../scheduler/RoundSchedule.hpp"
#include <atomic>
#include <cstdio>
#include <chrono>

// The primary ring consumer. Runs on its own dedicated thread.
//
// On ROOM_FREE: finds the next waiting company for that section + room type,
// mutates the event slot to ROOM_ASSIGNED, then advances that company's round
// counter and re-enqueues it so it can claim its next room.
//
// Other consumers (StudentNotifier, OcsDashboard, AuditLog) wait on this
// consumer's sequence as their upstream barrier — they always see the
// ROOM_ASSIGNED version, never the raw ROOM_FREE.

class SlotAssignmentConsumer {
public:
    SlotAssignmentConsumer(RingBuffer<RoomEvent>&  ring,
                           Sequence&               seq,
                           CompanyQueue&           queue,
                           RoundScheduleRegistry&  schedRegistry)
        : ring_(ring), sequence_(seq)
        , queue_(queue), schedRegistry_(schedRegistry)
        , running_(true) {}

    void run() {
        int64_t nextSeq = sequence_.get() + 1;

        while (running_.load(std::memory_order_relaxed)) {

            // Wait for producer to publish this sequence
            while (running_.load(std::memory_order_relaxed) &&
                   ring_.getCursor() < nextSeq)
                BusySpinWaitStrategy::wait();

            if (!running_.load(std::memory_order_relaxed))
                break;

            RoomEvent& evt = ring_.get(nextSeq);

            if (evt.type == EventType::ROOM_FREE && evt.room != nullptr) {
                Company* next = queue_.pollNext(evt.room->section, evt.room->type);

                if (next != nullptr) {
                    int   round = queue_.getCurrentRound(next->companyId);
                    int64_t now = nowMs();

                    evt.setRoomAssigned(evt.room, next, round, now);

                    std::printf("[SlotAssign]  seq=%-4lld  %-8s → %-14s  round=%d\n",
                        (long long)nextSeq,
                        evt.room->roomId.c_str(),
                        next->displayName.c_str(),
                        round);

                    // Check if this company has more rounds left
                    const CompanySchedule* sched = schedRegistry_.get(next->companyId);
                    if (sched && round < sched->totalRounds()) {
                        queue_.advanceRound(next->companyId);
                        queue_.reEnqueue(next);   // back in queue for next round
                    } else {
                        std::printf("[SlotAssign]  %s — all rounds done, removed from queue\n",
                            next->displayName.c_str());
                    }
                } else {
                    // No company waiting for this room type in this section right now
                    std::printf("[SlotAssign]  seq=%-4lld  %-8s → (no company waiting)\n",
                        (long long)nextSeq, evt.room->roomId.c_str());
                }
            }

            sequence_.set(nextSeq);
            ++nextSeq;
        }
    }

    void     stop()           { running_.store(false, std::memory_order_relaxed); }
    Sequence& getSequence()   { return sequence_; }

private:
    static int64_t nowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    RingBuffer<RoomEvent>&  ring_;
    Sequence&               sequence_;
    CompanyQueue&           queue_;
    RoundScheduleRegistry&  schedRegistry_;
    std::atomic<bool>       running_;
};
