#pragma once

// Two UI-driven phases: build candidate queries, then score and diversify the
// results. Tag affinity is the dominant signal.

#include <Geode/Geode.hpp>

#include <functional>
#include <string>
#include <vector>

namespace paimon::foryou {

struct TasteSnapshot;

enum class FeedSource {
    TagMatch,
    TagPair,
    TagExplore,
    DifficultyMatch,
    Featured,
    Trending,
    Similar,
    FavoriteCreator,
    TopCreator,
    SongMatch,
    Explore
};

struct FeedQuery {
    FeedSource source = FeedSource::Featured;
    // Keep the search object alive while queued work drains across frames.
    geode::Ref<GJSearchObject> searchObj = nullptr;
    std::string label;
};

struct Recommendation {
    geode::Ref<GJGameLevel> level = nullptr;
    float score = 0.f;
    float tagScore = 0.f;
    std::vector<std::string> tags;
    // Localization key and its single argument.
    std::string reasonKey;
    std::string reasonArg;
};

class RecommendationEngine {
public:
    static RecommendationEngine& get();

    // Build the candidate plan; callback runs on the main thread.
    void planQueries(std::function<void(std::vector<FeedQuery>)> callback);

    // Score candidates and return up to limit picks; callback runs on the main thread.
    void rank(std::vector<geode::Ref<GJGameLevel>> candidates, int limit,
              std::function<void(std::vector<Recommendation>)> callback);

    // Score summary from the last rank(), newest first.
    std::string const& lastPlanSummary() const { return m_lastPlanSummary; }

private:
    RecommendationEngine() = default;

    // Type19 searches are paged in groups of ten IDs.
    std::vector<FeedQuery> buildTagQueries(std::vector<int> const& pool, FeedSource source);
    std::vector<FeedQuery> buildNativeQueries(TasteSnapshot const& taste, int budget);

    FeedQuery buildDifficultyMatch(TasteSnapshot const& taste);
    FeedQuery buildFeatured(TasteSnapshot const& taste);
    FeedQuery buildTrending(TasteSnapshot const& taste);
    FeedQuery buildSimilar(TasteSnapshot const& taste);
    FeedQuery buildFavoriteCreator(TasteSnapshot const& taste);
    FeedQuery buildTopCreator(TasteSnapshot const& taste);
    FeedQuery buildSongMatch(TasteSnapshot const& taste);
    FeedQuery buildExplore(TasteSnapshot const& taste);

    struct Scored {
        geode::Ref<GJGameLevel> level;
        std::vector<std::string> tags;
        float base = 0.f;
        float tagScore = 0.f;
        std::string reasonKey;
        std::string reasonArg;
        bool rejected = false;
    };

    Scored scoreLevel(GJGameLevel* level, std::vector<std::string> tags,
                      TasteSnapshot const& taste) const;
    // Greedy diversity selection penalizes similarity to existing picks.
    std::vector<Recommendation> diversify(std::vector<Scored>& scored, int limit) const;

    // Rotate the first native strategy between refreshes.
    int m_strategyCursor = 0;
    std::string m_lastPlanSummary;
};

}
