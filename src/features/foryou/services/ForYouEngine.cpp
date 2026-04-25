#include "ForYouEngine.hpp"
#include "ForYouTracker.hpp"
#include "LevelTagsIntegration.hpp"
#include "../../../framework/compat/ModCompat.hpp"
#include <Geode/binding/GJSearchObject.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <random>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace {
    // GD difficulty enum (10=Easy..60=Demon) → search filter (1..6)
    int mapDifficulty(int gdDiff) {
        switch (gdDiff) {
            case 10: return 1;
            case 20: return 2;
            case 30: return 3;
            case 40: return 4;
            case 50: return 5;
            case 60: return 6;
            default: return -1;
        }
    }

    int mapDemonFilter(int demonDiff) {
        switch (demonDiff) {
            case 1: return static_cast<int>(GJDifficulty::DemonEasy);
            case 2: return static_cast<int>(GJDifficulty::DemonMedium);
            case 3: return static_cast<int>(GJDifficulty::Demon);
            case 4: return static_cast<int>(GJDifficulty::DemonInsane);
            case 5: return static_cast<int>(GJDifficulty::DemonExtreme);
            default: return 0;
        }
    }

    std::string difficultyFilterFor(paimon::foryou::UserProfile const& profile) {
        int diff = mapDifficulty(profile.preferredDifficulty);
        return diff > 0 ? std::to_string(diff) : "-1";
    }

    std::string lengthFilterFor(paimon::foryou::UserProfile const& profile) {
        if (profile.preferredLength < 0 || profile.preferredLength > 4) {
            return "-1";
        }

        bool platformerOnly = profile.platformerRatio >= 0.99f;
        return std::to_string(GJGameLevel::getLengthKey(profile.preferredLength, platformerOnly));
    }

    bool wantsStarOnly(paimon::foryou::UserProfile const& profile) {
        return profile.starRatedRatio >= 0.99f;
    }

    bool wantsFeaturedOnly(paimon::foryou::UserProfile const& profile) {
        return profile.featuredRatio >= 0.7f;
    }

    bool wantsEpicOnly(paimon::foryou::UserProfile const& profile) {
        return profile.epicRatio >= 0.4f;
    }

    int demonFilterFor(paimon::foryou::UserProfile const& profile) {
        if (profile.preferredDifficulty != 60) {
            return 0;
        }
        return mapDemonFilter(profile.preferredDemonDifficulty);
    }

    // Helper que evita el overload de 22 parametros de GJSearchObject::create,
    // el cual tiene un binding potencialmente incorrecto que corrompe los campos
    // gd::string y causa bad_alloc en getKey().
    GJSearchObject* makeSearchObject(
        SearchType type,
        gd::string query,
        gd::string difficulty,
        gd::string length,
        int page,
        bool star,
        bool uncompleted,
        bool featured,
        int songID,
        bool original,
        bool twoPlayer,
        bool customSong,
        bool songFilter,
        bool noStar,
        bool coins,
        bool epic,
        bool legendary,
        bool mythic,
        bool onlyCompleted,
        int demonFilter,
        int folder,
        int searchMode
    ) {
        auto obj = GJSearchObject::create(type, query);
        if (!obj) return nullptr;
        obj->m_difficulty = difficulty;
        obj->m_length = length;
        obj->m_page = page;
        obj->m_starFilter = star;
        obj->m_uncompletedFilter = uncompleted;
        obj->m_featuredFilter = featured;
        obj->m_songID = songID;
        obj->m_originalFilter = original;
        obj->m_twoPlayerFilter = twoPlayer;
        obj->m_customSongFilter = customSong;
        obj->m_songFilter = songFilter;
        obj->m_noStarFilter = noStar;
        obj->m_coinsFilter = coins;
        obj->m_epicFilter = epic;
        obj->m_legendaryFilter = legendary;
        obj->m_mythicFilter = mythic;
        obj->m_completedFilter = onlyCompleted;
        obj->m_demonFilter = static_cast<GJDifficulty>(demonFilter);
        obj->m_folder = folder;
        obj->m_searchMode = searchMode;
        return obj;
    }
}

