#pragma once
#include <Geode/Geode.hpp>
#include <Geode/binding/GJSearchObject.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <vector>
#include <string>

namespace paimon::foryou {

class ForYouTracker;

enum class QueryStrategy {
    DifficultyMatch,
    FeaturedDiscovery,
    TrendingAtDifficulty,
    CreatorFollowUp,
    SimilarLevels,
    SongMatch,
    TagBasedDiscovery,
    FavoriteCreatorLevels
};

struct ForYouQuery {
    QueryStrategy strategy;
    GJSearchObject* searchObj = nullptr;
    std::string label;
};

class ForYouEngine {
public:
    static ForYouEngine& get();

    std::vector<ForYouQuery> generateQueries(int count = 3);

    // Score and sort results by relevance to user profile
    void scoreAndSortResults(std::vector<geode::Ref<GJGameLevel>>& results);

private:
    ForYouEngine() = default;

    ForYouQuery buildDifficultyMatch();
    ForYouQuery buildFeaturedDiscovery();
    ForYouQuery buildTrendingAtDifficulty();
    ForYouQuery buildCreatorFollowUp();
    ForYouQuery buildSimilarLevels();
    ForYouQuery buildSongMatch();
    ForYouQuery buildTagBasedDiscovery();
    ForYouQuery buildFavoriteCreatorLevels();

    float scoreLevelForUser(GJGameLevel* level) const;

    int randomPage() const;
    int m_lastStrategyIndex = 0;
};

} // namespace paimon::foryou
