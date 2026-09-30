#pragma once

// search builder plus a client-side "refine" pass for filters robtop's api lacks.
// a refined page can return fewer than ten levels; the popup says so.

#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GJSearchObject.hpp>
#include <optional>
#include <string>
#include <vector>

namespace paimon::info {

struct AdvancedQuery {
    std::string query;

    std::vector<int> difficulties;  // 1..6, empty = any
    int demonFilter = 0;            // gjdifficulty demon sub type, 0 = any
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

    // client side refine (0 = unset)
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

// the refine pass applies only to a browser with the matching search key.
void armRefine(std::string key, AdvancedQuery query);
AdvancedQuery const* refineFor(std::string const& key);
void clearRefine();

// runs the search: builds the object, arms the refine and pushes the browser.
void runSearch(AdvancedQuery const& query);

struct SearchPreset {
    std::string name;
    AdvancedQuery query;
};

std::vector<SearchPreset> const& presets();
void addPreset(std::string name, AdvancedQuery query);
void removePreset(int index);

// human readable one liner for a preset row.
std::string describeQuery(AdvancedQuery const& query);

} // namespace paimon::info
