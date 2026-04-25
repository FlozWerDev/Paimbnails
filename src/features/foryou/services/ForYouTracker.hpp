#pragma once
#include <Geode/Geode.hpp>
#include <matjson.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <array>
#include <chrono>
#include <mutex>

namespace paimon::foryou {

struct LevelRecord {
    int playCount = 0;
    float totalTime = 0.f;
    int bestPercent = 0;
    bool completed = false;
    int attempts = 0;
    bool liked = false;
    int thumbnailRating = 0;
    int difficulty = 0;
    int demonDifficulty = 0;
    int stars = 0;
    int length = 0;
    int songID = 0;
    int audioTrack = 0;
    int creatorID = 0;
    bool featured = false;
    bool epic = false;
    bool isPlatformer = false;
    bool hasStars = false;
    bool isFavoriteLevel = false;
    bool isFavoriteCreator = false;
    int64_t lastPlayed = 0;
    std::vector<std::string> tags;
};

struct UserProfile {
    int totalLevelsPlayed = 0;
    int totalCompletions = 0;
    float totalPlayTime = 0.f;
    float avgDifficulty = 0.f;
    float avgLength = 0.f;
    std::vector<int> preferredSongs;
    std::vector<int> preferredCreators;
    int preferredDifficulty = 0;
    int preferredLength = 0;
    int likesGiven = 0;
    float avgThumbnailRating = 0.f;
    std::unordered_map<std::string, int> tagFrequency;
    std::vector<std::string> preferredTags;

    // v2: distribution histograms & ratios
    std::array<float, 7> difficultyHistogram{};   // indices 0..6 (NA,Easy..Demon)
    std::array<float, 6> lengthHistogram{};        // indices 0..5 (Tiny..Plat)
    float starRatedRatio = 0.f;
    float featuredRatio = 0.f;
    float epicRatio = 0.f;
    float platformerRatio = 0.f;
    int preferredDemonDifficulty = 0; // 0=Any, 1=Easy..5=Extreme

    // v2: explicit favorites
    std::unordered_set<int> favoriteCreators;
    std::unordered_set<int> favoriteLevels;
};

class ForYouTracker {
public:
    static ForYouTracker& get();

    // tracking events
    void onLevelEnter(GJGameLevel* level);
    void onLevelExit(GJGameLevel* level);
    void onLevelComplete(GJGameLevel* level);
    void onThumbnailRated(int levelID, int stars);
    void onLevelLiked(int levelID);
    void onLevelTagsFetched(int levelID, std::vector<std::string> const& tags);

    // persistence
    void load();
    void save();

    // tag queries
    std::vector<std::string> getPreferredTags(int topN = 5) const;

    // explicit favorites
    void onFavoriteCreator(int creatorID);
    void onUnfavoriteCreator(int creatorID);
    void onFavoriteLevel(int levelID);
    void onUnfavoriteLevel(int levelID);
    bool isCreatorFavorited(int creatorID) const;
    bool isLevelFavorited(int levelID) const;

    // preference seeding (for onboarding when no play data)
    void seedPreferences(int difficulty, float platformerRatio, int length, bool starRated, bool featured, bool epic, int demonDiff);
    bool isSeeded() const;

    // accessors
    UserProfile const& getProfile() const { return m_profile; }
    std::unordered_map<int, LevelRecord> const& getLevels() const { return m_levels; }
    bool hasEnoughData(int minLevels = 5) const;
    bool isLevelTracked(int levelID) const;

private:
    ForYouTracker();
    void recalculateProfile();
    void applySeedPreferencesLocked(int difficulty, float platformerRatio, int length, bool starRated, bool featured, bool epic, int demonDiff);

    matjson::Value levelRecordToJson(LevelRecord const& rec) const;
    LevelRecord jsonToLevelRecord(matjson::Value const& val) const;

    std::unordered_map<int, LevelRecord> m_levels;
    UserProfile m_profile;
    bool m_dirty = false;
    bool m_loaded = false;
    bool m_preferencesSeeded = false;

    // active session tracking
    int m_activeSessionLevelID = 0;
    std::chrono::steady_clock::time_point m_sessionStart;

    mutable std::mutex m_mutex;

    std::filesystem::path getProfilePath() const;
};

} // namespace paimon::foryou
