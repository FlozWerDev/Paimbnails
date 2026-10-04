#pragma once

#include <Geode/Geode.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <algorithm>
#include <cctype>
#include <string>

namespace paimon::editorfilters {

enum class TriState { Any = 0, Yes = 1, No = 2 };

enum class SortMode {
    Default = 0,
    NameAsc,
    NameDesc,
    Newest,
    Oldest,
    MostObjects,
    LeastObjects,
    Longest,
    Shortest,
};

struct FilterState {
    // length buckets (gjgamelevel::m_levelLength: 0..4).
    bool tiny = false;
    bool shortLen = false;
    bool medium = false;
    bool longLen = false;
    bool xl = false;

    bool verified = false;
    bool unverified = false;
    std::string songID;

    std::string nameQuery;
    int minObjects = 0;
    int maxObjects = 0;

    TriState customSong = TriState::Any;
    TriState uploaded = TriState::Any;
    TriState twoPlayer = TriState::Any;
    TriState coins = TriState::Any;
    TriState lowDetail = TriState::Any;

    SortMode sort = SortMode::Default;
};

inline FilterState& state() {
    static FilterState s;
    return s;
}

inline bool anyLengthFilter() {
    auto& f = state();
    return f.tiny || f.shortLen || f.medium || f.longLen || f.xl;
}

inline bool anyVerificationFilter() {
    auto& f = state();
    return f.verified || f.unverified;
}

inline bool anyActive() {
    auto& f = state();
    return anyLengthFilter() || anyVerificationFilter() || !f.songID.empty()
        || !f.nameQuery.empty() || f.minObjects > 0 || f.maxObjects > 0
        || f.customSong != TriState::Any || f.uploaded != TriState::Any
        || f.twoPlayer != TriState::Any || f.coins != TriState::Any
        || f.lowDetail != TriState::Any;
}

inline bool anySort() {
    return state().sort != SortMode::Default;
}

inline void reset() {
    state() = FilterState{};
}

namespace detail {
    inline std::string toLower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c) { return std::tolower(c); });
        return s;
    }

    inline bool triOk(TriState want, bool value) {
        if (want == TriState::Any) return true;
        return (want == TriState::Yes) == value;
    }
}

inline bool matches(GJGameLevel* level) {
    if (!level) return false;
    auto& f = state();

    if (anyLengthFilter()) {
        int len = level->m_levelLength;
        bool lengthOk =
            (len == 0 && f.tiny)     ||
            (len == 1 && f.shortLen) ||
            (len == 2 && f.medium)   ||
            (len == 3 && f.longLen)  ||
            (len == 4 && f.xl);
        if (!lengthOk) return false;
    }

    if (anyVerificationFilter()) {
        bool isVerified = level->m_isVerified;
        if (!((isVerified && f.verified) || (!isVerified && f.unverified))) return false;
    }

    if (!f.songID.empty() && std::to_string(level->m_songID) != f.songID) return false;

    if (!f.nameQuery.empty()) {
        std::string name = detail::toLower(level->m_levelName);
        if (name.find(detail::toLower(f.nameQuery)) == std::string::npos) return false;
    }

    if (f.minObjects > 0 || f.maxObjects > 0) {
        int objs = level->m_objectCount.value();
        if (f.minObjects > 0 && objs < f.minObjects) return false;
        if (f.maxObjects > 0 && objs > f.maxObjects) return false;
    }

    // audioTrack is the official track slot; a non-zero songID means a Newgrounds/custom song.
    if (!detail::triOk(f.customSong, level->m_songID != 0)) return false;
    if (!detail::triOk(f.uploaded, level->m_levelID.value() > 0)) return false;
    if (!detail::triOk(f.twoPlayer, level->m_twoPlayerMode)) return false;
    if (!detail::triOk(f.coins, level->m_coins > 0)) return false;
    if (!detail::triOk(f.lowDetail, level->m_lowDetailMode)) return false;

    return true;
}

inline bool lessFor(SortMode mode, GJGameLevel* a, GJGameLevel* b) {
    switch (mode) {
        case SortMode::NameAsc:
            return detail::toLower(a->m_levelName) < detail::toLower(b->m_levelName);
        case SortMode::NameDesc:
            return detail::toLower(a->m_levelName) > detail::toLower(b->m_levelName);
        case SortMode::Newest:
            return a->m_timestamp > b->m_timestamp;
        case SortMode::Oldest:
            return a->m_timestamp < b->m_timestamp;
        case SortMode::MostObjects:
            return a->m_objectCount.value() > b->m_objectCount.value();
        case SortMode::LeastObjects:
            return a->m_objectCount.value() < b->m_objectCount.value();
        case SortMode::Longest:
            return a->m_levelLength > b->m_levelLength;
        case SortMode::Shortest:
            return a->m_levelLength < b->m_levelLength;
        default:
            return false;
    }
}

