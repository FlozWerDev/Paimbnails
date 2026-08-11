#include "TasteProfile.hpp"

#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../utils/JsonHelper.hpp"

#include <Geode/binding/GJGameLevel.hpp>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <fstream>

using namespace geode::prelude;

namespace paimon::foryou {

namespace {

// Support shrinkage keeps rare tags from dominating.
constexpr float kTagShrinkage = 2.5f;
// A pinned tag is worth roughly two positive interactions.
constexpr float kPinnedTagWeight = 4.f;
// Smaller magnitudes count as noise.
constexpr float kSignalThreshold = 0.75f;
// Interaction half-life in days.
constexpr float kHalfLifeDays = 120.f;

constexpr int kMaxRankedTags = 8;
constexpr int kMaxRankedCreators = 6;
constexpr int kMaxRankedSongs = 5;

bool trackingEnabled() {
    return paimon::modules::isEnabled("paimbnails.foryou.browser");
}

int epicTierOf(GJGameLevel* level) {
    int epic = level->m_isEpic;
    return std::clamp(epic, 0, 3);
}

template <typename K>
std::vector<K> rankTop(std::unordered_map<K, float> const& scores, int limit, bool positive) {
    std::vector<std::pair<K, float>> sorted(scores.begin(), scores.end());
    std::erase_if(sorted, [positive](auto const& entry) {
        return positive ? entry.second <= 0.f : entry.second >= 0.f;
    });
    std::sort(sorted.begin(), sorted.end(), [positive](auto const& a, auto const& b) {
        return positive ? a.second > b.second : a.second < b.second;
    });
    if (static_cast<int>(sorted.size()) > limit) sorted.resize(limit);

    std::vector<K> out;
    out.reserve(sorted.size());
    for (auto const& [key, score] : sorted) out.push_back(key);
    return out;
}

void normalize(float* values, size_t count) {
    float total = 0.f;
    for (size_t i = 0; i < count; i++) total += values[i];
    if (total <= 0.f) return;
    for (size_t i = 0; i < count; i++) values[i] /= total;
}

}

TasteProfile& TasteProfile::get() {
    static TasteProfile instance;
    return instance;
}

TasteProfile::TasteProfile() {
    load();
}

std::filesystem::path TasteProfile::profilePath() const {
    return Mod::get()->getSaveDir() / "foryou_profile.json";
}


void TasteProfile::onLevelEnter(GJGameLevel* level) {
    if (!trackingEnabled()) return;
    if (!level || level->m_levelID <= 0) return;

    std::lock_guard lock(m_mutex);

    int id = level->m_levelID;
    m_activeSessionLevelID = id;
    m_sessionStart = std::chrono::steady_clock::now();

    auto& rec = m_levels[id];
    rec.playCount++;
    rec.difficulty = static_cast<int>(level->m_difficulty);
    rec.demonDifficulty = level->m_demonDifficulty;
    rec.stars = level->m_stars;
    rec.length = level->m_levelLength;
    rec.platformer = level->isPlatformer();
    rec.featured = level->m_featured > 0;
    rec.epicTier = epicTierOf(level);
    rec.songID = level->m_songID;
    rec.creatorID = level->m_accountID;
    rec.attempts = level->m_attempts;
    rec.bestPercent = std::max<int>(rec.bestPercent, level->m_normalPercent);
    rec.lastSeen = static_cast<int64_t>(std::time(nullptr));

    // Mirror GD's favourite flag as a positive signal.
    if (level->m_levelFavorited) {
        rec.favoriteLevel = true;
        m_favoriteLevels.insert(id);
    }

    m_dirty = true;
    m_snapshotStale = true;
}

void TasteProfile::onLevelExit(GJGameLevel* level) {
    if (!trackingEnabled()) return;
    if (!level || level->m_levelID <= 0) return;

    std::lock_guard lock(m_mutex);

    int id = level->m_levelID;
    if (m_activeSessionLevelID != id) return;

    auto elapsed = std::chrono::steady_clock::now() - m_sessionStart;

    auto& rec = m_levels[id];
    rec.playSeconds += std::chrono::duration<float>(elapsed).count();
    rec.attempts = std::max<int>(rec.attempts, level->m_attempts);
    rec.bestPercent = std::max<int>(rec.bestPercent, level->m_normalPercent);
    if (level->m_levelFavorited) {
        rec.favoriteLevel = true;
        m_favoriteLevels.insert(id);
    }

    m_activeSessionLevelID = 0;
    m_dirty = true;
    m_snapshotStale = true;
}

void TasteProfile::onLevelComplete(GJGameLevel* level) {
    if (!trackingEnabled()) return;
    if (!level || level->m_levelID <= 0) return;

    std::lock_guard lock(m_mutex);

    auto& rec = m_levels[level->m_levelID];
    rec.completed = true;
    rec.bestPercent = 100;
    rec.attempts = std::max<int>(rec.attempts, level->m_attempts);
    rec.lastSeen = static_cast<int64_t>(std::time(nullptr));
    m_dirty = true;
    m_snapshotStale = true;
}

void TasteProfile::onLevelVote(int levelID, bool liked) {
    if (!trackingEnabled()) return;
    if (levelID <= 0) return;

    std::lock_guard lock(m_mutex);

    auto& rec = m_levels[levelID];
    rec.vote = liked ? 1 : -1;
    rec.lastSeen = static_cast<int64_t>(std::time(nullptr));
    // A like after exclusion is a change of mind.
    if (liked) rec.dismissed = false;
    m_dirty = true;
    m_snapshotStale = true;
}

void TasteProfile::onThumbnailRated(int levelID, int stars) {
    if (!trackingEnabled()) return;
    if (levelID <= 0 || stars < 1 || stars > 5) return;

    std::lock_guard lock(m_mutex);
    m_levels[levelID].thumbnailRating = stars;
    m_dirty = true;
    m_snapshotStale = true;
}

void TasteProfile::onTagsResolved(int levelID, std::vector<std::string> const& tags) {
    if (levelID <= 0) return;

    std::lock_guard lock(m_mutex);
    auto it = m_levels.find(levelID);
    // Only touched levels enter the model.
    if (it == m_levels.end()) return;
    if (it->second.tags == tags) return;

    it->second.tags = tags;
    m_dirty = true;
    m_snapshotStale = true;
}

void TasteProfile::onLevelDismissed(int levelID) {
    if (levelID <= 0) return;

    std::lock_guard lock(m_mutex);
    auto& rec = m_levels[levelID];
    rec.dismissed = true;
    rec.lastSeen = static_cast<int64_t>(std::time(nullptr));
    m_dirty = true;
    m_snapshotStale = true;
}

void TasteProfile::onFavoriteCreator(int creatorID) {
    if (creatorID <= 0) return;
    std::lock_guard lock(m_mutex);
    m_favoriteCreators.insert(creatorID);
    m_dirty = true;
    m_snapshotStale = true;
}

void TasteProfile::onUnfavoriteCreator(int creatorID) {
    if (creatorID <= 0) return;
    std::lock_guard lock(m_mutex);
    m_favoriteCreators.erase(creatorID);
    m_dirty = true;
    m_snapshotStale = true;
}

void TasteProfile::onFavoriteLevel(int levelID) {
    if (levelID <= 0) return;
    std::lock_guard lock(m_mutex);
    m_favoriteLevels.insert(levelID);
    m_levels[levelID].favoriteLevel = true;
    m_dirty = true;
    m_snapshotStale = true;
}

void TasteProfile::onUnfavoriteLevel(int levelID) {
    if (levelID <= 0) return;
    std::lock_guard lock(m_mutex);
    m_favoriteLevels.erase(levelID);
    auto it = m_levels.find(levelID);
    if (it != m_levels.end()) it->second.favoriteLevel = false;
    m_dirty = true;
    m_snapshotStale = true;
}

bool TasteProfile::isCreatorFavorited(int creatorID) const {
    std::lock_guard lock(m_mutex);
    return m_favoriteCreators.count(creatorID) > 0;
}

bool TasteProfile::isLevelFavorited(int levelID) const {
    std::lock_guard lock(m_mutex);
    return m_favoriteLevels.count(levelID) > 0;
}


void TasteProfile::setPinnedTag(std::string const& tag, int vote) {
    if (tag.empty()) return;
    std::lock_guard lock(m_mutex);
    if (vote == 0) {
        m_pinnedTags.erase(tag);
    } else {
        m_pinnedTags[tag] = vote > 0 ? 1 : -1;
    }
    m_dirty = true;
    m_snapshotStale = true;
}

int TasteProfile::pinnedTagVote(std::string const& tag) const {
    std::lock_guard lock(m_mutex);
    auto it = m_pinnedTags.find(tag);
    return it != m_pinnedTags.end() ? it->second : 0;
}

std::vector<std::string> TasteProfile::pinnedTags(int vote) const {
    std::lock_guard lock(m_mutex);
    std::vector<std::string> out;
    for (auto const& [tag, v] : m_pinnedTags) {
        if (vote == 0 || v == vote) out.push_back(tag);
    }
    std::sort(out.begin(), out.end());
    return out;
}


void TasteProfile::applySeedLocked(TasteSnapshot& snapshot) const {
    snapshot.preferredDifficulty = m_seedDifficulty;
    snapshot.platformerRatio = m_seedPlatformerRatio;
    snapshot.preferredDemonDifficulty = m_seedDemonDifficulty;

    snapshot.difficultyHistogram.fill(0.f);
    int diffIdx = std::clamp(m_seedDifficulty / 10, 0, static_cast<int>(kDifficultyBuckets) - 1);
    // Keep neighboring difficulties plausible instead of collapsing to one band.
    snapshot.difficultyHistogram[diffIdx] = 0.6f;
    if (diffIdx > 0) snapshot.difficultyHistogram[diffIdx - 1] = 0.2f;
    if (diffIdx + 1 < static_cast<int>(kDifficultyBuckets)) snapshot.difficultyHistogram[diffIdx + 1] = 0.2f;

    snapshot.lengthHistogram.fill(0.f);
    if (m_seedLength >= 0 && m_seedLength <= 4) {
        snapshot.lengthHistogram[m_seedLength] = 0.5f;
        if (m_seedLength > 0) snapshot.lengthHistogram[m_seedLength - 1] = 0.3f;
        if (m_seedLength < 4) snapshot.lengthHistogram[m_seedLength + 1] = 0.2f;
        snapshot.preferredLength = m_seedLength;
    } else {
        snapshot.lengthHistogram = std::array<float, kLengthBuckets>{0.15f, 0.20f, 0.25f, 0.25f, 0.15f, 0.f};
        snapshot.preferredLength = 5;
    }
    if (m_seedPlatformerRatio > 0.5f) snapshot.lengthHistogram[5] = 0.5f;

    snapshot.demonHistogram.fill(0.f);
    if (m_seedDemonDifficulty >= 1 && m_seedDemonDifficulty <= 5) {
        snapshot.demonHistogram[m_seedDemonDifficulty] = 1.f;
    }

    snapshot.starRatedRatio = m_seedStarRated ? 1.f : 0.f;
    snapshot.featuredRatio = m_seedFeatured ? 0.8f : 0.1f;
    snapshot.epicRatio = m_seedEpic ? 0.6f : 0.f;
}

void TasteProfile::seedPreferences(int difficulty, float platformerRatio, int length,
                                   bool starRated, bool featured, bool epic, int demonDifficulty) {
    std::lock_guard lock(m_mutex);

    m_seeded = true;
    m_seedDifficulty = difficulty;
    m_seedPlatformerRatio = platformerRatio;
    m_seedLength = length;
    m_seedStarRated = starRated;
    m_seedFeatured = featured;
    m_seedEpic = epic;
    m_seedDemonDifficulty = demonDifficulty;

    m_dirty = true;
    m_snapshotStale = true;

    log::info("[ForYou] Seeded priors: diff={} platformer={:.2f} len={} star={} feat={} epic={} demon={}",
              difficulty, platformerRatio, length, starRated, featured, epic, demonDifficulty);
}

bool TasteProfile::isSeeded() const {
    std::lock_guard lock(m_mutex);
    return m_seeded;
}


float TasteProfile::interactionWeightLocked(LevelInteraction const& rec) const {
    float w = 0.f;

    // Explicit signals dominate.
    if (rec.vote > 0)      w += 6.f;
    else if (rec.vote < 0) w -= 8.f;
    if (rec.favoriteLevel) w += 7.f;
    if (rec.dismissed)     w -= 5.f;
    if (rec.thumbnailRating > 0) w += static_cast<float>(rec.thumbnailRating - 3) * 1.2f;

    // Implicit signals capture progress and time spent.
    if (rec.completed)             w += 4.f;
    else if (rec.bestPercent >= 70) w += 2.f;
    else if (rec.bestPercent >= 40) w += 0.8f;

    if (rec.playSeconds > 0.f) {
        w += std::min(3.f, std::log2(1.f + rec.playSeconds / 30.f));
    }
    if (rec.attempts > 1) {
        w += std::min(2.f, std::log2(static_cast<float>(rec.attempts)) * 0.5f);
    }

    // An immediate quit is a mild negative signal.
    bool bounced = !rec.completed && rec.playCount <= 1 &&
                   rec.playSeconds < 8.f && rec.bestPercent < 15;
    if (bounced) w -= 1.5f;

    if (rec.creatorID > 0 && m_favoriteCreators.count(rec.creatorID)) w += 3.f;

    // Age magnitude, not sign; old tastes fade instead of inverting.
    if (rec.lastSeen > 0) {
        float ageDays = static_cast<float>(std::time(nullptr) - rec.lastSeen) / 86400.f;
        if (ageDays > 0.f) w *= std::pow(0.5f, ageDays / kHalfLifeDays);
    }

    return w;
}

void TasteProfile::ensureSnapshotLocked() const {
    if (!m_snapshotStale) return;
    rebuildLocked();
}

void TasteProfile::rebuildLocked() const {
    TasteSnapshot snap;
    snap.favoriteCreators = m_favoriteCreators;
    snap.favoriteLevels = m_favoriteLevels;
    snap.pinnedTags = m_pinnedTags;

    std::unordered_map<std::string, float> tagSum;
    std::unordered_map<std::string, float> tagSupport;
    std::unordered_map<int, float> creatorSum;
    std::unordered_map<int, float> creatorSupport;
    std::unordered_map<int, float> songSum;
    std::unordered_map<int, float> songSupport;

    float qualityWeight = 0.f;
    float ratedWeight = 0.f;
    float featuredWeight = 0.f;
    float epicWeight = 0.f;
    float platformerWeight = 0.f;

    for (auto const& [id, rec] : m_levels) {
        float w = interactionWeightLocked(rec);

        if (rec.vote > 0) snap.likeCount++;
        else if (rec.vote < 0) snap.dislikeCount++;

        if (std::fabs(w) >= kSignalThreshold) {
            snap.signalCount++;
            if (!rec.tags.empty()) snap.taggedSignalCount++;
        }

        for (auto const& tag : rec.tags) {
            tagSum[tag] += w;
            tagSupport[tag] += 1.f;
        }
        if (rec.creatorID > 0) {
            creatorSum[rec.creatorID] += w;
            creatorSupport[rec.creatorID] += 1.f;
        }
        if (rec.songID > 0) {
            songSum[rec.songID] += w;
            songSupport[rec.songID] += 1.f;
        }

        // Only positive interactions shape the preference histograms.
        if (w <= 0.f) continue;

        int diffIdx = std::clamp(rec.difficulty / 10, 0, static_cast<int>(kDifficultyBuckets) - 1);
        snap.difficultyHistogram[diffIdx] += w;

        int lenIdx = rec.platformer ? 5 : std::clamp(rec.length, 0, 4);
        snap.lengthHistogram[lenIdx] += w;

        if (rec.difficulty == 60 && rec.demonDifficulty >= 1 && rec.demonDifficulty <= 5) {
            snap.demonHistogram[rec.demonDifficulty] += w;
        }

        qualityWeight += w;
        if (rec.stars > 0)    ratedWeight += w;
        if (rec.featured)     featuredWeight += w;
        if (rec.epicTier > 0) epicWeight += w;
        if (rec.platformer)   platformerWeight += w;
    }

    if (qualityWeight > 0.f) {
        snap.starRatedRatio  = ratedWeight / qualityWeight;
        snap.featuredRatio   = featuredWeight / qualityWeight;
        snap.epicRatio       = epicWeight / qualityWeight;
        snap.platformerRatio = platformerWeight / qualityWeight;

        auto modeOf = [](auto const& histogram) {
            size_t best = 0;
            for (size_t i = 1; i < histogram.size(); i++) {
                if (histogram[i] > histogram[best]) best = i;
            }
            return histogram[best] > 0.f ? static_cast<int>(best) : -1;
        };

        int diffMode = modeOf(snap.difficultyHistogram);
        if (diffMode >= 0) snap.preferredDifficulty = diffMode * 10;

        int lenMode = modeOf(snap.lengthHistogram);
        snap.preferredLength = lenMode >= 0 ? lenMode : 5;

        int demonMode = modeOf(snap.demonHistogram);
        snap.preferredDemonDifficulty = demonMode > 0 ? demonMode : 0;
    }

    // Use onboarding shape priors until the profile has enough signal.
    if (m_seeded && snap.signalCount < 3) {
        applySeedLocked(snap);
    }

    normalize(snap.difficultyHistogram.data(), snap.difficultyHistogram.size());
    normalize(snap.lengthHistogram.data(), snap.lengthHistogram.size());
    normalize(snap.demonHistogram.data(), snap.demonHistogram.size());

    // Shrink means so rare tags cannot outrank consistent preferences.
    for (auto const& [tag, sum] : tagSum) {
        snap.tagAffinity[tag] = sum / (tagSupport[tag] + kTagShrinkage);
    }
    // Pinned tags outrank inferred values without erasing them.
    for (auto const& [tag, vote] : m_pinnedTags) {
        float pinned = static_cast<float>(vote) * kPinnedTagWeight;
        auto it = snap.tagAffinity.find(tag);
        snap.tagAffinity[tag] = it != snap.tagAffinity.end() ? it->second * 0.5f + pinned : pinned;
    }

    // Normalize to [-1, 1] so weights are history-independent.
    float maxAbs = 0.f;
    for (auto const& [tag, affinity] : snap.tagAffinity) {
        maxAbs = std::max(maxAbs, std::fabs(affinity));
    }
    if (maxAbs > 0.f) {
        for (auto& [tag, affinity] : snap.tagAffinity) affinity /= maxAbs;
    }

    snap.topTags = rankTop(snap.tagAffinity, kMaxRankedTags, true);
    snap.avoidedTags = rankTop(snap.tagAffinity, kMaxRankedTags, false);

    for (auto const& [creatorID, sum] : creatorSum) {
        snap.creatorAffinity[creatorID] = sum / (creatorSupport[creatorID] + 1.f);
    }
    for (int creatorID : m_favoriteCreators) {
        snap.creatorAffinity[creatorID] = std::max(snap.creatorAffinity[creatorID], 0.f) + 5.f;
    }
    for (auto const& [songID, sum] : songSum) {
        snap.songAffinity[songID] = sum / (songSupport[songID] + 1.f);
    }
    snap.topCreators = rankTop(snap.creatorAffinity, kMaxRankedCreators, true);
    snap.topSongs = rankTop(snap.songAffinity, kMaxRankedSongs, true);

    m_snapshot = std::move(snap);
    m_snapshotStale = false;
}


TasteSnapshot TasteProfile::snapshot() const {
    std::lock_guard lock(m_mutex);
    ensureSnapshotLocked();
    return m_snapshot;
}

bool TasteProfile::isWarm() const {
    std::lock_guard lock(m_mutex);
    ensureSnapshotLocked();
    return m_seeded || m_snapshot.signalCount >= 3;
}

bool TasteProfile::isKnownLevel(int levelID) const {
    std::lock_guard lock(m_mutex);
    return m_levels.count(levelID) > 0;
}

bool TasteProfile::isDismissed(int levelID) const {
    std::lock_guard lock(m_mutex);
    auto it = m_levels.find(levelID);
    return it != m_levels.end() && it->second.dismissed;
}

int TasteProfile::voteFor(int levelID) const {
    std::lock_guard lock(m_mutex);
    auto it = m_levels.find(levelID);
    return it != m_levels.end() ? it->second.vote : 0;
}

int TasteProfile::favouriteLevelIDForSimilarity() const {
    std::lock_guard lock(m_mutex);
    int best = 0;
    float bestWeight = 0.f;
    for (auto const& [id, rec] : m_levels) {
        float w = interactionWeightLocked(rec);
        if (w > bestWeight) {
            bestWeight = w;
            best = id;
        }
    }
    return best;
}

std::unordered_set<int> TasteProfile::knownLevelIDs() const {
    std::lock_guard lock(m_mutex);
    std::unordered_set<int> out;
    out.reserve(m_levels.size());
    for (auto const& [id, rec] : m_levels) out.insert(id);
    return out;
}


matjson::Value TasteProfile::toJson(LevelInteraction const& rec) const {
    auto obj = matjson::Value::object();
    obj["playCount"] = rec.playCount;
    obj["playSeconds"] = rec.playSeconds;
    obj["attempts"] = rec.attempts;
    obj["bestPercent"] = rec.bestPercent;
    obj["completed"] = rec.completed;
    obj["vote"] = rec.vote;
    obj["favoriteLevel"] = rec.favoriteLevel;
    obj["thumbnailRating"] = rec.thumbnailRating;
    obj["dismissed"] = rec.dismissed;
    obj["difficulty"] = rec.difficulty;
    obj["demonDifficulty"] = rec.demonDifficulty;
    obj["stars"] = rec.stars;
    obj["length"] = rec.length;
    obj["platformer"] = rec.platformer;
    obj["featured"] = rec.featured;
    obj["epicTier"] = rec.epicTier;
    obj["songID"] = rec.songID;
    obj["creatorID"] = rec.creatorID;
    obj["lastSeen"] = rec.lastSeen;
    if (!rec.tags.empty()) {
        auto tags = matjson::Value::array();
        for (auto const& tag : rec.tags) tags.push(tag);
        obj["tags"] = tags;
    }
    return obj;
}

LevelInteraction TasteProfile::fromJson(matjson::Value const& value) const {
    LevelInteraction rec;
    rec.playCount = value["playCount"].asInt().unwrapOr(0);
    rec.playSeconds = static_cast<float>(value["playSeconds"].asDouble().unwrapOr(0.0));
    rec.attempts = value["attempts"].asInt().unwrapOr(0);
    rec.bestPercent = value["bestPercent"].asInt().unwrapOr(0);
    rec.completed = value["completed"].asBool().unwrapOr(false);
    rec.vote = std::clamp<int>(static_cast<int>(value["vote"].asInt().unwrapOr(0)), -1, 1);
    rec.favoriteLevel = value["favoriteLevel"].asBool().unwrapOr(false);
    rec.thumbnailRating = value["thumbnailRating"].asInt().unwrapOr(0);
    rec.dismissed = value["dismissed"].asBool().unwrapOr(false);
    rec.difficulty = value["difficulty"].asInt().unwrapOr(0);
    rec.demonDifficulty = value["demonDifficulty"].asInt().unwrapOr(0);
    rec.stars = value["stars"].asInt().unwrapOr(0);
    rec.length = value["length"].asInt().unwrapOr(0);
    rec.platformer = value["platformer"].asBool().unwrapOr(false);
    rec.featured = value["featured"].asBool().unwrapOr(false);
    rec.epicTier = value["epicTier"].asInt().unwrapOr(0);
    rec.songID = value["songID"].asInt().unwrapOr(0);
    rec.creatorID = value["creatorID"].asInt().unwrapOr(0);
    rec.lastSeen = static_cast<int64_t>(value["lastSeen"].asInt().unwrapOr(0));
    paimon::json::forEachInArray(value["tags"], [&](matjson::Value const& tag) {
        auto name = tag.asString().unwrapOr("");
        if (!name.empty()) rec.tags.push_back(std::move(name));
    });
    return rec;
}

LevelInteraction TasteProfile::fromLegacyJson(matjson::Value const& value) const {
    LevelInteraction rec;
    rec.playCount = value["playCount"].asInt().unwrapOr(0);
    rec.playSeconds = static_cast<float>(value["totalTime"].asDouble().unwrapOr(0.0));
    rec.attempts = value["attempts"].asInt().unwrapOr(0);
    rec.bestPercent = value["bestPercent"].asInt().unwrapOr(0);
    rec.completed = value["completed"].asBool().unwrapOr(false);
    // v2 recorded only positives and conflated favorites with likes.
    rec.vote = value["liked"].asBool().unwrapOr(false) ? 1 : 0;
    rec.favoriteLevel = value["isFavoriteLevel"].asBool().unwrapOr(false);
    rec.thumbnailRating = value["thumbnailRating"].asInt().unwrapOr(0);
    rec.difficulty = value["difficulty"].asInt().unwrapOr(0);
    rec.demonDifficulty = value["demonDifficulty"].asInt().unwrapOr(0);
    rec.stars = value["stars"].asInt().unwrapOr(0);
    rec.length = value["length"].asInt().unwrapOr(0);
    rec.platformer = value["isPlatformer"].asBool().unwrapOr(false);
    rec.featured = value["featured"].asBool().unwrapOr(false);
    rec.epicTier = value["epic"].asBool().unwrapOr(false) ? 1 : 0;
    rec.songID = value["songID"].asInt().unwrapOr(0);
    rec.creatorID = value["creatorID"].asInt().unwrapOr(0);
    rec.lastSeen = static_cast<int64_t>(value["lastPlayed"].asInt().unwrapOr(0));
    paimon::json::forEachInArray(value["tags"], [&](matjson::Value const& tag) {
        auto name = tag.asString().unwrapOr("");
        if (!name.empty()) rec.tags.push_back(std::move(name));
    });
    return rec;
}

void TasteProfile::load() {
    std::lock_guard lock(m_mutex);

    m_loaded = true;

    auto path = profilePath();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) return;

