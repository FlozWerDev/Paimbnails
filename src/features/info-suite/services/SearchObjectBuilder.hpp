#pragma once

// gjsearchobject::create's 22-argument overload can corrupt its gd::string fields and crash in getkey().
// everything builds through the two-argument overload and assigns fields afterwards.

#include <Geode/binding/GJSearchObject.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/LevelBrowserLayer.hpp>
#include <Geode/Geode.hpp>

namespace paimon::info {

struct SearchFilters {
    gd::string query;
    // "-1" is what the game itself sends for "any"; see recommendationengine.
    gd::string difficulty = "-1";
    gd::string length = "-1";
    int page = 0;
    bool star = false;
    bool uncompleted = false;
    bool featured = false;
    int songID = 0;
    bool original = false;
    bool twoPlayer = false;
    bool customSong = false;
    bool songFilter = false;
    bool noStar = false;
    bool coins = false;
    bool epic = false;
    bool legendary = false;
    bool mythic = false;
    bool onlyCompleted = false;
    int demonFilter = 0;
    int folder = 0;
    int searchMode = 0;
};

inline GJSearchObject* buildSearchObject(SearchType type, SearchFilters const& f) {
    auto* obj = GJSearchObject::create(type, f.query);
    if (!obj) return nullptr;

    obj->m_difficulty = f.difficulty;
    obj->m_length = f.length;
    obj->m_page = f.page;
    obj->m_starFilter = f.star;
    obj->m_uncompletedFilter = f.uncompleted;
    obj->m_featuredFilter = f.featured;
    obj->m_songID = f.songID;
    obj->m_originalFilter = f.original;
    obj->m_twoPlayerFilter = f.twoPlayer;
    obj->m_customSongFilter = f.customSong;
    obj->m_songFilter = f.songFilter;
    obj->m_noStarFilter = f.noStar;
    obj->m_coinsFilter = f.coins;
    obj->m_epicFilter = f.epic;
    obj->m_legendaryFilter = f.legendary;
    obj->m_mythicFilter = f.mythic;
    obj->m_completedFilter = f.onlyCompleted;
    obj->m_demonFilter = static_cast<GJDifficulty>(f.demonFilter);
    obj->m_folder = f.folder;
    obj->m_searchMode = f.searchMode;
    return obj;
}

// the game cache key includes the page; cross-page search identity needs an independent digest.
inline std::string searchKey(GJSearchObject* obj) {
    if (!obj) return {};
    return fmt::format(
        "{}|{}|{}|{}|{}|{}|{}{}{}{}{}{}{}{}{}{}{}",
        static_cast<int>(obj->m_searchType),
        std::string(obj->m_searchQuery),
        std::string(obj->m_difficulty),
        std::string(obj->m_length),
        obj->m_songID,
        obj->m_folder,
        static_cast<int>(obj->m_demonFilter),
        obj->m_starFilter ? "s" : "",
        obj->m_noStarFilter ? "n" : "",
        obj->m_uncompletedFilter ? "u" : "",
        obj->m_completedFilter ? "c" : "",
        obj->m_featuredFilter ? "f" : "",
        obj->m_originalFilter ? "o" : "",
        obj->m_twoPlayerFilter ? "2" : "",
        obj->m_coinsFilter ? "$" : "",
        obj->m_epicFilter ? "e" : "",
        obj->m_songFilter ? "m" : "");
}

// pushes the level browser for a search object, with the usual gd fade.
inline void pushBrowser(GJSearchObject* obj) {
    if (!obj) return;
    auto* scene = LevelBrowserLayer::scene(obj);
    if (!scene) return;
    cocos2d::CCDirector::get()->pushScene(
        cocos2d::CCTransitionFade::create(0.5f, scene));
}

// "open level #id" — the same thing typing an id into the search box does.
inline void openLevelByID(int levelID) {
    if (levelID <= 0) return;
    SearchFilters filters;
    filters.query = std::to_string(levelID);
    pushBrowser(buildSearchObject(SearchType::Search, filters));
}

// "levels that use this song".
inline void openLevelsWithSong(int songID) {
    if (songID <= 0) return;
    SearchFilters filters;
    filters.songID = songID;
    filters.songFilter = true;
    filters.customSong = true;
    pushBrowser(buildSearchObject(SearchType::Search, filters));
}

} // namespace paimon::info