namespace paimon::foryou {

ForYouEngine& ForYouEngine::get() {
    static ForYouEngine instance;
    return instance;
}

int ForYouEngine::randomPage() const {
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 4);
    return dist(rng);
}

std::vector<ForYouQuery> ForYouEngine::generateQueries(int count) {
    auto& tracker = ForYouTracker::get();
    auto const& profile = tracker.getProfile();

    std::vector<QueryStrategy> allStrategies = {
        QueryStrategy::DifficultyMatch,
        QueryStrategy::FeaturedDiscovery,
        QueryStrategy::TrendingAtDifficulty,
        QueryStrategy::CreatorFollowUp,
        QueryStrategy::SimilarLevels,
        QueryStrategy::SongMatch
    };

    if (paimon::compat::ModCompat::isLevelTagsLoaded() && !profile.preferredTags.empty()) {
        allStrategies.push_back(QueryStrategy::TagBasedDiscovery);
    }

    if (!profile.favoriteCreators.empty()) {
        allStrategies.push_back(QueryStrategy::FavoriteCreatorLevels);
    }

    std::vector<ForYouQuery> queries;

    // rotate start
    int start = m_lastStrategyIndex % static_cast<int>(allStrategies.size());

    for (int i = 0; i < count && i < static_cast<int>(allStrategies.size()); i++) {
        int idx = (start + i) % static_cast<int>(allStrategies.size());
        auto strategy = allStrategies[idx];

        ForYouQuery q;
        switch (strategy) {
            case QueryStrategy::DifficultyMatch:
                q = buildDifficultyMatch(); break;
            case QueryStrategy::FeaturedDiscovery:
                q = buildFeaturedDiscovery(); break;
            case QueryStrategy::TrendingAtDifficulty:
                q = buildTrendingAtDifficulty(); break;
            case QueryStrategy::CreatorFollowUp:
                q = buildCreatorFollowUp(); break;
            case QueryStrategy::SimilarLevels:
                q = buildSimilarLevels(); break;
            case QueryStrategy::SongMatch:
                q = buildSongMatch(); break;
            case QueryStrategy::TagBasedDiscovery:
                q = buildTagBasedDiscovery(); break;
            case QueryStrategy::FavoriteCreatorLevels:
                q = buildFavoriteCreatorLevels(); break;
        }

        if (q.searchObj) {
            queries.push_back(q);
        }
    }

    // ensure at least 2 different strategies
    if (queries.size() >= 2) {
        bool allSame = true;
        auto firstStrategy = queries[0].strategy;
        for (size_t i = 1; i < queries.size(); i++) {
            if (queries[i].strategy != firstStrategy) {
                allSame = false;
                break;
            }
        }
        if (allSame) {
            int altIdx = (start + count) % static_cast<int>(allStrategies.size());
            auto altStrategy = allStrategies[altIdx];
            ForYouQuery altQ;
            switch (altStrategy) {
                case QueryStrategy::FeaturedDiscovery:
                    altQ = buildFeaturedDiscovery(); break;
                case QueryStrategy::TrendingAtDifficulty:
                    altQ = buildTrendingAtDifficulty(); break;
                default:
                    altQ = buildFeaturedDiscovery(); break;
            }
            if (altQ.searchObj && queries.size() > 1) {
                queries[1] = altQ;
            }
        }
    }

    m_lastStrategyIndex = (start + count) % static_cast<int>(allStrategies.size());

    return queries;
}

// ── Strategy builders ───────────────────────────────────

ForYouQuery ForYouEngine::buildDifficultyMatch() {
    auto const& profile = ForYouTracker::get().getProfile();
    auto diffStr = difficultyFilterFor(profile);
    auto lenStr = lengthFilterFor(profile);

    auto searchObj = makeSearchObject(
        SearchType::Awarded,
        "", diffStr, lenStr,
        randomPage(),
        wantsStarOnly(profile),
        false, wantsFeaturedOnly(profile),
        0, false, false, false, false, false, false,
        wantsEpicOnly(profile), false, false, false,
        demonFilterFor(profile), 0, 0
    );

    return {QueryStrategy::DifficultyMatch, searchObj, "Difficulty Match"};
}

