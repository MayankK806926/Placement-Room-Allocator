#include "scheduler/CompanyQueue.hpp"
#include "scheduler/RoundSchedule.hpp"
#include "allocator/SectionRouter.hpp"
#include <cassert>
#include <cstdio>

// Helper: build a RoundScheduleRegistry with a single company's rounds
static RoundScheduleRegistry makeRegistry(
    const std::string& companyId,
    std::vector<std::pair<int, RoomType>> rounds)
{
    RoundScheduleRegistry reg;
    CompanySchedule sched;
    sched.companyId = companyId;
    int n = 1;
    for (auto& [rn, rt] : rounds) {
        Round r;
        r.roundNumber = n++;
        r.type        = RoundType::TECH;
        r.roomType    = rt;
        r.durationMin = 60;
        sched.rounds.push_back(r);
    }
    reg.add(std::move(sched));
    return reg;
}

// ── Test 1: pollNext returns correct company for matching room type ───────────
static void test_poll_returns_matching_company() {
    RoundScheduleRegistry reg;
    {
        CompanySchedule s; s.companyId = "GOOGLE";
        Round r; r.roundNumber = 1; r.roomType = RoomType::SEMINAR_HALL;
        r.type = RoundType::PPT; r.durationMin = 45;
        s.rounds.push_back(r);
        reg.add(std::move(s));
    }

    std::vector<Company> companies = {
        Company("GOOGLE", "Google", Sector::CORE_TECH, {}, RoomType::PANEL_6),
    };
    companies[0].assignedSection = Section::SECTION_A;

    CompanyQueue queue(companies, reg);

    Company* result = queue.pollNext(Section::SECTION_A, RoomType::SEMINAR_HALL);
    assert(result != nullptr);
    assert(result->companyId == "GOOGLE");

    std::printf("[PASS] test_poll_returns_matching_company\n");
}

// ── Test 2: pollNext returns nullptr if no company needs that room type ───────
static void test_poll_returns_null_for_wrong_type() {
    RoundScheduleRegistry reg;
    {
        CompanySchedule s; s.companyId = "ONGC";
        Round r; r.roundNumber = 1; r.roomType = RoomType::SEMINAR_HALL;
        r.type = RoundType::PPT; r.durationMin = 90;
        s.rounds.push_back(r);
        reg.add(std::move(s));
    }

    std::vector<Company> companies = {
        Company("ONGC", "ONGC", Sector::PSU, {}, RoomType::SEMINAR_HALL),
    };
    companies[0].assignedSection = Section::SECTION_C;

    CompanyQueue queue(companies, reg);

    // ONGC needs SEMINAR_HALL for round 1, not PANEL_6
    Company* result = queue.pollNext(Section::SECTION_C, RoomType::PANEL_6);
    assert(result == nullptr);

    std::printf("[PASS] test_poll_returns_null_for_wrong_type\n");
}

// ── Test 3: Round advancement works correctly ────────────────────────────────
static void test_round_advancement() {
    RoundScheduleRegistry reg;
    {
        CompanySchedule s; s.companyId = "MCKINSEY";
        for (int i = 1; i <= 3; ++i) {
            Round r; r.roundNumber = i;
            r.roomType = (i == 1) ? RoomType::SEMINAR_HALL :
                         (i == 2) ? RoomType::GD_ROOM : RoomType::PANEL_4;
            r.type = RoundType::TECH; r.durationMin = 60;
            s.rounds.push_back(r);
        }
        reg.add(std::move(s));
    }

    std::vector<Company> companies = {
        Company("MCKINSEY", "McKinsey", Sector::CONSULTING, {}, RoomType::GD_ROOM),
    };
    companies[0].assignedSection = Section::SECTION_B;

    CompanyQueue queue(companies, reg);

    assert(queue.getCurrentRound("MCKINSEY") == 1);

    int r2 = queue.advanceRound("MCKINSEY");
    assert(r2 == 2);
    assert(queue.getCurrentRound("MCKINSEY") == 2);

    std::printf("[PASS] test_round_advancement\n");
}

// ── Test 4: Two companies in same section — correct one is returned ──────────
static void test_two_companies_correct_match() {
    RoundScheduleRegistry reg;
    // Google needs SEMINAR_HALL for round 1
    { CompanySchedule s; s.companyId = "GOOGLE";
      Round r; r.roundNumber=1; r.roomType=RoomType::SEMINAR_HALL;
      r.type=RoundType::PPT; r.durationMin=45; s.rounds.push_back(r);
      reg.add(std::move(s)); }
    // Amazon needs GD_ROOM for round 1
    { CompanySchedule s; s.companyId = "AMAZON";
      Round r; r.roundNumber=1; r.roomType=RoomType::GD_ROOM;
      r.type=RoundType::GD; r.durationMin=45; s.rounds.push_back(r);
      reg.add(std::move(s)); }

    std::vector<Company> companies = {
        Company("GOOGLE", "Google", Sector::CORE_TECH, {}, RoomType::PANEL_6),
        Company("AMAZON", "Amazon", Sector::CORE_TECH, {}, RoomType::PANEL_6),
    };
    companies[0].assignedSection = Section::SECTION_A;
    companies[1].assignedSection = Section::SECTION_A;

    CompanyQueue queue(companies, reg);

    // A GD_ROOM becomes free — should go to Amazon, not Google
    Company* result = queue.pollNext(Section::SECTION_A, RoomType::GD_ROOM);
    assert(result != nullptr);
    assert(result->companyId == "AMAZON");

    std::printf("[PASS] test_two_companies_correct_match\n");
}

// ── Test 5: reEnqueue puts company back for next round ───────────────────────
static void test_reenqueue_after_round() {
    RoundScheduleRegistry reg;
    { CompanySchedule s; s.companyId = "BCG";
      Round r1; r1.roundNumber=1; r1.roomType=RoomType::GD_ROOM;
      r1.type=RoundType::GD; r1.durationMin=60;
      Round r2; r2.roundNumber=2; r2.roomType=RoomType::PANEL_4;
      r2.type=RoundType::HR; r2.durationMin=30;
      s.rounds.push_back(r1); s.rounds.push_back(r2);
      reg.add(std::move(s)); }

    std::vector<Company> companies = {
        Company("BCG", "BCG", Sector::CONSULTING, {}, RoomType::GD_ROOM),
    };
    companies[0].assignedSection = Section::SECTION_B;

    CompanyQueue queue(companies, reg);

    // Round 1: poll for GD_ROOM
    Company* c = queue.pollNext(Section::SECTION_B, RoomType::GD_ROOM);
    assert(c != nullptr && c->companyId == "BCG");

    // Advance to round 2 and re-enqueue
    queue.advanceRound("BCG");
    queue.reEnqueue(c);

    // Round 2: poll for PANEL_4
    Company* c2 = queue.pollNext(Section::SECTION_B, RoomType::PANEL_4);
    assert(c2 != nullptr && c2->companyId == "BCG");

    std::printf("[PASS] test_reenqueue_after_round\n");
}

int main() {
    std::printf("=== CompanyQueue Tests ===\n");
    test_poll_returns_matching_company();
    test_poll_returns_null_for_wrong_type();
    test_round_advancement();
    test_two_companies_correct_match();
    test_reenqueue_after_round();
    std::printf("All CompanyQueue tests passed.\n");
    return 0;
}
