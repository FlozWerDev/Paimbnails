#include "ForYouTracker.hpp"
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GameManager.hpp>
#include <fstream>
#include <algorithm>
#include <ctime>

using namespace geode::prelude;

namespace paimon::foryou {

ForYouTracker& ForYouTracker::get() {
    static ForYouTracker instance;
    return instance;
}

ForYouTracker::ForYouTracker() {
    load();
}

std::filesystem::path ForYouTracker::getProfilePath() const {
    return Mod::get()->getSaveDir() / "foryou_profile.json";
}

// ── tracking events ─────────────────────────────────────

void ForYouTracker::onLevelEnter(GJGameLevel* level) {
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
    rec.length = static_cast<int>(level->m_levelLength);
    rec.songID = level->m_songID;
    rec.audioTrack = level->m_audioTrack;
    rec.creatorID = level->m_accountID;
    rec.featured = level->m_featured > 0;
    rec.epic = level->m_isEpic > 0;
    rec.isPlatformer = level->isPlatformer();
    rec.hasStars = level->m_stars > 0;
    rec.lastPlayed = static_cast<int64_t>(std::time(nullptr));

    // capturar progreso actual
    rec.attempts = level->m_attempts;
    if (level->m_normalPercent > rec.bestPercent) {
        rec.bestPercent = level->m_normalPercent;
    }

    // detectar favorito
    if (level->m_levelFavorited) {
        rec.liked = true;
    }

    m_dirty = true;
}

void ForYouTracker::onLevelExit(GJGameLevel* level) {
    if (!level || level->m_levelID <= 0) return;

    std::lock_guard lock(m_mutex);

    int id = level->m_levelID;

    if (m_activeSessionLevelID == id) {
        auto elapsed = std::chrono::steady_clock::now() - m_sessionStart;
        float seconds = std::chrono::duration<float>(elapsed).count();

        auto& rec = m_levels[id];
        rec.totalTime += seconds;

        // actualizar progreso final de la sesion
        rec.attempts = level->m_attempts;
        if (level->m_normalPercent > rec.bestPercent) {
            rec.bestPercent = level->m_normalPercent;
        }
        if (level->m_levelFavorited) {
            rec.liked = true;
        }

        m_activeSessionLevelID = 0;
        m_dirty = true;
    }
}

void ForYouTracker::onLevelComplete(GJGameLevel* level) {
    if (!level || level->m_levelID <= 0) return;

    std::lock_guard lock(m_mutex);

    int id = level->m_levelID;
    auto& rec = m_levels[id];
    rec.completed = true;
    rec.bestPercent = 100;
    rec.attempts = level->m_attempts;
    m_dirty = true;
}

void ForYouTracker::onThumbnailRated(int levelID, int stars) {
    if (levelID <= 0 || stars < 1 || stars > 5) return;

    std::lock_guard lock(m_mutex);

    auto& rec = m_levels[levelID];
    rec.thumbnailRating = stars;
    m_dirty = true;
}

void ForYouTracker::onLevelLiked(int levelID) {
    if (levelID <= 0) return;

    std::lock_guard lock(m_mutex);

    auto& rec = m_levels[levelID];
    rec.liked = true;
    m_dirty = true;
}

void ForYouTracker::onLevelTagsFetched(int levelID, std::vector<std::string> const& tags) {
    if (levelID <= 0 || tags.empty()) return;

    std::lock_guard lock(m_mutex);

    auto it = m_levels.find(levelID);
    if (it == m_levels.end()) return;

    it->second.tags = tags;
    m_dirty = true;
}

std::vector<std::string> ForYouTracker::getPreferredTags(int topN) const {
    std::lock_guard lock(m_mutex);
    // Return cached preferred tags from profile
    if (topN >= static_cast<int>(m_profile.preferredTags.size())) {
        return m_profile.preferredTags;
    }
    return std::vector<std::string>(m_profile.preferredTags.begin(),
                                     m_profile.preferredTags.begin() + topN);
}

// ── accessors ───────────────────────────────────────────

bool ForYouTracker::hasEnoughData(int minLevels) const {
    std::lock_guard lock(m_mutex);
    return m_preferencesSeeded || static_cast<int>(m_levels.size()) >= minLevels;
}

bool ForYouTracker::isLevelTracked(int levelID) const {
    std::lock_guard lock(m_mutex);
    return m_levels.count(levelID) > 0;
}

void ForYouTracker::applySeedPreferencesLocked(int difficulty, float platformerRatio, int length, bool starRated, bool featured, bool epic, int demonDiff) {
    m_profile.preferredDifficulty = difficulty;
    m_profile.platformerRatio = platformerRatio;
    m_profile.preferredDemonDifficulty = demonDiff;

    m_profile.difficultyHistogram.fill(0.f);
    int idx = difficulty / 10;
    if (idx >= 0 && idx < 7) {
        m_profile.difficultyHistogram[idx] = 0.6f;
        if (idx > 0) m_profile.difficultyHistogram[idx - 1] = 0.2f;
        if (idx < 6) m_profile.difficultyHistogram[idx + 1] = 0.2f;
    }

    m_profile.lengthHistogram.fill(0.f);
    if (length >= 0 && length <= 4) {
        m_profile.lengthHistogram[length] = 0.5f;
        if (length > 0) m_profile.lengthHistogram[length - 1] = 0.3f;
        if (length < 4) m_profile.lengthHistogram[length + 1] = 0.2f;
        m_profile.preferredLength = length;
    } else {
        m_profile.lengthHistogram[0] = 0.15f;
        m_profile.lengthHistogram[1] = 0.20f;
        m_profile.lengthHistogram[2] = 0.25f;
        m_profile.lengthHistogram[3] = 0.25f;
        m_profile.lengthHistogram[4] = 0.15f;
        m_profile.preferredLength = 5;
    }

    m_profile.starRatedRatio = starRated ? 1.0f : 0.0f;
    m_profile.featuredRatio = featured ? 0.8f : 0.1f;
    m_profile.epicRatio = epic ? 0.6f : 0.0f;
}

void ForYouTracker::seedPreferences(int difficulty, float platformerRatio, int length, bool starRated, bool featured, bool epic, int demonDiff) {
    std::lock_guard lock(m_mutex);

    applySeedPreferencesLocked(difficulty, platformerRatio, length, starRated, featured, epic, demonDiff);

    m_preferencesSeeded = true;
    m_dirty = true;

    log::info("[ForYou] Preferences seeded: difficulty={}, platformerRatio={}, length={}, star={}, featured={}, epic={}, demonDiff={}",
              difficulty, platformerRatio, length, starRated, featured, epic, demonDiff);
}

bool ForYouTracker::isSeeded() const {
    std::lock_guard lock(m_mutex);
    return m_preferencesSeeded;
}

// ── profile recalculation ───────────────────────────────

void ForYouTracker::recalculateProfile() {
    // called with mutex held
    if (m_levels.empty()) {
        if (!m_preferencesSeeded) {
            m_profile = UserProfile{};
        }
        return;
    }

    auto favoriteCreators = m_profile.favoriteCreators;
    auto favoriteLevels = m_profile.favoriteLevels;
    m_profile = UserProfile{};
    m_profile.favoriteCreators = std::move(favoriteCreators);
    m_profile.favoriteLevels = std::move(favoriteLevels);

    std::unordered_map<int, float> songCounts;
    std::unordered_map<int, float> creatorCounts;
    std::unordered_map<int, float> difficultyCounts;
    std::unordered_map<int, float> lengthCounts;
    std::unordered_map<std::string, float> tagCounts;

    float diffSum = 0.f;
    float lenSum = 0.f;
    float thumbRatingSum = 0.f;
    int thumbRatingCount = 0;
    float totalWeight = 0.f;

    // v2: counters for ratios and histograms
    float starRatedCount = 0.f;
    float featuredCount = 0.f;
    float epicCount = 0.f;
    float platformerCount = 0.f;
    float validLevelCount = 0.f;

    int64_t now = static_cast<int64_t>(std::time(nullptr));
    constexpr int64_t THIRTY_DAYS = 30 * 24 * 60 * 60;
    constexpr float MIN_SESSION_SECONDS = 5.f;
    constexpr float FAVORITE_BOOST = 3.0f;

    for (auto& [id, rec] : m_levels) {
        // Session quality: skip very short sessions (< 5s total)
        if (rec.totalTime < MIN_SESSION_SECONDS && !rec.completed) continue;

        // Time decay: levels played > 30 days ago get 0.5x weight
        float timeWeight = 1.0f;
        if (rec.lastPlayed > 0 && (now - rec.lastPlayed) > THIRTY_DAYS) {
            timeWeight = 0.5f;
        }

        // Session quality weighting
        float qualityWeight = 1.0f;
        if (rec.completed) {
            qualityWeight = 2.0f;
        } else if (rec.bestPercent >= 80) {
            qualityWeight = 1.5f;
        }

        // v2: explicit favorite boost (strongest signal)
        float favoriteBoost = 1.0f;
        if (rec.isFavoriteLevel || rec.isFavoriteCreator) {
            favoriteBoost = FAVORITE_BOOST;
        }

        float weight = timeWeight * qualityWeight * favoriteBoost * static_cast<float>(rec.playCount);

        m_profile.totalLevelsPlayed++;
        m_profile.totalPlayTime += rec.totalTime;

        if (rec.completed) m_profile.totalCompletions++;
        if (rec.liked) m_profile.likesGiven++;

        diffSum += static_cast<float>(rec.difficulty) * weight;
        lenSum += static_cast<float>(rec.length) * weight;
        totalWeight += weight;

        if (rec.songID > 0) songCounts[rec.songID] += weight;
        if (rec.creatorID > 0) creatorCounts[rec.creatorID] += weight;
        difficultyCounts[rec.difficulty] += weight;
        lengthCounts[rec.length] += weight;

        // Tag frequency
        for (auto const& tag : rec.tags) {
            tagCounts[tag] += weight;
        }

        if (rec.thumbnailRating > 0) {
            thumbRatingSum += rec.thumbnailRating;
            thumbRatingCount++;
        }

        // v2: ratio counters
        validLevelCount += 1.f;
        if (rec.hasStars) starRatedCount += 1.f;
        if (rec.featured) featuredCount += 1.f;
        if (rec.epic) epicCount += 1.f;
        if (rec.isPlatformer) platformerCount += 1.f;
    }

    if (totalWeight > 0.f) {
        m_profile.avgDifficulty = diffSum / totalWeight;
        m_profile.avgLength = lenSum / totalWeight;
    }
    m_profile.avgThumbnailRating = thumbRatingCount > 0 ? thumbRatingSum / thumbRatingCount : 0.f;

    // v2: ratios
    if (validLevelCount > 0.f) {
        m_profile.starRatedRatio = starRatedCount / validLevelCount;
        m_profile.featuredRatio = featuredCount / validLevelCount;
        m_profile.epicRatio = epicCount / validLevelCount;
        m_profile.platformerRatio = platformerCount / validLevelCount;
    }

    // v2: difficulty histogram (normalized)
    m_profile.difficultyHistogram.fill(0.f);
    if (totalWeight > 0.f) {
        for (auto& [d, c] : difficultyCounts) {
            int idx = d / 10; // 0=NA, 10->1=Easy, 20->2=Normal, 30->3=Hard, 40->4=Harder, 50->5=Insane, 60->6=Demon
            if (idx >= 0 && idx < 7) {
                m_profile.difficultyHistogram[idx] += c / totalWeight;
            }
        }
    }

    // v2: length histogram (normalized)
    m_profile.lengthHistogram.fill(0.f);
    if (totalWeight > 0.f) {
        for (auto& [l, c] : lengthCounts) {
            if (l >= 0 && l < 6) {
                m_profile.lengthHistogram[l] += c / totalWeight;
            }
        }
    }

    // preferred difficulty: mode (highest weighted)
    float maxDiffCount = 0.f;
    for (auto& [d, c] : difficultyCounts) {
        if (c > maxDiffCount) { maxDiffCount = c; m_profile.preferredDifficulty = d; }
    }

    // preferred length: mode
    float maxLenCount = 0.f;
    for (auto& [l, c] : lengthCounts) {
        if (c > maxLenCount) { maxLenCount = c; m_profile.preferredLength = l; }
    }

    // top 5 songs by weighted count
    std::vector<std::pair<int, float>> sortedSongs(songCounts.begin(), songCounts.end());
    std::sort(sortedSongs.begin(), sortedSongs.end(), [](auto& a, auto& b) { return a.second > b.second; });
    m_profile.preferredSongs.clear();
    for (size_t i = 0; i < std::min<size_t>(5, sortedSongs.size()); i++) {
        m_profile.preferredSongs.push_back(sortedSongs[i].first);
    }

    // top 5 creators by weighted count
    std::vector<std::pair<int, float>> sortedCreators(creatorCounts.begin(), creatorCounts.end());
    std::sort(sortedCreators.begin(), sortedCreators.end(), [](auto& a, auto& b) { return a.second > b.second; });
    m_profile.preferredCreators.clear();
    for (size_t i = 0; i < std::min<size_t>(5, sortedCreators.size()); i++) {
        m_profile.preferredCreators.push_back(sortedCreators[i].first);
    }

    // tag frequency + preferred tags (top 5)
    m_profile.tagFrequency.clear();
    for (auto& [tag, count] : tagCounts) {
        m_profile.tagFrequency[tag] = static_cast<int>(count);
    }
    std::vector<std::pair<std::string, float>> sortedTags(tagCounts.begin(), tagCounts.end());
    std::sort(sortedTags.begin(), sortedTags.end(), [](auto& a, auto& b) { return a.second > b.second; });
    m_profile.preferredTags.clear();
    for (size_t i = 0; i < std::min<size_t>(5, sortedTags.size()); i++) {
        m_profile.preferredTags.push_back(sortedTags[i].first);
    }
}

// ── JSON serialization ──────────────────────────────────

matjson::Value ForYouTracker::levelRecordToJson(LevelRecord const& rec) const {
    auto obj = matjson::Value::object();
    obj["playCount"] = rec.playCount;
    obj["totalTime"] = rec.totalTime;
    obj["bestPercent"] = rec.bestPercent;
    obj["completed"] = rec.completed;
    obj["attempts"] = rec.attempts;
    obj["liked"] = rec.liked;
    obj["thumbnailRating"] = rec.thumbnailRating;
    obj["difficulty"] = rec.difficulty;
    obj["demonDifficulty"] = rec.demonDifficulty;
    obj["stars"] = rec.stars;
    obj["length"] = rec.length;
    obj["songID"] = rec.songID;
    obj["audioTrack"] = rec.audioTrack;
    obj["creatorID"] = rec.creatorID;
    obj["featured"] = rec.featured;
    obj["epic"] = rec.epic;
    obj["isPlatformer"] = rec.isPlatformer;
    obj["hasStars"] = rec.hasStars;
    obj["isFavoriteLevel"] = rec.isFavoriteLevel;
    obj["isFavoriteCreator"] = rec.isFavoriteCreator;
    obj["lastPlayed"] = rec.lastPlayed;
    if (!rec.tags.empty()) {
        auto tagsArr = matjson::Value::array();
        for (auto const& tag : rec.tags) {
            tagsArr.push(tag);
        }
        obj["tags"] = tagsArr;
    }
    return obj;
}

LevelRecord ForYouTracker::jsonToLevelRecord(matjson::Value const& val) const {
    LevelRecord rec;
    rec.playCount = val["playCount"].asInt().unwrapOr(0);
    rec.totalTime = static_cast<float>(val["totalTime"].asDouble().unwrapOr(0.0));
    rec.bestPercent = val["bestPercent"].asInt().unwrapOr(0);
    rec.completed = val["completed"].asBool().unwrapOr(false);
    rec.attempts = val["attempts"].asInt().unwrapOr(0);
    rec.liked = val["liked"].asBool().unwrapOr(false);
    rec.thumbnailRating = val["thumbnailRating"].asInt().unwrapOr(0);
    rec.difficulty = val["difficulty"].asInt().unwrapOr(0);
    rec.demonDifficulty = val["demonDifficulty"].asInt().unwrapOr(0);
    rec.stars = val["stars"].asInt().unwrapOr(0);
    rec.length = val["length"].asInt().unwrapOr(0);
    rec.songID = val["songID"].asInt().unwrapOr(0);
    rec.audioTrack = val["audioTrack"].asInt().unwrapOr(0);
    rec.creatorID = val["creatorID"].asInt().unwrapOr(0);
    rec.featured = val["featured"].asBool().unwrapOr(false);
    rec.epic = val["epic"].asBool().unwrapOr(false);
    rec.isPlatformer = val["isPlatformer"].asBool().unwrapOr(false);
    rec.hasStars = val["hasStars"].asBool().unwrapOr(false);
    rec.isFavoriteLevel = val["isFavoriteLevel"].asBool().unwrapOr(false);
    rec.isFavoriteCreator = val["isFavoriteCreator"].asBool().unwrapOr(false);
    rec.lastPlayed = static_cast<int64_t>(val["lastPlayed"].asInt().unwrapOr(0));
    if (val.contains("tags") && val["tags"].isArray()) {
        for (auto const& tag : val["tags"].asArray().unwrap()) {
            auto s = tag.asString().unwrapOr("");
            if (!s.empty()) rec.tags.push_back(s);
        }
    }
    return rec;
}

// ── persistence ─────────────────────────────────────────

void ForYouTracker::load() {
    std::lock_guard lock(m_mutex);

    auto path = getProfilePath();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        m_loaded = true;
        return;
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        log::warn("[ForYou] Failed to open profile file for reading");
        m_loaded = true;
        return;
    }

