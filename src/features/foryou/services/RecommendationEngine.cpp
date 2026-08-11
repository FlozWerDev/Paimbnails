#include "RecommendationEngine.hpp"

#include "LevelTagsClient.hpp"
#include "TasteProfile.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"

#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GJSearchObject.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <random>
#include <unordered_set>

using namespace geode::prelude;

namespace {

// Tag affinity is dominant; the remaining signals refine the feed.
constexpr float kWeightTag        = 5.0f;
constexpr float kWeightDifficulty = 1.8f;
constexpr float kWeightLength     = 0.9f;
constexpr float kWeightCreator    = 2.2f;
constexpr float kWeightSong       = 1.1f;
constexpr float kWeightQuality    = 1.2f;
constexpr float kWeightPopularity = 0.8f;

// Untagged levels can surface, but tagged matches rank higher.
constexpr float kUntaggedPenalty = 0.6f;
// Avoidance outweighs a single positive signal.
constexpr float kAvoidMultiplier = 1.6f;
// Diversity penalty for candidates resembling existing picks.
constexpr float kDiversityLambda = 1.2f;

// Type19 lookups need ten-ID pages.
constexpr size_t kIDsPerQuery = 10;
constexpr int kMaxTagQueries = 4;

int defaultFeedSize() {
    return static_cast<int>(std::clamp<int64_t>(
        Mod::get()->getSavedValue<int64_t>("for-you-feed-size", 12), 3, 40));
}

int queryBudget() {
    return static_cast<int>(std::clamp<int64_t>(
        Mod::get()->getSavedValue<int64_t>("for-you-query-budget", 6), 2, 10));
}

float explorationRate() {
    return static_cast<float>(std::clamp(
        Mod::get()->getSavedValue<double>("for-you-exploration", 0.2), 0.0, 0.6));
}

bool tagsEnabled() {
    return Mod::get()->getSavedValue<bool>("for-you-use-tags", true) &&
           paimon::foryou::LevelTagsClient::isAvailable();
}

std::mt19937& rng() {
    static std::mt19937 engine(std::random_device{}());
    return engine;
}

// Gameplay tags predict taste more strongly than style or theme tags.
float categoryWeight(paimon::foryou::TagCategory category) {
    switch (category) {
        case paimon::foryou::TagCategory::Gameplay: return 1.0f;
        case paimon::foryou::TagCategory::Style:    return 0.9f;
        case paimon::foryou::TagCategory::Meta:     return 0.7f;
        case paimon::foryou::TagCategory::Theme:    return 0.6f;
        default:                                    return 0.5f;
    }
}

// Map GD difficulty (10..60) to the search filter's 1..6.
std::string difficultyFilter(int preferredDifficulty) {
    int bucket = preferredDifficulty / 10;
    if (bucket < 1 || bucket > 6) return "-1";
    return std::to_string(bucket);
}

std::string lengthFilter(int preferredLength, bool platformerOnly) {
    if (preferredLength < 0 || preferredLength > 4) return "-1";
    return std::to_string(GJGameLevel::getLengthKey(preferredLength, platformerOnly));
}

int demonFilter(int preferredDifficulty, int preferredDemonDifficulty) {
    if (preferredDifficulty != 60) return 0;
    switch (preferredDemonDifficulty) {
        case 1: return static_cast<int>(GJDifficulty::DemonEasy);
        case 2: return static_cast<int>(GJDifficulty::DemonMedium);
        case 3: return static_cast<int>(GJDifficulty::Demon);
        case 4: return static_cast<int>(GJDifficulty::DemonInsane);
        case 5: return static_cast<int>(GJDifficulty::DemonExtreme);
        default: return 0;
    }
}

// Avoid the 22-argument create overload; its binding can corrupt gd::string fields.
GJSearchObject* makeSearchObject(
    SearchType type,
    gd::string query,
    gd::string difficulty,
    gd::string length,
    int page,
    bool starOnly,
    bool featuredOnly,
    bool epicOnly,
    int songID,
    bool songFilter,
    int demon
) {
    auto obj = GJSearchObject::create(type, query);
    if (!obj) return nullptr;
    obj->m_difficulty = difficulty;
    obj->m_length = length;
    obj->m_page = page;
    obj->m_starFilter = starOnly;
    obj->m_uncompletedFilter = false;
    obj->m_featuredFilter = featuredOnly;
    obj->m_songID = songID;
    obj->m_originalFilter = false;
    obj->m_twoPlayerFilter = false;
    obj->m_customSongFilter = songFilter;
    obj->m_songFilter = songFilter;
    obj->m_noStarFilter = false;
    obj->m_coinsFilter = false;
    obj->m_epicFilter = epicOnly;
    obj->m_legendaryFilter = false;
    obj->m_mythicFilter = false;
    obj->m_completedFilter = false;
    obj->m_demonFilter = static_cast<GJDifficulty>(demon);
    obj->m_folder = 0;
    obj->m_searchMode = 0;
    return obj;
}

float jaccard(std::vector<std::string> const& a, std::vector<std::string> const& b) {
    if (a.empty() || b.empty()) return 0.f;
    std::unordered_set<std::string> lhs(a.begin(), a.end());
    size_t shared = 0;
    for (auto const& tag : b) {
        if (lhs.count(tag)) shared++;
    }
    size_t total = lhs.size() + b.size() - shared;
    return total > 0 ? static_cast<float>(shared) / static_cast<float>(total) : 0.f;
}

}

