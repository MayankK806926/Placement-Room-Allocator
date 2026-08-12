#pragma once
#include "../model/Model.hpp"
#include <vector>
#include <string>
#include <map>
#include <unordered_map>

// Assigns building sections to companies ONCE before the day begins.
// Section assignments are fixed for the entire day - they do not change
// as rooms are allocated and freed through the ring buffer.
//
// Rules (in priority order):
//   1. Same-sector companies go to the same section by default.
//   2. If a rival company already occupies that section, bump to SECTION_D.
//   3. After all assignments, validateAssignments() catches any remaining
//      conflicts (e.g. two rivals both pushed to SECTION_D).

class SectionRouter {
public:
    SectionRouter();

    // Fills company.assignedSection for every company in the list.
    // Call once at day start, before the ring buffer begins processing.
    void assignSections(std::vector<Company>& companies);

    // Returns list of conflict strings. Empty = all good.
    std::vector<std::string> validateAssignments(
        const std::vector<Company>& companies) const;

private:
    Section defaultSectionFor(Sector sector) const;

    // True if `candidate` has no rival already occupying `section`.
    bool isConflictFree(
        const Company&                     candidate,
        Section                            section,
        const std::map<Section, std::vector<const Company*>>& occupants) const;

    // Tries `preferred` first, then SECTION_D, then every other section, and
    // returns the first one with no rival conflict. Falls back to `preferred`
    // if every section conflicts (validateAssignments() will flag it).
    Section resolveSection(
        const Company&                     candidate,
        Section                            preferred,
        const std::map<Section, std::vector<const Company*>>& occupants) const;

    // Maps Sector → default Section
    std::unordered_map<int, Section> sectorDefaultSection_;
};