    std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    auto parseResult = matjson::parse(contents);
    if (!parseResult.isOk()) {
        log::warn("[ForYou] Failed to parse profile JSON: {}", parseResult.unwrapErr());
        m_loaded = true;
        return;
    }

    auto root = parseResult.unwrap();

    if (root.contains("levels") && root["levels"].isObject()) {
        for (auto& [key, val] : root["levels"]) {
            auto idResult = geode::utils::numFromString<int>(key);
            if (idResult.isOk()) {
                m_levels[idResult.unwrap()] = jsonToLevelRecord(val);
            }
        }
    }

    // v2: load favorite sets
    if (root.contains("favoriteCreators") && root["favoriteCreators"].isArray()) {
        for (auto const& v : root["favoriteCreators"].asArray().unwrap()) {
            auto id = v.asInt().unwrapOr(0);
            if (id > 0) m_profile.favoriteCreators.insert(id);
        }
    }
    if (root.contains("favoriteLevels") && root["favoriteLevels"].isArray()) {
        for (auto const& v : root["favoriteLevels"].asArray().unwrap()) {
            auto id = v.asInt().unwrapOr(0);
            if (id > 0) m_profile.favoriteLevels.insert(id);
        }
    }

    // v2: load seeded flag
    m_preferencesSeeded = root.contains("preferencesSeeded") &&
                          root["preferencesSeeded"].asBool().unwrapOr(false);

