#pragma once
#include <string>
#include <vector>

// ── Enums ────────────────────────────────────────────────────────────────────

enum class Section {
    SECTION_A,   // Core tech: Google, Microsoft, Qualcomm …
    SECTION_B,   // Finance + Consulting: Goldman, McKinsey, BCG …
    SECTION_C,   // PSU + Research: ONGC, ISRO, DRDO …
    SECTION_D    // Overflow / rival separation bucket
};

enum class Sector {
    CORE_TECH,   // Google, Microsoft, Amazon, TI, Qualcomm
    FINANCE,     // Goldman Sachs, JPMorgan, DE Shaw
    CONSULTING,  // McKinsey, BCG, Bain, Deloitte
    PSU,         // ONGC, BHEL, NTPC, Power Grid
    RESEARCH     // ISRO, DRDO, IITM-linked labs
};

// The format of a room determines which round type it can host.
enum class RoomType {
    SEMINAR_HALL,  // 50-100 seats  — PPT / pre-placement talk
    GD_ROOM,       // 12-15 seats   — group discussion
    PANEL_6,       // 6 seats       — technical interview
    PANEL_4,       // 4 seats       — HR / 1-on-1 tech
    LOUNGE         // 20-30 seats   — student waiting area
};

// ── Room ─────────────────────────────────────────────────────────────────────

struct Room {
    std::string roomId;    // e.g. "A-204", "B-GD1"
    RoomType    type;
    Section     section;
    int         capacity;
    std::string building;  // "CLT" or "IC"
    int         floor;

    Room() = default;
    Room(std::string id, RoomType t, Section s, int cap,
         std::string bldg, int flr)
        : roomId(std::move(id)), type(t), section(s)
        , capacity(cap), building(std::move(bldg)), floor(flr) {}
};

// ── Company ──────────────────────────────────────────────────────────────────

struct Company {
    std::string              companyId;
    std::string              displayName;
    Sector                   sector;
    std::vector<std::string> rivalIds;     // must NOT share a section
    RoomType                 primaryRoomType;
    Section                  assignedSection = Section::SECTION_D; // set by SectionRouter

    Company() = default;
    Company(std::string id, std::string name, Sector sec,
            std::vector<std::string> rivals, RoomType roomType)
        : companyId(std::move(id)), displayName(std::move(name))
        , sector(sec), rivalIds(std::move(rivals))
        , primaryRoomType(roomType) {}

    bool isRivalWith(const std::string& otherId) const {
        for (auto& r : rivalIds)
            if (r == otherId) return true;
        return false;
    }
};

// ── Hash support for Section (needed by unordered_map<Section, ...>) ─────────
namespace std {
    template<> struct hash<Section> {
        size_t operator()(Section s) const noexcept {
            return hash<int>{}(static_cast<int>(s));
        }
    };
}
