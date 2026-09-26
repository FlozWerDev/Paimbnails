#pragma once
// Search history data model and persistence.
// Based on "Search History" by hiimjasmine00
// (https://github.com/hiimjasmine00/SearchHistory, Geode id
// hiimjustin000.search_history) under the MIT License.
// Copyright (c) 2024-2026 hiimjasmine00. See THIRD-PARTY-NOTICES.md.
// Local port retains the upstream capture/restore logic with Paimbnails
// UI and persistence.

#include <matjson.hpp>
#include <cstdint>
#include <string>
#include <vector>

class GJSearchObject;

namespace paimon::searchhistory {

// One entry: the query plus the full filter snapshot.
struct Entry {
    int64_t time = 0;            // epoch seconds (date display)
    int type = 0;                // 0 = levels, 1 = lists, 2 = users
    std::string query;
    std::vector<int> difficulties;
    std::vector<int> lengths;
    bool uncompleted = false;
    bool completed = false;
    bool featured = false;
    bool original = false;
    bool twoPlayer = false;
    bool coins = false;
    bool epic = false;
    bool legendary = false;
    bool mythic = false;
    bool song = false;
    bool customSong = false;
    int songID = 0;
    int demonFilter = 0;
    bool noStar = false;
    bool star = false;

    // Two entries match when day, type, query and filters agree.
    bool operator==(const Entry& other) const;
    // Short text with the active filters (cell subtitle).
    std::string summary() const;
};

// Newest first. Lives in memory; persisted in load()/save().
extern std::vector<Entry> history;

// Pushes (or re-promotes) a search to the front.
void add(GJSearchObject* search, std::vector<int> difficulties, std::vector<int> lengths, int type);
void remove(int index);
void clear();
void load();
void save();

} // namespace paimon::searchhistory

template <>
struct matjson::Serialize<paimon::searchhistory::Entry> {
    static geode::Result<paimon::searchhistory::Entry> fromJson(const matjson::Value&);
    static matjson::Value toJson(const paimon::searchhistory::Entry&);
};
