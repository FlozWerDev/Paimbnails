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
constexpr int64_t kMissTTL = 30;
constexpr size_t kMaxCachedLevels = 256;

int64_t nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

// same mapping gd uses for its own level cells (7-10 = demon tiers, -1 = auto).
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
    if (levelID <= 0 || m_stopped || paimon::isRuntimeShuttingDown()) return;
    if (m_cache.contains(levelID) && m_levels.contains(levelID)) return;
    if (auto miss = m_misses.find(levelID); miss != m_misses.end()) {
        if (nowSeconds() - miss->second < kMissTTL) {
            flushCallbacks(levelID);
            return;
        }
        m_misses.erase(miss);
        m_cache.erase(levelID);
        std::erase(m_cacheOrder, levelID);
    }

    // levels the player already downloaded are right there in the save file.
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
    if (!callback || m_stopped || paimon::isRuntimeShuttingDown()) return;
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

    geode::Ref<GJGameLevel> level = peekLevel(levelID);
    for (auto const& callback : callbacks) {
        if (callback) callback(level.data());
    }
}

void TwitchLevelBriefCache::tick() {
    if (m_stopped || paimon::isRuntimeShuttingDown()) return;
    auto now = nowSeconds();
    for (auto it = m_misses.begin(); it != m_misses.end();) {
        if (now - it->second < kMissTTL) {
            ++it;
            continue;
        }
        m_cache.erase(it->first);
        std::erase(m_cacheOrder, it->first);
        it = m_misses.erase(it);
        ++m_revision;
    }
    if (m_inFlight != 0 && now - m_inFlightSince > kLookupTimeout) {
        finish(false);
        return;
    }
    pump();
}

void TwitchLevelBriefCache::pump() {
    if (m_inFlight != 0 || m_pending.empty()) return;
    if (m_stopped || paimon::isRuntimeShuttingDown()) return;

    auto* glm = GameLevelManager::get();
    if (!glm) return;
    if (glm->m_levelManagerDelegate && glm->m_levelManagerDelegate != this) return;

    int levelID = m_pending.front();
    m_inFlight = levelID;
    m_inFlightSince = nowSeconds();
    auto* search = GJSearchObject::create(SearchType::Search, std::to_string(levelID));
    if (!search) {
        finish(false);
        return;
    }

    auto* key = search->getKey();
    m_inFlightKey = key ? key : "";
    if (m_inFlightKey.empty()) {
        finish(false);
        return;
    }
    glm->m_levelManagerDelegate = this;
    glm->getOnlineLevels(search);
}

void TwitchLevelBriefCache::store(GJGameLevel* level) {
    if (!level || level->m_levelID <= 0) return;
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
    m_misses.erase(levelID);
    touchCache(levelID);
    ++m_revision;
}

void TwitchLevelBriefCache::touchCache(int levelID) {
    std::erase(m_cacheOrder, levelID);
    m_cacheOrder.push_back(levelID);
    while (m_cacheOrder.size() > kMaxCachedLevels) {
        int oldest = m_cacheOrder.front();
        m_cacheOrder.pop_front();
        m_cache.erase(oldest);
        m_levels.erase(oldest);
        m_misses.erase(oldest);
    }
}

void TwitchLevelBriefCache::finish(bool found) {
    int levelID = m_inFlight;
    m_inFlight = 0;
    m_inFlightSince = 0;
    m_inFlightKey.clear();

    if (auto* glm = GameLevelManager::get()) {
        if (glm->m_levelManagerDelegate == this) glm->m_levelManagerDelegate = nullptr;
    }

    if (levelID > 0) {
        std::erase(m_pending, levelID);
        if (!found && !m_cache.contains(levelID)) {
            // transient failures must expire so the row can retry.
            m_cache[levelID] = LevelBrief{levelID, {}, {}, 0, 0, false};
            m_misses[levelID] = nowSeconds();
            touchCache(levelID);
            ++m_revision;
        }
        flushCallbacks(levelID);
    }

    // out of the delegate callback: gamelevelmanager may still be walking it.
    Loader::get()->queueInMainThread([] {
        if (paimon::isRuntimeShuttingDown()) return;
        TwitchLevelBriefCache::get().pump();
    });
}

bool TwitchLevelBriefCache::isCurrentKey(char const* key) const {
    return !m_stopped && m_inFlight != 0 && key && m_inFlightKey == key;
}

void TwitchLevelBriefCache::shutdown() {
    m_stopped = true;
    if (auto* glm = GameLevelManager::get()) {
        if (glm->m_levelManagerDelegate == this) glm->m_levelManagerDelegate = nullptr;
    }
    m_inFlight = 0;
    m_inFlightSince = 0;
    m_inFlightKey.clear();
    m_callbacks.clear();
    m_pending.clear();
    m_cacheOrder.clear();
    m_cache.clear();
    m_levels.clear();
    m_misses.clear();
}

void TwitchLevelBriefCache::loadLevelsFinished(CCArray* levels, char const* key) {
    if (paimon::isRuntimeShuttingDown() || !isCurrentKey(key)) return;
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

void TwitchLevelBriefCache::loadLevelsFailed(char const* key) {
    if (paimon::isRuntimeShuttingDown() || !isCurrentKey(key)) return;
    finish(false);
}

void TwitchLevelBriefCache::setupPageInfo(gd::string, char const*) {}

} // namespace paimon::twitch