inline char const* sortLabel(SortMode mode) {
    switch (mode) {
        case SortMode::NameAsc:      return "Name A-Z";
        case SortMode::NameDesc:     return "Name Z-A";
        case SortMode::Newest:       return "Newest";
        case SortMode::Oldest:       return "Oldest";
        case SortMode::MostObjects:  return "Most objects";
        case SortMode::LeastObjects: return "Least objects";
        case SortMode::Longest:      return "Longest";
        case SortMode::Shortest:     return "Shortest";
        default:                     return "Default order";
    }
}

inline SortMode nextSort(SortMode mode, int dir) {
    int count = static_cast<int>(SortMode::Shortest) + 1;
    int v = (static_cast<int>(mode) + dir % count + count) % count;
    return static_cast<SortMode>(v);
}

inline void load() {
    auto* mod = geode::Mod::get();
    auto& f = state();
    f.tiny       = mod->getSavedValue<bool>("mylevels-filter-tiny", false);
    f.shortLen   = mod->getSavedValue<bool>("mylevels-filter-short", false);
    f.medium     = mod->getSavedValue<bool>("mylevels-filter-medium", false);
    f.longLen    = mod->getSavedValue<bool>("mylevels-filter-long", false);
    f.xl         = mod->getSavedValue<bool>("mylevels-filter-xl", false);
    f.verified   = mod->getSavedValue<bool>("mylevels-filter-verified", false);
    f.unverified = mod->getSavedValue<bool>("mylevels-filter-unverified", false);
    f.songID     = mod->getSavedValue<std::string>("mylevels-filter-songid", "");
    f.nameQuery  = mod->getSavedValue<std::string>("mylevels-filter-name", "");
    f.minObjects = mod->getSavedValue<int>("mylevels-filter-minobj", 0);
    f.maxObjects = mod->getSavedValue<int>("mylevels-filter-maxobj", 0);
    f.customSong = static_cast<TriState>(mod->getSavedValue<int>("mylevels-filter-customsong", 0));
    f.uploaded   = static_cast<TriState>(mod->getSavedValue<int>("mylevels-filter-uploaded", 0));
    f.twoPlayer  = static_cast<TriState>(mod->getSavedValue<int>("mylevels-filter-twoplayer", 0));
    f.coins      = static_cast<TriState>(mod->getSavedValue<int>("mylevels-filter-coins", 0));
    f.lowDetail  = static_cast<TriState>(mod->getSavedValue<int>("mylevels-filter-ldm", 0));
    f.sort       = static_cast<SortMode>(mod->getSavedValue<int>("mylevels-filter-sort", 0));
}

inline void save() {
    auto* mod = geode::Mod::get();
    auto& f = state();
    mod->setSavedValue<bool>("mylevels-filter-tiny", f.tiny);
    mod->setSavedValue<bool>("mylevels-filter-short", f.shortLen);
    mod->setSavedValue<bool>("mylevels-filter-medium", f.medium);
    mod->setSavedValue<bool>("mylevels-filter-long", f.longLen);
    mod->setSavedValue<bool>("mylevels-filter-xl", f.xl);
    mod->setSavedValue<bool>("mylevels-filter-verified", f.verified);
    mod->setSavedValue<bool>("mylevels-filter-unverified", f.unverified);
    mod->setSavedValue<std::string>("mylevels-filter-songid", f.songID);
    mod->setSavedValue<std::string>("mylevels-filter-name", f.nameQuery);
    mod->setSavedValue<int>("mylevels-filter-minobj", f.minObjects);
    mod->setSavedValue<int>("mylevels-filter-maxobj", f.maxObjects);
    mod->setSavedValue<int>("mylevels-filter-customsong", static_cast<int>(f.customSong));
    mod->setSavedValue<int>("mylevels-filter-uploaded", static_cast<int>(f.uploaded));
    mod->setSavedValue<int>("mylevels-filter-twoplayer", static_cast<int>(f.twoPlayer));
    mod->setSavedValue<int>("mylevels-filter-coins", static_cast<int>(f.coins));
    mod->setSavedValue<int>("mylevels-filter-ldm", static_cast<int>(f.lowDetail));
    mod->setSavedValue<int>("mylevels-filter-sort", static_cast<int>(f.sort));
}

} // namespace paimon::editorfilters