    if (m_preferencesSeeded && root.contains("seededPreferences") && root["seededPreferences"].isObject()) {
        auto const& seeded = root["seededPreferences"];
        applySeedPreferencesLocked(
            seeded["difficulty"].asInt().unwrapOr(30),
            static_cast<float>(seeded["platformerRatio"].asDouble().unwrapOr(0.0)),
            seeded["length"].asInt().unwrapOr(5),
            seeded["starRated"].asBool().unwrapOr(true),
            seeded["featured"].asBool().unwrapOr(false),
            seeded["epic"].asBool().unwrapOr(false),
            seeded["demonDifficulty"].asInt().unwrapOr(0)
        );
    }

    recalculateProfile();
    m_loaded = true;
    m_dirty = false;
    log::info("[ForYou] Loaded profile with {} tracked levels", m_levels.size());
}

void ForYouTracker::save() {
    std::lock_guard lock(m_mutex);

    if (!m_dirty && m_loaded) return;

    recalculateProfile();

    auto root = matjson::Value::object();
    auto levelsObj = matjson::Value::object();

    for (auto& [id, rec] : m_levels) {
        levelsObj[std::to_string(id)] = levelRecordToJson(rec);
    }
    root["levels"] = levelsObj;
    root["version"] = 2;

    // v2: save favorite sets
    auto favCreatorsArr = matjson::Value::array();
    for (int cid : m_profile.favoriteCreators) {
        favCreatorsArr.push(cid);
    }
    root["favoriteCreators"] = favCreatorsArr;

    auto favLevelsArr = matjson::Value::array();
    for (int lid : m_profile.favoriteLevels) {
        favLevelsArr.push(lid);
    }
    root["favoriteLevels"] = favLevelsArr;

    // v2: save seeded flag
    root["preferencesSeeded"] = m_preferencesSeeded;
    if (m_preferencesSeeded) {
        auto seeded = matjson::Value::object();
        seeded["difficulty"] = m_profile.preferredDifficulty;
        seeded["platformerRatio"] = m_profile.platformerRatio;
        seeded["length"] = m_profile.preferredLength;
        seeded["starRated"] = m_profile.starRatedRatio >= 0.99f;
        seeded["featured"] = m_profile.featuredRatio >= 0.7f;
        seeded["epic"] = m_profile.epicRatio >= 0.4f;
        seeded["demonDifficulty"] = m_profile.preferredDemonDifficulty;
        root["seededPreferences"] = seeded;
    }

    auto path = getProfilePath();
    auto tmpPath = std::filesystem::path(path).replace_extension(".tmp");

    std::ofstream file(tmpPath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        log::warn("[ForYou] Failed to open profile file for writing");
        return;
    }
    file << root.dump();
    file.close();

    std::error_code ec;
    std::filesystem::rename(tmpPath, path, ec);
    if (ec) {
        log::warn("[ForYou] Failed to rename tmp profile file: {}", ec.message());
        return;
    }

    m_dirty = false;
    log::info("[ForYou] Saved profile with {} tracked levels", m_levels.size());
}

