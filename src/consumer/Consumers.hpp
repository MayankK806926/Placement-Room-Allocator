#pragma once
#include "../core/RingBuffer.hpp"
#include "../core/Sequence.hpp"
#include "../core/WaitStrategy.hpp"
#include "../event/RoomEvent.hpp"
#include <atomic>
#include <cstdio>
#include <fstream>
#include <string>
#include <chrono>

// Base for all downstream consumers that depend on SlotAssignmentConsumer.
// They spin on upstreamBarrier_ (SlotAssignment's sequence) first, then
// on the ring cursor. This guarantees they never read a ROOM_FREE slot —
// they always see the ROOM_ASSIGNED version that SlotAssignment wrote.

class DownstreamConsumer {
public:
    DownstreamConsumer(RingBuffer<RoomEvent>& ring,
                       Sequence&              seq,
                       Sequence&              upstream)
        : ring_(ring), sequence_(seq), upstreamBarrier_(upstream)
        , running_(true) {}

    virtual ~DownstreamConsumer() = default;

    void run() {
        int64_t nextSeq = sequence_.get() + 1;
        while (running_.load(std::memory_order_relaxed)) {
            // Wait for SlotAssignment to process this seq first
            while (running_.load(std::memory_order_relaxed) &&
                   upstreamBarrier_.get() < nextSeq) {
                YieldingWaitStrategy::wait();
            }
            if (!running_.load(std::memory_order_relaxed))
                break;
            process(ring_.get(nextSeq), nextSeq);
            sequence_.set(nextSeq);
            ++nextSeq;
        }
    }

    void stop() { running_.store(false, std::memory_order_relaxed); }
    Sequence& getSequence() { return sequence_; }

protected:
    virtual void process(const RoomEvent& evt, int64_t seq) = 0;

    RingBuffer<RoomEvent>& ring_;
    Sequence&              sequence_;
    Sequence&              upstreamBarrier_;
    std::atomic<bool>      running_;
};

// ── StudentNotifierConsumer ──────────────────────────────────────────────────
// Pushes room + round info to the student portal / SMS gateway.

class StudentNotifierConsumer : public DownstreamConsumer {
public:
    using DownstreamConsumer::DownstreamConsumer;

protected:
    void process(const RoomEvent& evt, int64_t seq) override {
        if (evt.type != EventType::ROOM_ASSIGNED || evt.company == nullptr)
            return;
        // TODO: replace printf with actual portal REST call / SMS push
        std::printf("[StudentNotify] seq=%-4lld  %s round %d → room %s (%s building)\n",
            (long long)seq,
            evt.company->displayName.c_str(),
            evt.roundNumber,
            evt.room->roomId.c_str(),
            evt.room->building.c_str());
    }
};

// ── OcsDashboardConsumer ─────────────────────────────────────────────────────
// Updates the live room map that OCS coordinators watch.
// Slowest consumer — a few seconds of lag is acceptable here.

class OcsDashboardConsumer : public DownstreamConsumer {
public:
    using DownstreamConsumer::DownstreamConsumer;

protected:
    void process(const RoomEvent& evt, int64_t seq) override {
        if (evt.type != EventType::ROOM_ASSIGNED || evt.room == nullptr)
            return;
        // TODO: push JSON update to dashboard WebSocket
        std::printf("[Dashboard]    seq=%-4lld  room %-8s  status=OCCUPIED  company=%s\n",
            (long long)seq,
            evt.room->roomId.c_str(),
            evt.company ? evt.company->displayName.c_str() : "—");
    }
};

// ── AuditLogConsumer ─────────────────────────────────────────────────────────
// Appends every allocation to a persistent audit trail.
// Used for post-day dispute resolution ("why was Google given A-204 at 11:32?")

class AuditLogConsumer : public DownstreamConsumer {
public:
    AuditLogConsumer(RingBuffer<RoomEvent>& ring,
                     Sequence&              seq,
                     Sequence&              upstream,
                     const std::string&     logPath)
        : DownstreamConsumer(ring, seq, upstream)
        , logFile_(logPath, std::ios::app) {}

protected:
    void process(const RoomEvent& evt, int64_t seq) override {
        if (evt.type == EventType::EMPTY) return;

        char line[512];
        std::snprintf(line, sizeof(line),
            "%lld,%s,%s,%s,%d,%lld\n",
            (long long)seq,
            evt.type == EventType::ROOM_FREE ? "ROOM_FREE" : "ROOM_ASSIGNED",
            evt.room    ? evt.room->roomId.c_str()           : "",
            evt.company ? evt.company->companyId.c_str()     : "",
            evt.roundNumber,
            (long long)evt.timestampMs);

        if (logFile_.is_open())
            logFile_ << line;

        std::printf("[Audit]        seq=%-4lld  %s",
            (long long)seq, line);
    }

private:
    std::ofstream logFile_;
};
