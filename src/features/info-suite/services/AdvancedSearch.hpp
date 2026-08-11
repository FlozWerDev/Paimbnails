#pragma once

// Advanced Search: a complete search builder plus the client side "refine"
// pass.
//
// The server filters (difficulty, length, rating flags, song, folder) go into a
// GJSearchObject like any normal search. The filters RobTop's API does not
// support — id range, game version range, object count range — are applied to
// each page as it arrives, through LevelBrowserLayer::updateResultArray. That
// means a refined page can come back with fewer than ten levels; the popup says
// so rather than pretending otherwise.

#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GJSearchObject.hpp>
#include <optional>
#include <string>
#include <vector>

namespace paimon::info {

struct AdvancedQuery {
    std::string query;

    std::vector<int> difficulties;  // 1..6, empty = any
    int demonFilter = 0;            // GJDifficulty demon sub type, 0 = any
    std::vector<int> lengths;       // 0..4, empty = any
    bool platformer = false;
    bool star = false;
    bool noStar = false;
    bool featured = false;
    bool epic = false;
    bool legendary = false;
    bool mythic = false;
    bool original = false;
    bool twoPlayer = false;
    bool coins = false;
    bool completed = false;
    bool uncompleted = false;
    int songID = 0;
    bool songFilter = false;

    // Client side refine (0 = unset)
    int minID = 0;
    int maxID = 0;
    int minGameVersion = 0;
    int maxGameVersion = 0;
    int minObjects = 0;
    int maxObjects = 0;
};

bool hasRefine(AdvancedQuery const& query);
bool passesRefine(AdvancedQuery const& query, GJGameLevel* level);

GJSearchObject* buildSearch(AdvancedQuery const& query);

// Arms the refine pass for the next browser opened with `key`. The browser hook
// only applies it when the search it is showing matches, so the refine cannot
// leak into an unrelated search later on.
void armRefine(std::string key, AdvancedQuery query);
AdvancedQuery const* refineFor(std::string const& key);
void clearRefine();

// Runs the search: builds the object, arms the refine and pushes the browser.
void runSearch(AdvancedQuery const& query);

struct SearchPreset {
    std::string name;
    AdvancedQuery query;
};

std::vector<SearchPreset> const& presets();
void addPreset(std::string name, AdvancedQuery query);
void removePreset(int index);

// Human readable one liner for a preset row.
std::string describeQuery(AdvancedQuery const& query);

} // namespace paimon::info