namespace paimon::foryou {

RecommendationEngine& RecommendationEngine::get() {
    static RecommendationEngine instance;
    return instance;
}


std::vector<FeedQuery> RecommendationEngine::buildTagQueries(
    std::vector<int> const& pool, FeedSource source
) {
    std::vector<FeedQuery> queries;
    if (pool.empty()) return queries;

    std::string ids;
    for (size_t i = 0; i < std::min(kIDsPerQuery, pool.size()); i++) {
        if (i) ids += ',';
        ids += std::to_string(pool[i]);
    }

    // Type19 ignores filters; request every supplied ID for scoring.
    auto obj = makeSearchObject(SearchType::Type19, ids, "-1", "-1", 0,
                                false, false, false, 0, false, 0);
    if (obj) queries.push_back({source, obj, "Tags"});
    return queries;
}

FeedQuery RecommendationEngine::buildDifficultyMatch(TasteSnapshot const& taste) {
    std::uniform_int_distribution<int> pageDist(0, 4);
    auto obj = makeSearchObject(
        SearchType::Awarded, "",
        difficultyFilter(taste.preferredDifficulty),
        lengthFilter(taste.preferredLength, taste.platformerRatio >= 0.99f),
        pageDist(rng()),
        taste.starRatedRatio >= 0.99f,
        taste.featuredRatio >= 0.7f,
        taste.epicRatio >= 0.4f,
        0, false,
        demonFilter(taste.preferredDifficulty, taste.preferredDemonDifficulty));
    return {FeedSource::DifficultyMatch, obj, "Difficulty"};
}

FeedQuery RecommendationEngine::buildFeatured(TasteSnapshot const& taste) {
    std::uniform_int_distribution<int> pageDist(0, 4);
    auto obj = makeSearchObject(
        SearchType::Featured, "",
        difficultyFilter(taste.preferredDifficulty),
        lengthFilter(taste.preferredLength, taste.platformerRatio >= 0.99f),
        pageDist(rng()),
        taste.starRatedRatio >= 0.99f, false,
        taste.epicRatio >= 0.4f,
        0, false,
        demonFilter(taste.preferredDifficulty, taste.preferredDemonDifficulty));
    return {FeedSource::Featured, obj, "Featured"};
}

FeedQuery RecommendationEngine::buildTrending(TasteSnapshot const& taste) {
    std::uniform_int_distribution<int> pageDist(0, 2);
    auto obj = makeSearchObject(
        SearchType::Trending, "",
        difficultyFilter(taste.preferredDifficulty),
        lengthFilter(taste.preferredLength, taste.platformerRatio >= 0.99f),
        pageDist(rng()),
        taste.starRatedRatio >= 0.99f,
        taste.featuredRatio >= 0.7f, false,
        0, false,
        demonFilter(taste.preferredDifficulty, taste.preferredDemonDifficulty));
    return {FeedSource::Trending, obj, "Trending"};
}

FeedQuery RecommendationEngine::buildSimilar(TasteSnapshot const&) {
    int seedLevel = TasteProfile::get().favouriteLevelIDForSimilarity();
    if (seedLevel <= 0) return {};

    auto obj = makeSearchObject(SearchType::Similar, std::to_string(seedLevel),
                                "-1", "-1", 0, false, false, false, 0, false, 0);
    return {FeedSource::Similar, obj, "Similar"};
}

FeedQuery RecommendationEngine::buildFavoriteCreator(TasteSnapshot const& taste) {
    if (taste.favoriteCreators.empty()) return {};

    std::uniform_int_distribution<size_t> dist(0, taste.favoriteCreators.size() - 1);
    int creatorID = *std::next(taste.favoriteCreators.begin(), dist(rng()));

    auto obj = makeSearchObject(SearchType::UsersLevels, std::to_string(creatorID),
                                "-1", "-1", 0, false, false, false, 0, false, 0);
    return {FeedSource::FavoriteCreator, obj, "Favourite creator"};
}

FeedQuery RecommendationEngine::buildTopCreator(TasteSnapshot const& taste) {
    if (taste.topCreators.empty()) return {};

    std::uniform_int_distribution<size_t> dist(0, taste.topCreators.size() - 1);
    int creatorID = taste.topCreators[dist(rng())];

    auto obj = makeSearchObject(SearchType::UsersLevels, std::to_string(creatorID),
                                "-1", "-1", 0, false, false, false, 0, false, 0);
    return {FeedSource::TopCreator, obj, "Creator"};
}

FeedQuery RecommendationEngine::buildSongMatch(TasteSnapshot const& taste) {
    if (taste.topSongs.empty()) return {};

    std::uniform_int_distribution<size_t> dist(0, taste.topSongs.size() - 1);
    int songID = taste.topSongs[dist(rng())];
    if (songID <= 0) return {};

    auto obj = makeSearchObject(
        SearchType::Search, "",
        difficultyFilter(taste.preferredDifficulty), "-1", 0,
        false, false, false, songID, true, 0);
    return {FeedSource::SongMatch, obj, "Song"};
}

FeedQuery RecommendationEngine::buildExplore(TasteSnapshot const& taste) {
    // Add an off-profile difficulty band for exploration.
    int bucket = std::clamp(taste.preferredDifficulty / 10, 1, 6);
    std::uniform_int_distribution<int> shiftDist(0, 1);
    int shifted = shiftDist(rng()) ? std::min(6, bucket + 2) : std::max(1, bucket - 2);

    std::uniform_int_distribution<int> pageDist(0, 8);
    auto obj = makeSearchObject(SearchType::Awarded, "",
                                std::to_string(shifted), "-1",
                                pageDist(rng()), false, false, false, 0, false, 0);
    return {FeedSource::Explore, obj, "Explore"};
}

std::vector<FeedQuery> RecommendationEngine::buildNativeQueries(
    TasteSnapshot const& taste, int budget
) {
    if (budget <= 0) return {};

    // Rotate the first strategy so consecutive refreshes differ.
    using Builder = FeedQuery (RecommendationEngine::*)(TasteSnapshot const&);
    std::vector<Builder> builders = {
        &RecommendationEngine::buildDifficultyMatch,
        &RecommendationEngine::buildFeatured,
        &RecommendationEngine::buildTrending,
        &RecommendationEngine::buildSimilar,
        &RecommendationEngine::buildFavoriteCreator,
        &RecommendationEngine::buildTopCreator,
        &RecommendationEngine::buildSongMatch
    };

    std::vector<FeedQuery> queries;
    int start = m_strategyCursor % static_cast<int>(builders.size());
    for (size_t offset = 0; offset < builders.size() && static_cast<int>(queries.size()) < budget; offset++) {
        auto builder = builders[(start + offset) % builders.size()];
        auto query = (this->*builder)(taste);
        // Unsupported profile signals produce an empty query.
        if (query.searchObj) queries.push_back(std::move(query));
    }
    m_strategyCursor = start + 1;

    // Reserve the last slot for exploration.
    if (static_cast<int>(queries.size()) < budget && explorationRate() > 0.f) {
        auto explore = buildExplore(taste);
        if (explore.searchObj) queries.push_back(std::move(explore));
    }

    return queries;
}

void RecommendationEngine::planQueries(std::function<void(std::vector<FeedQuery>)> callback) {
    if (!paimon::modules::isEnabled("paimbnails.foryou.browser")) {
        if (callback) callback({});
        return;
    }

    auto taste = std::make_shared<TasteSnapshot>(TasteProfile::get().snapshot());
    auto shared = std::make_shared<std::function<void(std::vector<FeedQuery>)>>(std::move(callback));
    int budget = queryBudget();

    if (!tagsEnabled() || taste->topTags.empty()) {
        m_lastPlanSummary = "native-only";
        auto queries = buildNativeQueries(*taste, budget);
        if (*shared) (*shared)(std::move(queries));
        return;
    }

    // `include` is an AND: pairs are precise, single tags are broad.
    std::vector<std::vector<std::string>> includeSets;
    std::vector<FeedSource> sources;

    auto const& top = taste->topTags;
    if (top.size() >= 2) {
        includeSets.push_back({top[0], top[1]});
        sources.push_back(FeedSource::TagPair);
    }
    includeSets.push_back({top[0]});
    sources.push_back(FeedSource::TagMatch);
    if (top.size() >= 2) {
        includeSets.push_back({top[1]});
        sources.push_back(FeedSource::TagMatch);
    }

    // Explore an adjacent tag without history to avoid a filter bubble.
    if (explorationRate() > 0.f) {
        auto category = LevelTagsClient::get().categoryOf(top[0]);
        auto neighbours = LevelTagsClient::get().catalogFor(category);
        std::vector<std::string> unknown;
        for (auto const& info : neighbours) {
            if (!taste->tagAffinity.count(info.name)) unknown.push_back(info.name);
        }
        if (!unknown.empty()) {
            std::uniform_int_distribution<size_t> dist(0, unknown.size() - 1);
            includeSets.push_back({unknown[dist(rng())]});
            sources.push_back(FeedSource::TagExplore);
        }
    }

    if (static_cast<int>(includeSets.size()) > kMaxTagQueries) {
        includeSets.resize(kMaxTagQueries);
        sources.resize(kMaxTagQueries);
    }

    std::vector<std::string> exclude;
    for (auto const& tag : taste->avoidedTags) {
        if (exclude.size() >= 2) break;
        exclude.push_back(tag);
    }
    // Explicitly avoided tags are never negotiable.
    for (auto const& [tag, vote] : taste->pinnedTags) {
        if (vote < 0 && std::find(exclude.begin(), exclude.end(), tag) == exclude.end()) {
            exclude.push_back(tag);
        }
    }

    auto pools = std::make_shared<std::vector<std::vector<int>>>(includeSets.size());
    auto pending = std::make_shared<std::atomic<int>>(static_cast<int>(includeSets.size()));
    auto known = std::make_shared<std::unordered_set<int>>(TasteProfile::get().knownLevelIDs());
    auto sourceList = std::make_shared<std::vector<FeedSource>>(std::move(sources));

    for (size_t i = 0; i < includeSets.size(); i++) {
        LevelTagsClient::get().searchByTags(includeSets[i], exclude,
            [this, i, pools, pending, known, sourceList, taste, shared, budget](std::vector<int> ids) {
                // Exclude seen levels and shuffle the remaining tag pool.
                std::erase_if(ids, [&known](int id) { return known->count(id) > 0; });
                std::shuffle(ids.begin(), ids.end(), rng());
                if (ids.size() > kIDsPerQuery) ids.resize(kIDsPerQuery);
                (*pools)[i] = std::move(ids);

                if (pending->fetch_sub(1) != 1) return;

                std::vector<FeedQuery> queries;
                int tagMatched = 0;
                for (size_t p = 0; p < pools->size(); p++) {
                    for (auto& query : buildTagQueries((*pools)[p], (*sourceList)[p])) {
                        tagMatched += static_cast<int>((*pools)[p].size());
                        queries.push_back(std::move(query));
                    }
                }

                // Give unused budget to GD search for levels outside the tag database.
                int remaining = std::max(2, budget - static_cast<int>(queries.size()));
                for (auto& query : buildNativeQueries(*taste, remaining)) {
                    queries.push_back(std::move(query));
                }

                m_lastPlanSummary = fmt::format("{} tag queries ({} levels) + {} native",
                                                pools->size(), tagMatched,
                                                queries.size() - pools->size());
                if (*shared) (*shared)(std::move(queries));
            });
    }
}


RecommendationEngine::Scored RecommendationEngine::scoreLevel(
    GJGameLevel* level, std::vector<std::string> tags, TasteSnapshot const& taste
) const {
    Scored out;
    out.level = level;
    out.tags = std::move(tags);

    if (!level) {
        out.rejected = true;
        return out;
    }

    int levelID = level->m_levelID;
    if (levelID <= 0 || TasteProfile::get().isKnownLevel(levelID)) {
        out.rejected = true;
        return out;
    }

    // Explicitly avoided tags are hard exclusions.
    for (auto const& tag : out.tags) {
        auto pinned = taste.pinnedTags.find(tag);
        if (pinned != taste.pinnedTags.end() && pinned->second < 0) {
            out.rejected = true;
            return out;
        }
    }

    if (taste.starRatedRatio >= 0.99f && level->m_stars <= 0) {
        out.rejected = true;
        return out;
    }

    auto& tagsClient = LevelTagsClient::get();

    float rawTag = 0.f;
    int matched = 0;
    float bestTagContribution = 0.f;
    std::string bestTag;

    for (auto const& tag : out.tags) {
        auto it = taste.tagAffinity.find(tag);
        if (it == taste.tagAffinity.end()) continue;

        float affinity = it->second;
        if (affinity < 0.f) affinity *= kAvoidMultiplier;
        float contribution = categoryWeight(tagsClient.categoryOf(tag)) * affinity;

        rawTag += contribution;
        matched++;
        if (contribution > bestTagContribution) {
            bestTagContribution = contribution;
            bestTag = tag;
        }
    }

    if (out.tags.empty()) {
        // Untagged levels remain playable but cannot beat a tag match.
        out.tagScore = -kUntaggedPenalty;
    } else if (matched > 0) {
        // Normalize by tag count and scale by recognized-tag coverage.
        float coverage = static_cast<float>(matched) / static_cast<float>(out.tags.size());
        out.tagScore = rawTag / std::sqrt(static_cast<float>(out.tags.size()))
                     * (0.55f + 0.45f * coverage);
    }

    int diffBucket = std::clamp(static_cast<int>(level->m_difficulty) / 10, 0,
                                static_cast<int>(kDifficultyBuckets) - 1);
    float difficultyFit = taste.difficultyHistogram[diffBucket];
    // Neighboring difficulties still contribute to the score.
    if (diffBucket > 0) difficultyFit += 0.35f * taste.difficultyHistogram[diffBucket - 1];
    if (diffBucket + 1 < static_cast<int>(kDifficultyBuckets)) {
        difficultyFit += 0.35f * taste.difficultyHistogram[diffBucket + 1];
    }
    if (diffBucket == 6 && taste.preferredDemonDifficulty > 0) {
        difficultyFit *= (level->m_demonDifficulty == taste.preferredDemonDifficulty) ? 1.5f : 0.6f;
    }

    bool platformer = level->isPlatformer();
    int lengthBucket = platformer ? 5 : std::clamp(level->m_levelLength, 0, 4);
    float lengthFit = taste.lengthHistogram[lengthBucket];

    int creatorID = level->m_accountID;
    int songID = level->m_songID;

    float creatorFit = 0.f;
    auto creatorIt = taste.creatorAffinity.find(creatorID);
    if (creatorIt != taste.creatorAffinity.end()) {
        creatorFit = std::clamp(creatorIt->second / 5.f, -1.f, 1.f);
    }

    float songFit = 0.f;
    auto songIt = taste.songAffinity.find(songID);
    if (songIt != taste.songAffinity.end()) {
        songFit = std::clamp(songIt->second / 4.f, -1.f, 1.f);
    }

    float qualityFit = 0.f;
    if (level->m_stars > 0) qualityFit += 0.35f + 0.35f * taste.starRatedRatio;
    else                    qualityFit -= 0.5f * taste.starRatedRatio;
    if (level->m_featured > 0) qualityFit += 0.3f + 0.3f * taste.featuredRatio;
    else                       qualityFit -= 0.25f * taste.featuredRatio;
    if (level->m_isEpic > 0) qualityFit += 0.25f + 0.35f * taste.epicRatio;
    else                     qualityFit -= 0.2f * taste.epicRatio;

    if (platformer) qualityFit += (taste.platformerRatio - 0.5f) * 0.6f;
    else            qualityFit += (0.5f - taste.platformerRatio) * 0.6f;

    // Community likes provide a mild quality prior.
    float popularity = std::min(1.f, std::log10(1.f + static_cast<float>(level->m_likes)) / 5.f);

    out.base = kWeightTag * out.tagScore
             + kWeightDifficulty * difficultyFit
             + kWeightLength * lengthFit
             + kWeightCreator * creatorFit
             + kWeightSong * songFit
             + kWeightQuality * qualityFit
             + kWeightPopularity * popularity;

    // Small noise prevents identical tie ordering across refreshes.
    float jitter = explorationRate();
    if (jitter > 0.f) {
        std::uniform_real_distribution<float> dist(0.f, jitter * 2.f);
        out.base += dist(rng());
    }

    if (bestTagContribution > 0.25f && !bestTag.empty()) {
        out.reasonKey = "foryou.reason_tag";
        out.reasonArg = bestTag;
    } else if (taste.favoriteCreators.count(creatorID)) {
        out.reasonKey = "foryou.reason_fav_creator";
        out.reasonArg = std::string(level->m_creatorName);
    } else if (creatorFit > 0.3f) {
        out.reasonKey = "foryou.reason_creator";
        out.reasonArg = std::string(level->m_creatorName);
    } else if (songFit > 0.3f) {
        out.reasonKey = "foryou.reason_song";
    } else if (difficultyFit > 0.25f) {
        out.reasonKey = "foryou.reason_difficulty";
    } else {
        out.reasonKey = "foryou.reason_discover";
    }

    return out;
}

std::vector<Recommendation> RecommendationEngine::diversify(
    std::vector<Scored>& scored, int limit
) const {
    std::vector<Recommendation> feed;
    if (scored.empty() || limit <= 0) return feed;

    std::sort(scored.begin(), scored.end(), [](Scored const& a, Scored const& b) {
        return a.base > b.base;
    });

    std::vector<bool> taken(scored.size(), false);
    std::vector<size_t> chosen;

    // Penalize similarity to already selected levels for feed diversity.
    while (static_cast<int>(chosen.size()) < limit) {
        size_t bestIndex = scored.size();
        float bestValue = -std::numeric_limits<float>::infinity();

        for (size_t i = 0; i < scored.size(); i++) {
            if (taken[i]) continue;

            float similarity = 0.f;
            for (size_t picked : chosen) {
                float sim = 0.5f * jaccard(scored[i].tags, scored[picked].tags);
                auto* a = scored[i].level.data();
                auto* b = scored[picked].level.data();
                if (a && b) {
                    if (static_cast<int>(a->m_accountID) == static_cast<int>(b->m_accountID)) sim += 0.3f;
                    if (static_cast<int>(a->m_difficulty) == static_cast<int>(b->m_difficulty)) sim += 0.2f;
                }
                similarity = std::max(similarity, sim);
            }

            float value = scored[i].base - kDiversityLambda * similarity;
            if (value > bestValue) {
                bestValue = value;
                bestIndex = i;
            }
        }

        if (bestIndex >= scored.size()) break;
        taken[bestIndex] = true;
        chosen.push_back(bestIndex);

        Recommendation pick;
        pick.level = scored[bestIndex].level;
        pick.score = bestValue;
        pick.tagScore = scored[bestIndex].tagScore;
        pick.tags = scored[bestIndex].tags;
        pick.reasonKey = scored[bestIndex].reasonKey;
        pick.reasonArg = scored[bestIndex].reasonArg;
        feed.push_back(std::move(pick));
    }

    return feed;
}

void RecommendationEngine::rank(
    std::vector<Ref<GJGameLevel>> candidates, int limit,
    std::function<void(std::vector<Recommendation>)> callback
) {
    if (limit <= 0) limit = defaultFeedSize();

    auto shared = std::make_shared<std::function<void(std::vector<Recommendation>)>>(std::move(callback));

    // Featured and Trending often return the same level.
    std::unordered_set<int> seen;
    std::vector<Ref<GJGameLevel>> unique;
    unique.reserve(candidates.size());
    for (auto const& level : candidates) {
        if (!level) continue;
        int id = level->m_levelID;
        if (id <= 0 || !seen.insert(id).second) continue;
        unique.push_back(level);
    }

    if (unique.empty()) {
        if (*shared) (*shared)({});
        return;
    }

    auto pool = std::make_shared<std::vector<Ref<GJGameLevel>>>(std::move(unique));

    auto finish = [this, pool, limit, shared](LevelTagMap const& tagMap) {
        auto taste = TasteProfile::get().snapshot();

        std::vector<Scored> scored;
        scored.reserve(pool->size());
        for (auto const& level : *pool) {
            int id = level->m_levelID;
            std::vector<std::string> tags;
            auto it = tagMap.find(id);
            if (it != tagMap.end()) tags = it->second;

            auto entry = scoreLevel(level, std::move(tags), taste);
            if (!entry.rejected) scored.push_back(std::move(entry));
        }

        auto feed = diversify(scored, limit);
        log::info("[ForYou] Ranked {} of {} candidates ({})",
                  feed.size(), pool->size(), m_lastPlanSummary);
        if (*shared) (*shared)(std::move(feed));
    };

    if (!tagsEnabled()) {
        finish({});
        return;
    }

    std::vector<int> ids;
    ids.reserve(pool->size());
    for (auto const& level : *pool) ids.push_back(level->m_levelID);

    // One batched request covers the feed; cached IDs cost nothing.
    LevelTagsClient::get().fetchTags(ids, [finish](LevelTagMap tagMap) {
        finish(tagMap);
    });
}

}