    std::ifstream file(path);
    if (!file.is_open()) {
        log::warn("[ForYou] Could not open the taste profile for reading");
        return;
    }
    std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    auto parsed = matjson::parse(contents);
    if (!parsed.isOk()) {
        log::warn("[ForYou] Taste profile is not valid JSON: {}", parsed.unwrapErr());
        return;
    }
    // Avoid operator[] here; probing must not insert null entries.
    auto document = parsed.unwrap();
    matjson::Value const& root = document;

    int version = root["version"].asInt().unwrapOr(2);
    bool legacy = version < 3;

    if (root["levels"].isObject()) {
        for (auto const& [key, value] : root["levels"]) {
            auto id = geode::utils::numFromString<int>(key);
            if (!id.isOk()) continue;
            m_levels[id.unwrap()] = legacy ? fromLegacyJson(value) : fromJson(value);
        }
    }

    paimon::json::forEachInArray(root["favoriteCreators"], [&](matjson::Value const& value) {
        int id = value.asInt().unwrapOr(0);
        if (id > 0) m_favoriteCreators.insert(id);
    });
    paimon::json::forEachInArray(root["favoriteLevels"], [&](matjson::Value const& value) {
        int id = value.asInt().unwrapOr(0);
        if (id > 0) m_favoriteLevels.insert(id);
    });

