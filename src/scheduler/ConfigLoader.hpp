#pragma once
// Minimal JSON parser — no external dependencies (no nlohmann, no rapidjson).
// Parses the three config files using simple string scanning.
// Good enough for structured config files with predictable schema.
// For production, swap with nlohmann/json or simdjson.

#include "../model/Model.hpp"
#include "../scheduler/RoundSchedule.hpp"
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <stdexcept>
#include <algorithm>
#include <set>

// ── Tiny JSON helpers ────────────────────────────────────────────────────────

static std::string extractString(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return "";
    pos = json.find('"', pos + needle.size() + 1); // skip : and whitespace
    if (pos == std::string::npos) return "";
    auto end = json.find('"', pos + 1);
    return json.substr(pos + 1, end - pos - 1);
}

static int extractInt(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return 0;
    pos = json.find_first_of("-0123456789", pos + needle.size());
    if (pos == std::string::npos) return 0;
    return std::stoi(json.substr(pos));
}

static std::vector<std::string> extractStringArray(const std::string& json,
                                                    const std::string& key) {
    std::vector<std::string> result;
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return result;
    auto arrStart = json.find('[', pos);
    auto arrEnd   = json.find(']', arrStart);
    if (arrStart == std::string::npos || arrEnd == std::string::npos) return result;
    std::string arr = json.substr(arrStart + 1, arrEnd - arrStart - 1);
    size_t p = 0;
    while ((p = arr.find('"', p)) != std::string::npos) {
        auto e = arr.find('"', p + 1);
        result.push_back(arr.substr(p + 1, e - p - 1));
        p = e + 1;
    }
    return result;
}

// Split a JSON array string "[ {...}, {...} ]" into individual object strings
static std::vector<std::string> splitObjects(const std::string& json) {
    std::vector<std::string> objects;
    int depth = 0;
    size_t start = std::string::npos;
    for (size_t i = 0; i < json.size(); ++i) {
        if (json[i] == '{') {
            if (depth == 0) start = i;
            ++depth;
        } else if (json[i] == '}') {
            --depth;
            if (depth == 0 && start != std::string::npos)
                objects.push_back(json.substr(start, i - start + 1));
        }
    }
    return objects;
}

static std::string readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open())
        throw std::runtime_error("Cannot open config file: " + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// ── Enum converters ──────────────────────────────────────────────────────────

static RoomType toRoomType(const std::string& s) {
    if (s == "SEMINAR_HALL") return RoomType::SEMINAR_HALL;
    if (s == "GD_ROOM")      return RoomType::GD_ROOM;
    if (s == "PANEL_6")      return RoomType::PANEL_6;
    if (s == "LOUNGE")       return RoomType::LOUNGE;
    return RoomType::PANEL_4;
}

static Section toSection(const std::string& s) {
    if (s == "SECTION_A") return Section::SECTION_A;
    if (s == "SECTION_B") return Section::SECTION_B;
    if (s == "SECTION_C") return Section::SECTION_C;
    return Section::SECTION_D;
}

static Sector toSector(const std::string& s) {
    if (s == "CORE_TECH")  return Sector::CORE_TECH;
    if (s == "FINANCE")    return Sector::FINANCE;
    if (s == "CONSULTING") return Sector::CONSULTING;
    if (s == "PSU")        return Sector::PSU;
    return Sector::RESEARCH;
}

static RoundType toRoundType(const std::string& s) {
    if (s == "PPT")      return RoundType::PPT;
    if (s == "GD")       return RoundType::GD;
    if (s == "TECH")     return RoundType::TECH;
    if (s == "CASE")     return RoundType::CASE;
    if (s == "HR")       return RoundType::HR;
    if (s == "WRITTEN")  return RoundType::WRITTEN;
    return RoundType::INTERVIEW;
}

// ── ConfigLoader ─────────────────────────────────────────────────────────────

class ConfigLoader {
public:
    // Load room inventory from rooms.json
    static std::vector<Room> loadRooms(const std::string& path) {
        std::string json = readFile(path);
        std::vector<Room> rooms;
        for (auto& obj : splitObjects(json)) {
            rooms.emplace_back(
                extractString(obj, "roomId"),
                toRoomType(extractString(obj, "type")),
                toSection(extractString(obj, "section")),
                extractInt(obj, "capacity"),
                extractString(obj, "building"),
                extractInt(obj, "floor")
            );
        }
        return rooms;
    }

    // Load rival pairs from rival-map.json and return a set of sorted pairs
    // e.g. { {"BCG","MCKINSEY"}, {"GOOGLE","MICROSOFT"} }
    static std::set<std::pair<std::string,std::string>>
    loadRivalPairs(const std::string& path) {
        std::set<std::pair<std::string,std::string>> pairs;
        std::string json = readFile(path);

        // Find the rivalPairs array
        auto arrStart = json.find("\"rivalPairs\"");
        if (arrStart == std::string::npos) return pairs;

        for (auto& obj : splitObjects(json.substr(arrStart))) {
            auto companies = extractStringArray(obj, "companies");
            if (companies.size() >= 2) {
                auto a = companies[0], b = companies[1];
                if (a > b) std::swap(a, b);
                pairs.insert({a, b});
            }
        }
        return pairs;
    }

    // Load companies and their round schedules from companies-day1.json
    // Also injects rival relationships from the rival map.
    static std::vector<Company> loadCompanies(
        const std::string& companiesPath,
        const std::string& rivalMapPath,
        RoundScheduleRegistry& registry)
    {
        auto rivalPairs = loadRivalPairs(rivalMapPath);

        // Build rivalId lookup: companyId → list of rivals
        std::unordered_map<std::string, std::vector<std::string>> rivalMap;
        for (auto& [a, b] : rivalPairs) {
            rivalMap[a].push_back(b);
            rivalMap[b].push_back(a);
        }

        std::string json = readFile(companiesPath);
        std::vector<Company> companies;

        for (auto& obj : splitObjects(json)) {
            std::string id   = extractString(obj, "companyId");
            std::string name = extractString(obj, "displayName");
            Sector sector    = toSector(extractString(obj, "sector"));
            RoomType prt     = toRoomType(extractString(obj, "primaryRoomType"));

            // Merge rivals from rival-map.json (authoritative) and inline "rivals" field
            std::vector<std::string> rivals;
            auto it = rivalMap.find(id);
            if (it != rivalMap.end()) rivals = it->second;

            for (auto& inlineRival : extractStringArray(obj, "rivals")) {
                if (std::find(rivals.begin(), rivals.end(), inlineRival) == rivals.end())
                    rivals.push_back(inlineRival);
            }

            companies.emplace_back(id, name, sector, rivals, prt);

            // Parse rounds for this company
            CompanySchedule sched;
            sched.companyId = id;

            // Find the rounds sub-array within this company object
            auto rpos = obj.find("\"rounds\"");
            if (rpos != std::string::npos) {
                auto rArrStart = obj.find('[', rpos);
                auto rArrEnd   = obj.find(']', rArrStart);
                if (rArrStart != std::string::npos && rArrEnd != std::string::npos) {
                    std::string roundsJson = obj.substr(rArrStart, rArrEnd - rArrStart + 1);
                    for (auto& robj : splitObjects(roundsJson)) {
                        Round r;
                        r.roundNumber = extractInt(robj, "roundNumber");
                        r.type        = toRoundType(extractString(robj, "type"));
                        r.roomType    = toRoomType(extractString(robj, "roomType"));
                        r.durationMin = extractInt(robj, "durationMinutes");
                        sched.rounds.push_back(r);
                    }
                }
            }
            registry.add(std::move(sched));
        }

        return companies;
    }
};