ForYouQuery ForYouEngine::buildFeaturedDiscovery() {
    auto const& profile = ForYouTracker::get().getProfile();

    auto searchObj = makeSearchObject(
        SearchType::Featured,
        "",
        difficultyFilterFor(profile),
        lengthFilterFor(profile),
        randomPage(),
        wantsStarOnly(profile),
        false, false,
        0, false, false, false, false, false, false,
        wantsEpicOnly(profile), false, false, false,
        demonFilterFor(profile), 0, 0
    );

    return {QueryStrategy::FeaturedDiscovery, searchObj, "Featured"};
}

ForYouQuery ForYouEngine::buildTrendingAtDifficulty() {
    auto const& profile = ForYouTracker::get().getProfile();

    auto searchObj = makeSearchObject(
        SearchType::Trending,
        "",
        difficultyFilterFor(profile),
        lengthFilterFor(profile),
        randomPage(),
        wantsStarOnly(profile), false, wantsFeaturedOnly(profile),
        0, false, false, false, false, false, false,
        wantsEpicOnly(profile), false, false, false,
        demonFilterFor(profile), 0, 0
    );

    return {QueryStrategy::TrendingAtDifficulty, searchObj, "Trending"};
}

ForYouQuery ForYouEngine::buildCreatorFollowUp() {
    auto const& profile = ForYouTracker::get().getProfile();

    if (profile.preferredCreators.empty()) return {};

    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(0, profile.preferredCreators.size() - 1);
    int creatorID = profile.preferredCreators[dist(rng)];

    auto searchObj = makeSearchObject(
        SearchType::UsersLevels,
        std::to_string(creatorID),
        difficultyFilterFor(profile), lengthFilterFor(profile),
        0,
        wantsStarOnly(profile), false, wantsFeaturedOnly(profile),
        0, false, false, false, false, false, false,
        wantsEpicOnly(profile), false, false, false,
        demonFilterFor(profile), 0, 0
    );

    return {QueryStrategy::CreatorFollowUp, searchObj, "Creator"};
}

ForYouQuery ForYouEngine::buildSimilarLevels() {
    auto const& levels = ForYouTracker::get().getLevels();

    // best completed level, fallback to most-played overall
    int bestID = 0, bestPlay = 0;
    bool bestCompleted = false;
    for (auto& [id, rec] : levels) {
        if (rec.playCount <= 0) continue;
        bool better = (rec.completed && !bestCompleted)
            || (rec.completed == bestCompleted && rec.playCount > bestPlay);
        if (better) {
            bestID = id;
            bestPlay = rec.playCount;
            bestCompleted = rec.completed;
        }
    }

    if (bestID == 0) return {};

    auto searchObj = makeSearchObject(
        SearchType::Similar,
        std::to_string(bestID),
        difficultyFilterFor(ForYouTracker::get().getProfile()), lengthFilterFor(ForYouTracker::get().getProfile()),
        0,
        wantsStarOnly(ForYouTracker::get().getProfile()), false, wantsFeaturedOnly(ForYouTracker::get().getProfile()),
        0, false, false, false, false, false, false,
        wantsEpicOnly(ForYouTracker::get().getProfile()), false, false, false,
        demonFilterFor(ForYouTracker::get().getProfile()), 0, 0
    );

    return {QueryStrategy::SimilarLevels, searchObj, "Similar"};
}

ForYouQuery ForYouEngine::buildSongMatch() {
    auto const& profile = ForYouTracker::get().getProfile();

    if (profile.preferredSongs.empty()) return {};

    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(0, profile.preferredSongs.size() - 1);
    int songID = profile.preferredSongs[dist(rng)];

    auto searchObj = makeSearchObject(
        SearchType::Search,
        "",
        difficultyFilterFor(profile), lengthFilterFor(profile),
        randomPage(),
        wantsStarOnly(profile), false, wantsFeaturedOnly(profile),
        songID,
        false, false,
        true,                       // customSong
        true,                       // songFilter
        false, false,
        wantsEpicOnly(profile), false, false, false,
        demonFilterFor(profile), 0, 0
    );

    return {QueryStrategy::SongMatch, searchObj, "Song Match"};
}