    if (root["pinnedTags"].isObject()) {
        for (auto const& [tag, value] : root["pinnedTags"]) {
            int vote = value.asInt().unwrapOr(0);
            if (vote != 0 && !tag.empty()) m_pinnedTags[tag] = vote > 0 ? 1 : -1;
        }
    }

    // v2 used the old preferencesSeeded/seededPreferences names.
    m_seeded = root["seeded"].asBool().unwrapOr(false) ||
               root["preferencesSeeded"].asBool().unwrapOr(false);
    auto const& seed = root.contains("seed") ? root["seed"] : root["seededPreferences"];
    if (m_seeded && seed.isObject()) {
        m_seedDifficulty = seed["difficulty"].asInt().unwrapOr(30);
        m_seedPlatformerRatio = static_cast<float>(seed["platformerRatio"].asDouble().unwrapOr(0.0));
        m_seedLength = seed["length"].asInt().unwrapOr(5);
        m_seedStarRated = seed["starRated"].asBool().unwrapOr(true);
        m_seedFeatured = seed["featured"].asBool().unwrapOr(false);
        m_seedEpic = seed["epic"].asBool().unwrapOr(false);
        m_seedDemonDifficulty = seed["demonDifficulty"].asInt().unwrapOr(0);
    }

    rebuildLocked();
m_dirty = legacy; // Rewrite migrated data on the next save.
    log::info("[ForYou] Loaded {} tracked levels (v{}, {} likes, {} dislikes)",
              m_levels.size(), version, m_snapshot.likeCount, m_snapshot.dislikeCount);
}

