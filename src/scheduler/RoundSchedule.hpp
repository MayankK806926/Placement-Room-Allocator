#pragma once
#include "../model/Model.hpp"
#include <vector>
#include <string>
#include <unordered_map>

// Describes what interview rounds a company runs and in what order.
// Loaded from companies-day1.json at startup.
// The CompanyQueue uses this to know what RoomType the next round needs.

enum class RoundType {
    PPT,        // Pre-placement talk   → SEMINAR_HALL
    GD,         // Group discussion     → GD_ROOM
    TECH,       // Technical interview  → PANEL_6
    CASE,       // Case study           → PANEL_4
    HR,         // HR interview         → PANEL_4
    WRITTEN,    // Written test         → SEMINAR_HALL
    INTERVIEW   // Generic interview    → PANEL_4
};

struct Round {
    int        roundNumber;
    RoundType  type;
    RoomType   roomType;       // what kind of room this round needs
    int        durationMin;    // expected duration in minutes
};

struct CompanySchedule {
    std::string        companyId;
    std::vector<Round> rounds;

    // Returns the RoomType needed for the given round number.
    // Returns RoomType::PANEL_4 as default if round not found.
    RoomType roomTypeForRound(int roundNum) const {
        for (auto& r : rounds)
            if (r.roundNumber == roundNum) return r.roomType;
        return RoomType::PANEL_4;
    }

    int totalRounds() const {
        return static_cast<int>(rounds.size());
    }
};

// Registry: companyId → CompanySchedule
// Built by ConfigLoader at startup.
class RoundScheduleRegistry {
public:
    void add(CompanySchedule schedule) {
        registry_[schedule.companyId] = std::move(schedule);
    }

    // Returns nullptr if company not found
    const CompanySchedule* get(const std::string& companyId) const {
        auto it = registry_.find(companyId);
        return (it != registry_.end()) ? &it->second : nullptr;
    }

    RoomType roomTypeFor(const std::string& companyId, int roundNum) const {
        auto* sched = get(companyId);
        return sched ? sched->roomTypeForRound(roundNum) : RoomType::PANEL_4;
    }

private:
    std::unordered_map<std::string, CompanySchedule> registry_;
};
