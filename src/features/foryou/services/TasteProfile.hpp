#pragma once

// Signed interactions feed For You's level, tag, creator, and song affinities.

#include <Geode/Geode.hpp>
#include <matjson.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace paimon::foryou {

// Index is GJ difficulty / 10 (0 = NA, 6 = Demon).
constexpr size_t kDifficultyBuckets = 7;
// 0..4 = Tiny..XL; 5 = Platformer.
constexpr size_t kLengthBuckets = 6;

struct LevelInteraction {
    int playCount = 0;
    float playSeconds = 0.f;
    int attempts = 0;
    int bestPercent = 0;
    bool completed = false;

    // +1 liked, -1 disliked, 0 untouched.
    int vote = 0;
    bool favoriteLevel = false;
    // 1..5; 0 means unrated.
    int thumbnailRating = 0;
    bool dismissed = false;

    int difficulty = 0;        // 0 or 10..60.
    int demonDifficulty = 0;   // 0 = n/a, 1..5.
    int stars = 0;
    int length = 0;            // 0..4.
    bool platformer = false;
    bool featured = false;
    int epicTier = 0;          // 0 none, 1 epic, 2 legendary, 3 mythic.
    int songID = 0;
    int creatorID = 0;

    int64_t lastSeen = 0;
    std::vector<std::string> tags;
};

// Derived from interactions; explicit choices are persisted separately.
struct TasteSnapshot {
    // Signed affinity, roughly [-1, 1].
    std::unordered_map<std::string, float> tagAffinity;
    // Strongest first; negatives drive exclusion.
    std::vector<std::string> topTags;
    std::vector<std::string> avoidedTags;

    std::unordered_map<int, float> creatorAffinity;
    std::unordered_map<int, float> songAffinity;
    std::vector<int> topCreators;
    std::vector<int> topSongs;

    std::array<float, kDifficultyBuckets> difficultyHistogram{};
    std::array<float, kLengthBuckets> lengthHistogram{};
    std::array<float, 6> demonHistogram{};   // Index 0 unused.

    float starRatedRatio = 0.f;
    float featuredRatio = 0.f;
    float epicRatio = 0.f;
    float platformerRatio = 0.f;

    int preferredDifficulty = 0;   // Histogram mode, GD units.
    int preferredLength = 5;       // 5 = no preference.
    int preferredDemonDifficulty = 0;

    // Meaningful interaction count used for confidence.
    int signalCount = 0;
    int likeCount = 0;
    int dislikeCount = 0;
    int taggedSignalCount = 0;

    std::unordered_set<int> favoriteCreators;
    std::unordered_set<int> favoriteLevels;

    // Manual choices outrank inferred history; values are +1/-1.
    std::unordered_map<std::string, int> pinnedTags;
};

class TasteProfile {
public:
    static TasteProfile& get();

    void onLevelEnter(GJGameLevel* level);
    void onLevelExit(GJGameLevel* level);
    void onLevelComplete(GJGameLevel* level);
    void onLevelVote(int levelID, bool liked);
    void onThumbnailRated(int levelID, int stars);
    void onTagsResolved(int levelID, std::vector<std::string> const& tags);
    void onLevelDismissed(int levelID);

    void onFavoriteCreator(int creatorID);
    void onUnfavoriteCreator(int creatorID);
    void onFavoriteLevel(int levelID);
    void onUnfavoriteLevel(int levelID);
    bool isCreatorFavorited(int creatorID) const;
    bool isLevelFavorited(int levelID) const;

    // +1 love, -1 avoid, 0 clear.
    void setPinnedTag(std::string const& tag, int vote);
    int pinnedTagVote(std::string const& tag) const;
    std::vector<std::string> pinnedTags(int vote) const;

    void seedPreferences(int difficulty, float platformerRatio, int length,
                         bool starRated, bool featured, bool epic, int demonDifficulty);
    bool isSeeded() const;

    TasteSnapshot snapshot() const;
    bool isWarm() const;
    bool isKnownLevel(int levelID) const;
    bool isDismissed(int levelID) const;
    int voteFor(int levelID) const;
    // Best positive level for similarity, or 0.
    int favouriteLevelIDForSimilarity() const;
    std::unordered_set<int> knownLevelIDs() const;

    void load();
    void save();
    void reset();

private:
    TasteProfile();

    // Events mark the snapshot stale; reads rebuild it off the event path.
    void rebuildLocked() const;
    void ensureSnapshotLocked() const;
    // Apply onboarding priors while history is sparse.
    void applySeedLocked(TasteSnapshot& snapshot) const;
    float interactionWeightLocked(LevelInteraction const& rec) const;

    matjson::Value toJson(LevelInteraction const& rec) const;
    LevelInteraction fromJson(matjson::Value const& value) const;
    // Migrate v2 liked values and missing dismissals.
    LevelInteraction fromLegacyJson(matjson::Value const& value) const;

    std::filesystem::path profilePath() const;

    std::unordered_map<int, LevelInteraction> m_levels;
    mutable TasteSnapshot m_snapshot;
    mutable bool m_snapshotStale = true;

    std::unordered_set<int> m_favoriteCreators;
    std::unordered_set<int> m_favoriteLevels;
    std::unordered_map<std::string, int> m_pinnedTags;

    // Preserve seed values so reloads do not drift.
    bool m_seeded = false;
    int m_seedDifficulty = 30;
    float m_seedPlatformerRatio = 0.f;
    int m_seedLength = 5;
    bool m_seedStarRated = true;
    bool m_seedFeatured = false;
    bool m_seedEpic = false;
    int m_seedDemonDifficulty = 0;

    bool m_dirty = false;
    bool m_loaded = false;

    int m_activeSessionLevelID = 0;
    std::chrono::steady_clock::time_point m_sessionStart;

    mutable std::mutex m_mutex;
};

}