ForYouQuery ForYouEngine::buildTagBasedDiscovery() {
    auto const& profile = ForYouTracker::get().getProfile();

    if (profile.preferredTags.empty()) return {};

    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(0, profile.preferredTags.size() - 1);
    auto tag = profile.preferredTags[dist(rng)];

    // Truncate tag to prevent "string too long" crash in GJSearchObject::getKey()
    if (tag.size() > 50) tag.resize(50);

    auto searchObj = makeSearchObject(
        SearchType::Search,
        tag,
        difficultyFilterFor(profile), lengthFilterFor(profile),
        randomPage(),
        wantsStarOnly(profile),
        false, wantsFeaturedOnly(profile),
        0, false, false, false, false, false, false,
        wantsEpicOnly(profile), false, false, false,
        demonFilterFor(profile), 0, 0
    );

    return {QueryStrategy::TagBasedDiscovery, searchObj, "Tag: " + tag};
}

ForYouQuery ForYouEngine::buildFavoriteCreatorLevels() {
    auto const& profile = ForYouTracker::get().getProfile();

    if (profile.favoriteCreators.empty()) return {};

    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(0, profile.favoriteCreators.size() - 1);
    int creatorID = *std::next(profile.favoriteCreators.begin(), dist(rng));

    auto searchObj = makeSearchObject(
        SearchType::UsersLevels,
        std::to_string(creatorID),
        difficultyFilterFor(profile), lengthFilterFor(profile),
        0,
        wantsStarOnly(profile), false, wantsFeaturedOnly(profile),
        0, false, false, false, false, false, false,
        wantsEpicOnly(profile), false, false, false,
        demonFilterFor(profile), 0, 0
    );

    return {QueryStrategy::FavoriteCreatorLevels, searchObj, "Fav Creator"};
}

// ── Scoring ─────────────────────────────────────────────