void TasteProfile::save() {
    std::lock_guard lock(m_mutex);
    if (!m_dirty) return;

    auto root = matjson::Value::object();
    root["version"] = 3;

    auto levels = matjson::Value::object();
    for (auto const& [id, rec] : m_levels) {
        levels[std::to_string(id)] = toJson(rec);
    }
    root["levels"] = levels;

    auto creators = matjson::Value::array();
    for (int id : m_favoriteCreators) creators.push(id);
    root["favoriteCreators"] = creators;

    auto favorites = matjson::Value::array();
    for (int id : m_favoriteLevels) favorites.push(id);
    root["favoriteLevels"] = favorites;

    auto pinned = matjson::Value::object();
    for (auto const& [tag, vote] : m_pinnedTags) pinned[tag] = vote;
    root["pinnedTags"] = pinned;

    root["seeded"] = m_seeded;
    if (m_seeded) {
        auto seed = matjson::Value::object();
        seed["difficulty"] = m_seedDifficulty;
        seed["platformerRatio"] = m_seedPlatformerRatio;
        seed["length"] = m_seedLength;
        seed["starRated"] = m_seedStarRated;
        seed["featured"] = m_seedFeatured;
        seed["epic"] = m_seedEpic;
        seed["demonDifficulty"] = m_seedDemonDifficulty;
        root["seed"] = seed;
    }

    auto path = profilePath();
    auto tmpPath = std::filesystem::path(path).replace_extension(".tmp");

    std::ofstream file(tmpPath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        log::warn("[ForYou] Could not open the taste profile for writing");
        return;
    }
    file << root.dump();
    file.close();

    std::error_code ec;
    std::filesystem::rename(tmpPath, path, ec);
    if (ec) {
        log::warn("[ForYou] Could not replace the taste profile: {}", ec.message());
        return;
    }

    m_dirty = false;
}

void TasteProfile::reset() {
    {
        std::lock_guard lock(m_mutex);
        m_levels.clear();
        m_favoriteCreators.clear();
        m_favoriteLevels.clear();
        m_pinnedTags.clear();
        m_seeded = false;
        m_activeSessionLevelID = 0;
        m_dirty = true;
        rebuildLocked();
    }
    save();
}

}
