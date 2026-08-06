#pragma once
#include "../model/Model.hpp"
#include "RoundSchedule.hpp"
#include <queue>
#include <unordered_map>
#include <mutex>
#include <string>

// Per-section FIFO queue of companies waiting for a room.
//
// SlotAssignmentConsumer calls pollNext(section, roomType) to find the next
// company whose *current* round needs exactly that room type.
//
// Uses RoundScheduleRegistry to know what room type each company's current
// round requires — so the match is round-aware, not just company-level.
//
// The mutex here is intentional and lightweight: accessed from the consumer
// thread and potentially from a coordinator UI thread. Critical section is
// a tiny queue scan (2–8 items max per section).

class CompanyQueue {
public:
    CompanyQueue(std::vector<Company>& companies,
                 const RoundScheduleRegistry& schedRegistry)
        : schedRegistry_(schedRegistry)
    {
        for (auto& c : companies) {
            queues_[c.assignedSection].push(&c);
            roundTracker_[c.companyId] = 1;
        }
    }

    // Returns the next Company in this section whose current round needs
    // this room type, or nullptr if none is waiting.
    Company* pollNext(Section section, RoomType neededType) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& q = queues_[section];
        size_t n = q.size();
        for (size_t i = 0; i < n; ++i) {
            Company* front = q.front();
            q.pop();
            int currentRound = roundTracker_[front->companyId];
            RoomType needed  = schedRegistry_.roomTypeFor(front->companyId, currentRound);
            if (needed == neededType) {
                return front;   // match — don't re-enqueue yet
            }
            q.push(front);      // rotate to back
        }
        return nullptr;
    }

    // Call after a room has been assigned; advances the company's round counter.
    // Returns the new round number.
    int advanceRound(const std::string& companyId) {
        std::lock_guard<std::mutex> lock(mutex_);
        return ++roundTracker_[companyId];
    }

    int getCurrentRound(const std::string& companyId) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = roundTracker_.find(companyId);
        return (it != roundTracker_.end()) ? it->second : 1;
    }

    // Re-enqueue a company after its round is done so it can claim the next room.
    void reEnqueue(Company* company) {
        std::lock_guard<std::mutex> lock(mutex_);
        queues_[company->assignedSection].push(company);
    }

    // Returns true if there are no companies waiting in any section.
    bool allQueuesEmpty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [sec, q] : queues_)
            if (!q.empty()) return false;
        return true;
    }

private:
    const RoundScheduleRegistry&                           schedRegistry_;
    std::unordered_map<Section, std::queue<Company*>>      queues_;
    std::unordered_map<std::string, int>                   roundTracker_;
    mutable std::mutex                                     mutex_;
};
