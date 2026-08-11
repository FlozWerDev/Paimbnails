#include "TwitchLevelBriefCache.hpp"

#include "../../../core/RuntimeLifecycle.hpp"

#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GJSearchObject.hpp>
#include <Geode/binding/GameLevelManager.hpp>

#include <algorithm>
#include <chrono>

using namespace geode::prelude;

namespace paimon::twitch {

namespace {

constexpr int64_t kLookupTimeout = 12;

int64_t unixTime() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

// Same mapping GD uses for its own level cells (7-10 = demon tiers, -1 = auto).
int difficultyValue(GJGameLevel* level) {
    if (!level) return 0;
    if (level->m_autoLevel) return -1;
    if (level->m_demon) {
        switch (static_cast<int>(level->m_demonDifficulty)) {
            case 3: return 7;
            case 4: return 8;
            case 5: return 9;
            case 6: return 10;
            default: return 6;
        }
    }
    return level->getAverageDifficulty();
}

int requestedDifficulty(GJGameLevel* level) {
    if (!level) return 0;
    if (level->m_autoLevel) return -1;

    int const stars = level->m_starsRequested;
    if (stars <= 0) return 0;
    if (stars <= 2) return 1;
    if (stars == 3) return 2;
    if (stars <= 5) return 3;
    if (stars <= 7) return 4;
    if (stars <= 9) return 5;
    return 6;
}

} // namespace

TwitchLevelBriefCache& TwitchLevelBriefCache::get() {
    static TwitchLevelBriefCache instance;
    return instance;
}

LevelBrief const* TwitchLevelBriefCache::peek(int levelID) const {
    auto it = m_cache.find(levelID);
    return it == m_cache.end() ? nullptr : &it->second;
}

void TwitchLevelBriefCache::request(int levelID) {
    if (levelID <= 0) return;
    if (m_cache.contains(levelID) && m_levels.contains(levelID)) return;

    // Levels the player already downloaded are right there in the save file.
    if (auto* glm = GameLevelManager::get()) {
        if (auto* saved = glm->getSavedLevel(levelID)) {
            if (!saved->m_levelName.empty()) {
                store(saved);
                flushCallbacks(levelID);
                return;
            }
        }
    }

    if (m_inFlight == levelID) return;
    if (std::ranges::find(m_pending, levelID) != m_pending.end()) return;
    m_pending.push_back(levelID);
    pump();
}

GJGameLevel* TwitchLevelBriefCache::peekLevel(int levelID) const {
    auto it = m_levels.find(levelID);
    return it == m_levels.end() ? nullptr : it->second.data();
}

void TwitchLevelBriefCache::fetch(int levelID, std::function<void(GJGameLevel*)> callback) {
    if (!callback) return;
    if (levelID <= 0) {
        callback(nullptr);
        return;
    }
    if (auto* level = peekLevel(levelID)) {
        callback(level);
        return;
    }
    m_callbacks[levelID].push_back(std::move(callback));
    request(levelID);
}

void TwitchLevelBriefCache::flushCallbacks(int levelID) {
    auto it = m_callbacks.find(levelID);
    if (it == m_callbacks.end()) return;
    auto callbacks = std::move(it->second);
    m_callbacks.erase(it);

    auto* level = peekLevel(levelID);
    for (auto const& callback : callbacks) {
        if (callback) callback(level);
    }
}

void TwitchLevelBriefCache::tick() {
    if (m_inFlight != 0 && unixTime() - m_inFlightSince > kLookupTimeout) {
        finish(false);
        return;
    }
    pump();
}

void TwitchLevelBriefCache::pump() {
    if (m_inFlight != 0 || m_pending.empty()) return;
    if (paimon::isRuntimeShuttingDown()) return;

    auto* glm = GameLevelManager::get();
    if (!glm) return;
    if (glm->m_levelManagerDelegate && glm->m_levelManagerDelegate != this) return;

    int levelID = m_pending.front();
    auto* search = GJSearchObject::create(SearchType::Search, std::to_string(levelID));
    if (!search) {
        m_pending.pop_front();
        return;
    }

    m_inFlight = levelID;
    m_inFlightSince = unixTime();
    glm->m_levelManagerDelegate = this;
    glm->getOnlineLevels(search);
}

void TwitchLevelBriefCache::store(GJGameLevel* level) {
    if (!level) return;
    LevelBrief brief;
    brief.levelID = level->m_levelID;
    brief.name = std::string(level->m_levelName);
    brief.author = std::string(level->m_creatorName);
    brief.stars = level->m_stars.value();
    brief.difficulty = difficultyValue(level);
    brief.found = !brief.name.empty();
    brief.length = std::clamp(static_cast<int>(level->m_levelLength), 0, 4);
    brief.platformer = level->isPlatformer();
    brief.filterDifficulty = brief.stars > 0
        ? brief.difficulty
        : requestedDifficulty(level);
    int const levelID = brief.levelID;
    m_cache[levelID] = std::move(brief);
    m_levels[levelID] = level;
    ++m_revision;
}

void TwitchLevelBriefCache::finish(bool found) {
    int levelID = m_inFlight;
    m_inFlight = 0;
    m_inFlightSince = 0;

    if (auto* glm = GameLevelManager::get()) {
        if (glm->m_levelManagerDelegate == this) glm->m_levelManagerDelegate = nullptr;
    }

    if (levelID > 0) {
        std::erase(m_pending, levelID);
        if (!found && !m_cache.contains(levelID)) {
            // Remember the miss so the row stops asking for a deleted ID.
            m_cache[levelID] = LevelBrief{levelID, {}, {}, 0, 0, false};
            ++m_revision;
        }
        flushCallbacks(levelID);
    }

    // Out of the delegate callback: GameLevelManager may still be walking it.
    Loader::get()->queueInMainThread([] {
        if (paimon::isRuntimeShuttingDown()) return;
        TwitchLevelBriefCache::get().pump();
    });
}

void TwitchLevelBriefCache::loadLevelsFinished(CCArray* levels, char const*) {
    bool found = false;
    if (levels) {
        for (auto* level : CCArrayExt<GJGameLevel*>(levels)) {
            if (!level) continue;
            store(level);
            if (level->m_levelID == m_inFlight) found = true;
        }
    }
    finish(found);
}

void TwitchLevelBriefCache::loadLevelsFailed(char const*) {
    finish(false);
}

void TwitchLevelBriefCache::setupPageInfo(gd::string, char const*) {}

} // namespace paimon::twitch
