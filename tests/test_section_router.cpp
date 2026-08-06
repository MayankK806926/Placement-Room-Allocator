#include "allocator/SectionRouter.hpp"
#include <cassert>
#include <cstdio>

// ── Test 1: Same-sector companies go to the same section ─────────────────────
static void test_same_sector_same_section() {
    std::vector<Company> companies = {
        Company("GOOGLE", "Google", Sector::CORE_TECH, {}, RoomType::PANEL_6),
        Company("AMAZON", "Amazon", Sector::CORE_TECH, {}, RoomType::PANEL_6),
    };

    SectionRouter router;
    router.assignSections(companies);

    assert(companies[0].assignedSection == Section::SECTION_A);
    assert(companies[1].assignedSection == Section::SECTION_A);

    std::printf("[PASS] test_same_sector_same_section\n");
}

// ── Test 2: Rivals must not share a section ──────────────────────────────────
static void test_rivals_get_different_sections() {
    std::vector<Company> companies = {
        Company("GOOGLE",    "Google",    Sector::CORE_TECH, {"MICROSOFT"}, RoomType::PANEL_6),
        Company("MICROSOFT", "Microsoft", Sector::CORE_TECH, {"GOOGLE"},    RoomType::PANEL_6),
    };

    SectionRouter router;
    router.assignSections(companies);

    assert(companies[0].assignedSection != companies[1].assignedSection);

    auto violations = router.validateAssignments(companies);
    assert(violations.empty());

    std::printf("[PASS] test_rivals_get_different_sections\n");
}

// ── Test 3: Non-rival companies in same sector are fine together ─────────────
static void test_no_false_separation() {
    std::vector<Company> companies = {
        Company("GOLDMAN",        "Goldman",     Sector::FINANCE, {},          RoomType::PANEL_4),
        Company("MORGAN_STANLEY", "Morgan Stanley", Sector::FINANCE, {},       RoomType::PANEL_4),
        Company("JPMORGAN",       "JPMorgan",    Sector::FINANCE, {},          RoomType::PANEL_4),
    };

    SectionRouter router;
    router.assignSections(companies);

    // All three should be in SECTION_B (Finance default)
    for (auto& c : companies)
        assert(c.assignedSection == Section::SECTION_B);

    auto violations = router.validateAssignments(companies);
    assert(violations.empty());

    std::printf("[PASS] test_no_false_separation\n");
}

// ── Test 4: Validate catches remaining conflicts ──────────────────────────────
static void test_validate_catches_conflict() {
    // Manually put two rivals in the same section to test the validator
    Company a("MCKINSEY", "McKinsey", Sector::CONSULTING, {"BCG"}, RoomType::GD_ROOM);
    Company b("BCG",      "BCG",      Sector::CONSULTING, {"MCKINSEY"}, RoomType::GD_ROOM);

    a.assignedSection = Section::SECTION_B;
    b.assignedSection = Section::SECTION_B;  // force conflict

    std::vector<Company> companies = { a, b };
    SectionRouter router;
    auto violations = router.validateAssignments(companies);

    assert(!violations.empty());
    std::printf("[PASS] test_validate_catches_conflict\n");
}

// ── Test 5: PSU companies go to SECTION_C ───────────────────────────────────
static void test_psu_to_section_c() {
    std::vector<Company> companies = {
        Company("ONGC", "ONGC", Sector::PSU,      {}, RoomType::SEMINAR_HALL),
        Company("ISRO", "ISRO", Sector::RESEARCH,  {}, RoomType::SEMINAR_HALL),
    };

    SectionRouter router;
    router.assignSections(companies);

    assert(companies[0].assignedSection == Section::SECTION_C);
    assert(companies[1].assignedSection == Section::SECTION_C);

    std::printf("[PASS] test_psu_to_section_c\n");
}

int main() {
    std::printf("=== SectionRouter Tests ===\n");
    test_same_sector_same_section();
    test_rivals_get_different_sections();
    test_no_false_separation();
    test_validate_catches_conflict();
    test_psu_to_section_c();
    std::printf("All SectionRouter tests passed.\n");
    return 0;
}
