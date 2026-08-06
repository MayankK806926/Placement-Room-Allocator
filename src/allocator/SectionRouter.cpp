#include "SectionRouter.hpp"
#include <cstdio>
#include <algorithm>
#include <map>

static const char* sectionName(Section s) {
    switch (s) {
        case Section::SECTION_A: return "SECTION_A";
        case Section::SECTION_B: return "SECTION_B";
        case Section::SECTION_C: return "SECTION_C";
        default:                 return "SECTION_D";
    }
}

SectionRouter::SectionRouter() {
    sectorDefaultSection_[static_cast<int>(Sector::CORE_TECH)]  = Section::SECTION_A;
    sectorDefaultSection_[static_cast<int>(Sector::FINANCE)]    = Section::SECTION_B;
    sectorDefaultSection_[static_cast<int>(Sector::CONSULTING)] = Section::SECTION_B;
    sectorDefaultSection_[static_cast<int>(Sector::PSU)]        = Section::SECTION_C;
    sectorDefaultSection_[static_cast<int>(Sector::RESEARCH)]   = Section::SECTION_C;
}

Section SectionRouter::defaultSectionFor(Sector sector) const {
    auto it = sectorDefaultSection_.find(static_cast<int>(sector));
    return (it != sectorDefaultSection_.end()) ? it->second : Section::SECTION_D;
}

bool SectionRouter::isConflictFree(
    const Company& candidate,
    Section section,
    const std::map<Section, std::vector<const Company*>>& occupants) const
{
    auto it = occupants.find(section);
    if (it == occupants.end()) return true;

    for (const Company* existing : it->second) {
        if (candidate.isRivalWith(existing->companyId) ||
            existing->isRivalWith(candidate.companyId)) {
            return false;
        }
    }
    return true;
}

Section SectionRouter::resolveSection(
    const Company& candidate,
    Section preferred,
    const std::map<Section, std::vector<const Company*>>& occupants) const
{
    if (isConflictFree(candidate, preferred, occupants))
        return preferred;

    std::printf("[SectionRouter] Rival conflict in preferred section for %s"
                " → looking for alternate section\n",
                candidate.displayName.c_str());

    // SECTION_D is the designated overflow bucket — try it before anything
    // else, but it must be checked for conflicts too: a previous rival may
    // already have been bumped there.
    static constexpr Section fallbackOrder[] = {
        Section::SECTION_D, Section::SECTION_A,
        Section::SECTION_B, Section::SECTION_C
    };
    for (Section candidateSection : fallbackOrder) {
        if (candidateSection == preferred) continue;
        if (isConflictFree(candidate, candidateSection, occupants)) {
            std::printf("[SectionRouter]   → routing %s to %s\n",
                        candidate.displayName.c_str(),
                        sectionName(candidateSection));
            return candidateSection;
        }
    }

    // Every section conflicts (pathological input, e.g. a rival clique
    // spanning all four sections). Fall back to preferred; validateAssignments()
    // will surface this as a boot-time error instead of allocating silently.
    std::printf("[SectionRouter]   WARNING: no conflict-free section found for %s\n",
                candidate.displayName.c_str());
    return preferred;
}

void SectionRouter::assignSections(std::vector<Company>& companies) {
    // Track who is already in each section
    std::map<Section, std::vector<const Company*>> occupants;

    for (auto& company : companies) {
        Section preferred = defaultSectionFor(company.sector);
        Section assigned  = resolveSection(company, preferred, occupants);

        company.assignedSection = assigned;
        occupants[assigned].push_back(&company);
    }
}

std::vector<std::string> SectionRouter::validateAssignments(
    const std::vector<Company>& companies) const
{
    std::vector<std::string> violations;

    for (size_t i = 0; i < companies.size(); ++i) {
        for (size_t j = i + 1; j < companies.size(); ++j) {
            const auto& a = companies[i];
            const auto& b = companies[j];
            if (a.assignedSection == b.assignedSection &&
                (a.isRivalWith(b.companyId) || b.isRivalWith(a.companyId)))
            {
                char buf[256];
                std::snprintf(buf, sizeof(buf),
                    "CONFLICT: %s and %s are rivals but both assigned to same section",
                    a.displayName.c_str(), b.displayName.c_str());
                violations.emplace_back(buf);
            }
        }
    }
    return violations;
}