// ── explicit favorites ──────────────────────────────────

void ForYouTracker::onFavoriteCreator(int creatorID) {
    if (creatorID <= 0) return;
    std::lock_guard lock(m_mutex);
    m_profile.favoriteCreators.insert(creatorID);
    // mark all levels by this creator as favorite-creator
    for (auto& [id, rec] : m_levels) {
        if (rec.creatorID == creatorID) {
            rec.isFavoriteCreator = true;
        }
    }
    m_dirty = true;
    recalculateProfile();
}

void ForYouTracker::onUnfavoriteCreator(int creatorID) {
    if (creatorID <= 0) return;
    std::lock_guard lock(m_mutex);
    m_profile.favoriteCreators.erase(creatorID);
    for (auto& [id, rec] : m_levels) {
        if (rec.creatorID == creatorID) {
            rec.isFavoriteCreator = false;
        }
    }
    m_dirty = true;
    recalculateProfile();
}

void ForYouTracker::onFavoriteLevel(int levelID) {
    if (levelID <= 0) return;
    std::lock_guard lock(m_mutex);
    m_profile.favoriteLevels.insert(levelID);
    auto it = m_levels.find(levelID);
    if (it != m_levels.end()) {
        it->second.isFavoriteLevel = true;
    }
    m_dirty = true;
    recalculateProfile();
}

void ForYouTracker::onUnfavoriteLevel(int levelID) {
    if (levelID <= 0) return;
    std::lock_guard lock(m_mutex);
    m_profile.favoriteLevels.erase(levelID);
    auto it = m_levels.find(levelID);
    if (it != m_levels.end()) {
        it->second.isFavoriteLevel = false;
    }
    m_dirty = true;
    recalculateProfile();
}

bool ForYouTracker::isCreatorFavorited(int creatorID) const {
    std::lock_guard lock(m_mutex);
    return m_profile.favoriteCreators.count(creatorID) > 0;
}

bool ForYouTracker::isLevelFavorited(int levelID) const {
    std::lock_guard lock(m_mutex);
    return m_profile.favoriteLevels.count(levelID) > 0;
}

} // namespace paimon::foryou
