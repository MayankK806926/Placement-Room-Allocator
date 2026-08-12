#include "core/RingBuffer.hpp"
#include "core/Sequence.hpp"
#include "model/Model.hpp"
#include "event/RoomEvent.hpp"
#include "allocator/SectionRouter.hpp"
#include "allocator/RoomAllocatorProducer.hpp"
#include "scheduler/RoundSchedule.hpp"
#include "scheduler/CompanyQueue.hpp"
#include "scheduler/ConfigLoader.hpp"
#include "consumer/SlotAssignmentConsumer.hpp"
#include "consumer/Consumers.hpp"

#include <thread>
#include <vector>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>

// ── Constants ────────────────────────────────────────────────────────────────

static constexpr size_t RING_SIZE = 32;   // must be power of 2

// ── Boot sequence ────────────────────────────────────────────────────────────
//
//  1. Load room inventory     (rooms.json)
//  2. Load companies + rounds (companies-day1.json + rival-map.json)
//  3. SectionRouter: assign sections, enforce rival separation
//  4. Validate - crash early if any rivals share a section
//  5. Build ring buffer (size 32)
//  6. Wire consumer dependency chain:
//
//       Producer
//          └─► SlotAssignmentConsumer  [seqA]  (BusySpin - latency critical)
//                   ├─► StudentNotifierConsumer [seqB]  waits on seqA  (Yielding)
//                   ├─► OcsDashboardConsumer    [seqC]  waits on seqA  (Yielding)
//                   └─► AuditLogConsumer        [seqD]  waits on seqA  (Sleeping)
//
//     Ring's gating sequences = {seqB, seqC, seqD}
//     Producer blocks only if it would lap the slowest of these three.
//
//  7. Start consumer threads
//  8. Simulate room-free events from interviewers

int main(int argc, char* argv[]) {

    std::string resourceDir = "resources/";
    if (argc > 1) resourceDir = std::string(argv[1]) + "/";

    // ── 1 & 2. Load config ───────────────────────────────────────────────────
    std::printf("[Boot] Loading config from %s\n", resourceDir.c_str());

    std::vector<Room> rooms;
    std::vector<Company> companies;
    RoundScheduleRegistry schedRegistry;

    try {
        rooms     = ConfigLoader::loadRooms(resourceDir + "rooms.json");
        companies = ConfigLoader::loadCompanies(
                        resourceDir + "companies-day1.json",
                        resourceDir + "rival-map.json",
                        schedRegistry);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[Boot] Config load failed: %s\n", e.what());
        return 1;
    }

    std::printf("[Boot] Loaded %zu rooms, %zu companies\n",
        rooms.size(), companies.size());

    // ── 3. Section assignment ────────────────────────────────────────────────
    SectionRouter router;
    router.assignSections(companies);

    // ── 4. Validate ──────────────────────────────────────────────────────────
    auto violations = router.validateAssignments(companies);
    if (!violations.empty()) {
        std::fprintf(stderr, "[Boot] Section assignment violations:\n");
        for (auto& v : violations)
            std::fprintf(stderr, "  %s\n", v.c_str());
        return 1;
    }

    std::printf("[Boot] Section assignments:\n");
    for (auto& c : companies) {
        const char* sec = "D";
        switch (c.assignedSection) {
            case Section::SECTION_A: sec = "A"; break;
            case Section::SECTION_B: sec = "B"; break;
            case Section::SECTION_C: sec = "C"; break;
            default: break;
        }
        std::printf("  %-14s → SECTION_%s\n", c.displayName.c_str(), sec);
    }

    // ── 5. Build ring buffer ─────────────────────────────────────────────────
    // Sequences - all start at -1 ("nothing consumed yet")
    Sequence seqSlotAssign(-1);
    Sequence seqNotifier(-1);
    Sequence seqDashboard(-1);
    Sequence seqAudit(-1);

    // Producer is gated by the three downstream consumers
    RingBuffer<RoomEvent> ring(RING_SIZE,
        { &seqNotifier, &seqDashboard, &seqAudit });

    // ── 6. Build consumers ───────────────────────────────────────────────────
    CompanyQueue companyQueue(companies, schedRegistry);

    SlotAssignmentConsumer  slotConsumer(ring, seqSlotAssign,
                                         companyQueue, schedRegistry);
    StudentNotifierConsumer notifyConsumer(ring, seqNotifier,   seqSlotAssign);
    OcsDashboardConsumer    dashConsumer  (ring, seqDashboard,  seqSlotAssign);
    AuditLogConsumer        auditConsumer (ring, seqAudit,      seqSlotAssign,
                                          "placement_audit.csv");

    // ── 7. Start consumer threads ────────────────────────────────────────────
    std::thread tSlot    ([&]{ slotConsumer.run();   });
    std::thread tNotify  ([&]{ notifyConsumer.run(); });
    std::thread tDash    ([&]{ dashConsumer.run();   });
    std::thread tAudit   ([&]{ auditConsumer.run();  });

    std::printf("[Boot] All consumer threads started. Ring buffer size = %zu\n\n",
        ring.size());

    // ── 8. Simulate room-free events ─────────────────────────────────────────
    // In production this is triggered by interviewers tapping "Round done"
    // in the OCS portal. Here we simulate a Day 1 morning burst.

    RoomAllocatorProducer producer(ring);

    // Build a quick lookup: roomId → Room*
    auto findRoom = [&](const std::string& id) -> Room* {
        for (auto& r : rooms)
            if (r.roomId == id) return &r;
        return nullptr;
    };

    // Simulate: interviews finishing across sections throughout the morning
    struct SimEvent { const char* roomId; int delayMs; };
    SimEvent simEvents[] = {
        { "A-SEM",  100 },   // Google PPT finishes → A-SEM free
        { "C-SEM",  150 },   // ONGC written test finishes
        { "B-GD1",  200 },   // McKinsey GD finishes
        { "A-201",  250 },   // Google tech round 2 finishes
        { "B-GD2",  300 },   // BCG GD finishes
        { "A-202",  350 },   // Google tech round 3 finishes
        { "B-101",  400 },   // Goldman HR finishes
        { "C-301",  450 },   // ONGC interview finishes
        { "D-SEM",  500 },   // Microsoft PPT finishes (in SECTION_D due to rival conflict)
        { "A-204",  550 },   // Google HR finishes
    };

    for (auto& ev : simEvents) {
        std::this_thread::sleep_for(std::chrono::milliseconds(ev.delayMs));
        Room* room = findRoom(ev.roomId);
        if (room) {
            std::printf("\n[Sim] Interviewer marks %s as FREE\n", ev.roomId);
            producer.publishRoomFree(room);
        } else {
            std::fprintf(stderr, "[Sim] Room %s not found in inventory\n", ev.roomId);
        }
    }

    // Let consumers drain all published events
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // ── Shutdown ─────────────────────────────────────────────────────────────
    std::printf("\n[Boot] Shutting down...\n");
    slotConsumer.stop();
    notifyConsumer.stop();
    dashConsumer.stop();
    auditConsumer.stop();

    tSlot.join();
    tNotify.join();
    tDash.join();
    tAudit.join();

    std::printf("[Boot] All threads joined. Audit log written to placement_audit.csv\n");
    return 0;
}
