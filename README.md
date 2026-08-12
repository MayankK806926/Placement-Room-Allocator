# Placement Room Allocator - IIT Madras OCS
### Ring Buffer (Disruptor pattern) in C++17

---

## The Problem

On Day 1 of placements, rooms across CLT and IC buildings cycle rapidly:
PPT → GD → Tech interviews → HR → free again, all day, across multiple companies simultaneously.

Two core constraints make this non-trivial:

1. **Rival companies must not share a section** - Google and Microsoft can't be in the same corridor. Candidates compare offers, interviewers cross paths.
2. **Room type must match the round** - a GD needs a 15-seat room, a PPT needs the seminar hall, a tech interview needs a PANEL_6.

The naive approach (coordinators on WhatsApp, shared Google Sheets) breaks under load and violates constraint 1 accidentally all the time.

---

## Architecture

```
Interviewer taps "Round done" in portal
              │
              ▼
  [RoomAllocatorProducer]
      ring.next()         ← atomic increment, no lock
      event.setRoomFree()
      ring.publish(seq)   ← memory barrier
              │
              ▼  (single ring buffer, size=32)
  ┌──────────────────────────────────────────┐
  │  slot 0 │ slot 1 │ ... │ slot 31         │  ← pre-allocated, reused all day
  └──────────────────────────────────────────┘
              │
     ┌────────┘
     ▼
  [SlotAssignmentConsumer]  seqA   BusySpin  - latency critical
      pollNext(section, roomType)
      setRoomAssigned(room, company, round)
      advanceRound + reEnqueue
     │
     ├──► [StudentNotifierConsumer]  seqB   Yielding  - waits on seqA
     │        push room+round to student portal
     │
     ├──► [OcsDashboardConsumer]    seqC   Yielding  - waits on seqA
     │        update live coordinator room map
     │
     └──► [AuditLogConsumer]        seqD   Sleeping  - waits on seqA
              append to placement_audit.csv

Producer gates on min(seqB, seqC, seqD) - cannot lap slowest consumer.
Zero mutexes in the hot path. One mutex only in CompanyQueue (tiny, cold path).
```

---

## Section Assignment (done once before day starts)

| Section   | Sector            | Companies (example)           |
|-----------|-------------------|-------------------------------|
| SECTION_A | CORE_TECH         | Google, Amazon, Qualcomm, TI  |
| SECTION_B | FINANCE/CONSULTING| Goldman, McKinsey, BCG        |
| SECTION_C | PSU/RESEARCH      | ONGC, ISRO, DRDO              |
| SECTION_D | Overflow/Rivals   | Microsoft (rival of Google)   |

Rival pairs are defined in `rival-map.json`. When a rival conflict is detected,
the later company is bumped to SECTION_D. Validated before the ring starts.

---

## File Structure

```
placement-room-allocator/
├── CMakeLists.txt
├── README.md
│
├── src/
│   ├── main.cpp                              Entry point, wires everything
│   │
│   ├── core/
│   │   ├── Sequence.hpp                      Cache-line padded atomic counter
│   │   ├── RingBuffer.hpp                    Templated fixed circular array
│   │   └── WaitStrategy.hpp                  BusySpin / Yielding / Sleeping
│   │
│   ├── model/
│   │   └── Model.hpp                         Room, Company, Section, Sector, RoomType enums
│   │
│   ├── event/
│   │   └── RoomEvent.hpp                     Data written into ring slots
│   │
│   ├── allocator/
│   │   ├── SectionRouter.hpp / .cpp          Assigns sections, enforces rival separation
│   │   └── RoomAllocatorProducer.hpp         Writes ROOM_FREE events into the ring
│   │
│   ├── scheduler/
│   │   ├── RoundSchedule.hpp                 Per-company round definitions + registry
│   │   ├── CompanyQueue.hpp                  Per-section FIFO, round-aware room matching
│   │   └── ConfigLoader.hpp                  Parses rooms.json / companies-day1.json / rival-map.json
│   │
│   └── consumer/
│       ├── SlotAssignmentConsumer.hpp        Primary consumer: matches rooms to companies
│       └── Consumers.hpp                     StudentNotifier, OcsDashboard, AuditLog
│
├── resources/
│   ├── rooms.json                            Static room inventory (CLT + IC)
│   ├── companies-day1.json                   Companies + round schedules for Day 1
│   └── rival-map.json                        Rival pairs (cannot share section)
│
└── tests/
    ├── test_ring_buffer.cpp                  Ring correctness, wraparound, blocking
    ├── test_section_router.cpp               Rival separation, sector defaults
    └── test_company_queue.cpp                pollNext matching, round advancement
```

---

## Build & Run

**Requirements:** GCC 10+ or Clang 12+, CMake 3.16+, pthreads

```bash
# Configure + build (Release mode)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Run
./build/placement_allocator

# Run with custom resource directory
./build/placement_allocator /path/to/resources

# Run tests
ctest --test-dir build -V
```

**Debug build with ThreadSanitizer** (catches data races):
```bash
cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug
./build-debug/placement_allocator
```

---

## Key Design Decisions

| Decision | Reason |
|---|---|
| Ring size = power of 2 | `seq & mask` replaces `seq % size` - no division in hot path |
| All RoomEvents pre-allocated | Zero heap allocation during the placement day |
| `Sequence` padded to cache line | Prevents false sharing between producer and consumer counters |
| `memory_order_acquire/release` | Correct happens-before without full `seq_cst` barrier cost |
| SectionRouter runs before ring starts | Section assignment is a cold-path config step, not a hot-path concern |
| One mutex only in CompanyQueue | The queue is accessed from coordinator UI thread too; section sizes are tiny (2–8 items) |
| SlotAssignment mutates the event slot | Downstream consumers see the completed ROOM_ASSIGNED, never the raw ROOM_FREE |