float ForYouEngine::scoreLevelForUser(GJGameLevel* level) const {
    if (!level) return 0.f;

    auto& tracker = ForYouTracker::get();
    auto const& profile = tracker.getProfile();
    bool strictSeededOnly = tracker.isSeeded() && profile.totalLevelsPlayed == 0;

    // Already played → exclude
    if (tracker.isLevelTracked(level->m_levelID)) return -1.f;

    float score = 1.0f;

    int levelDiff = static_cast<int>(level->m_difficulty);
    int diffIdx = levelDiff / 10; // 0=NA, 1=Easy..5=Insane, 6=Demon
    bool isPlatformer = level->isPlatformer();
    int lengthValue = static_cast<int>(level->m_levelLength);
    int lenIdx = isPlatformer ? 5 : std::clamp(lengthValue, 0, 4);

    if (strictSeededOnly) {
        if (profile.preferredDifficulty > 0 && levelDiff != profile.preferredDifficulty) {
            return -1.f;
        }
        if (profile.preferredDemonDifficulty > 0 && levelDiff == 60) {
            int levelDemon = static_cast<int>(level->m_demonDifficulty);
            if (levelDemon != profile.preferredDemonDifficulty) {
                return -1.f;
            }
        }
        if (profile.platformerRatio >= 0.99f && !isPlatformer) {
            return -1.f;
        }
        if (profile.platformerRatio <= 0.01f && isPlatformer) {
            return -1.f;
        }
        if (!isPlatformer && profile.preferredLength >= 0 && profile.preferredLength <= 4 && lengthValue != profile.preferredLength) {
            return -1.f;
        }
    }

    if (diffIdx >= 0 && diffIdx < 7) {
        score += profile.difficultyHistogram[diffIdx] * 2.5f;
    }

    if (lenIdx >= 0 && lenIdx < 6) {
        score += profile.lengthHistogram[lenIdx] * 1.0f;
    }

    if (isPlatformer && profile.platformerRatio > 0.3f) {
        score += profile.platformerRatio * 1.5f;
    } else if (!isPlatformer && profile.platformerRatio < 0.7f) {
        score += (1.f - profile.platformerRatio) * 1.5f;
    }

    // Strict filters from user preferences
    if (profile.starRatedRatio >= 0.99f && level->m_stars <= 0) {
        return -1.f; // user wants star-rated only
    }
    if (level->m_stars > 0 && profile.starRatedRatio > 0.5f) {
        score += 0.5f;
    }
    if (level->m_featured > 0) {
        score += 0.5f + profile.featuredRatio * 0.5f;
    } else if (profile.featuredRatio >= 0.7f) {
        score -= 0.3f; // penalize non-featured if user strongly prefers featured
    }
    if (level->m_isEpic > 0) {
        score += 0.3f + profile.epicRatio * 0.3f;
    } else if (profile.epicRatio >= 0.4f) {
        score -= 0.2f; // slight penalty for non-epic if user prefers epic
    }

    // Demon difficulty preference
    if (levelDiff == 60 && profile.preferredDemonDifficulty > 0) {
        int levelDemon = static_cast<int>(level->m_demonDifficulty);
        if (levelDemon == profile.preferredDemonDifficulty) {
            score += 2.0f;
        } else {
            score += 0.5f; // still a demon
        }
    }

    // favorite creator = strongest signal
    if (profile.favoriteCreators.count(level->m_accountID)) {
        score += 5.0f;
    } else {
        for (int creatorID : profile.preferredCreators) {
            if (level->m_accountID == creatorID) {
                score += 3.0f;
                break;
            }
        }
    }

    for (int songID : profile.preferredSongs) {
        if (level->m_songID == songID) {
            score += 2.0f;
            break;
        }
    }

    if (paimon::compat::ModCompat::isLevelTagsLoaded()) {
        auto tags = LevelTagsIntegration::get().getCachedTags(level->m_levelID);
        for (auto const& tag : tags) {
            if (profile.tagFrequency.count(tag)) {
                score += 2.0f;
            }
        }
    }

    score += 1.0f; // freshness boost

    return score;
}

void ForYouEngine::scoreAndSortResults(std::vector<Ref<GJGameLevel>>& results) {
    std::vector<std::pair<float, size_t>> scored;
    scored.reserve(results.size());

    for (size_t i = 0; i < results.size(); i++) {
        float s = scoreLevelForUser(results[i]);
        if (s >= 0.f) {
            scored.push_back({s, i});
        }
    }

    std::sort(scored.begin(), scored.end(), [](auto& a, auto& b) {
        return a.first > b.first;
    });

    // penalize consecutive same-creator / same-difficulty
    std::vector<std::pair<float, size_t>> diversified;
    diversified.reserve(scored.size());
    int lastCreatorID = -1;
    int lastDifficulty = -1;

    for (auto& [s, idx] : scored) {
        float adjustedScore = s;
        auto* lvl = results[idx].data();
        if (lvl) {
            int creatorID = lvl->m_accountID;
            int diff = static_cast<int>(lvl->m_difficulty);
            if (creatorID == lastCreatorID && lastCreatorID > 0) {
                adjustedScore *= 0.7f; // penalize same-creator consecutive
            }
            if (diff == lastDifficulty) {
                adjustedScore *= 0.85f; // penalize same-difficulty consecutive
            }
            lastCreatorID = creatorID;
            lastDifficulty = diff;
        }
        diversified.push_back({adjustedScore, idx});
    }

    std::sort(diversified.begin(), diversified.end(), [](auto& a, auto& b) {
        return a.first > b.first;
    });

    std::vector<Ref<GJGameLevel>> sorted;
    sorted.reserve(diversified.size());
    for (auto& [score, idx] : diversified) {
        sorted.push_back(results[idx]);
    }
    results = std::move(sorted);
}

} // namespace paimon::foryou
