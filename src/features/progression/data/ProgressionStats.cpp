#include "ProgressionStats.hpp"

#include <Geode/binding/GJUserScore.hpp>
#include <Geode/binding/GameStatsManager.hpp>
#include <charconv>
#include <cmath>
#include <vector>

namespace paimon::progression {

namespace {

std::vector<int> splitInts(std::string const& raw) {
    std::vector<int> out;
    size_t start = 0;
    while (start <= raw.size()) {
        size_t const comma = raw.find(',', start);
        size_t const end = comma == std::string::npos ? raw.size() : comma;
        int value = 0;
        std::from_chars(raw.data() + start, raw.data() + end, value);
        out.push_back(value);
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return out;
}

int at(std::vector<int> const& values, size_t index) {
    return index < values.size() ? values[index] : 0;
}

int64_t masteryOf(DifficultyBreakdown const& d) {
    using namespace exp_values;
    return static_cast<int64_t>(d.autos)    * kMasteryAuto
         + static_cast<int64_t>(d.easy)     * kMasteryEasy
         + static_cast<int64_t>(d.normal)   * kMasteryNormal
         + static_cast<int64_t>(d.hard)     * kMasteryHard
         + static_cast<int64_t>(d.harder)   * kMasteryHarder
         + static_cast<int64_t>(d.insane)   * kMasteryInsane
         + static_cast<int64_t>(d.daily)    * kMasteryDaily
         + static_cast<int64_t>(d.gauntlet) * kMasteryGauntlet
         + static_cast<int64_t>(d.mapPacks) * kMasteryMapPack;
}

} // namespace

int DemonBreakdown::counted() const {
    return easyClassic + mediumClassic + hardClassic + insaneClassic + extremeClassic
         + easyPlatformer + mediumPlatformer + hardPlatformer + insanePlatformer + extremePlatformer;
}

int DemonBreakdown::platformer() const {
    return easyPlatformer + mediumPlatformer + hardPlatformer + insanePlatformer + extremePlatformer;
}

int DifficultyBreakdown::counted() const {
    return autos + easy + normal + hard + harder + insane;
}

DemonBreakdown parseDemonInfo(std::string const& raw, bool* ok) {
    DemonBreakdown out;
    auto const values = splitInts(raw);
    if (ok) *ok = values.size() >= 5;
    out.easyClassic        = at(values, 0);
    out.mediumClassic      = at(values, 1);
    out.hardClassic        = at(values, 2);
    out.insaneClassic      = at(values, 3);
    out.extremeClassic     = at(values, 4);
    out.easyPlatformer     = at(values, 5);
    out.mediumPlatformer   = at(values, 6);
    out.hardPlatformer     = at(values, 7);
    out.insanePlatformer   = at(values, 8);
    out.extremePlatformer  = at(values, 9);
    out.weekly             = at(values, 10);
    out.gauntlet           = at(values, 11);
    return out;
}

DifficultyBreakdown parseDifficultyInfo(std::string const& raw, bool* ok) {
    DifficultyBreakdown out;
    auto const values = splitInts(raw);
    if (ok) *ok = values.size() >= 6;
    out.autos    = at(values, 0);
    out.easy     = at(values, 1);
    out.normal   = at(values, 2);
    out.hard     = at(values, 3);
    out.harder   = at(values, 4);
    out.insane   = at(values, 5);
    out.daily    = at(values, 6);
    out.gauntlet = at(values, 7);
    out.mapPacks = at(values, 8);
    return out;
}

PlayerStats statsFromScore(GJUserScore* score) {
    PlayerStats stats;
    if (!score) return stats;

    stats.stars         = score->m_stars;
    stats.moons         = score->m_moons;
    stats.diamonds      = score->m_diamonds;
    stats.secretCoins   = score->m_secretCoins;
    stats.userCoins     = score->m_userCoins;
    stats.demons        = score->m_demons;
    stats.creatorPoints = score->m_creatorPoints;
    stats.globalRank    = score->m_globalRank;

    stats.demonInfo      = parseDemonInfo(std::string(score->m_demonInfo), &stats.hasDemonInfo);
    stats.classicInfo    = parseDifficultyInfo(std::string(score->m_starsInfo), &stats.hasClassicInfo);
    stats.platformerInfo = parseDifficultyInfo(std::string(score->m_platformerInfo), &stats.hasPlatformerInfo);

    reconcileDemons(stats);
    return stats;
}

void reconcileDemons(PlayerStats& stats) {
    // Zeroed breakdown next to a real count: field never arrived, use fallback.
    if (stats.demonInfo.counted() == 0) {
        stats.hasDemonInfo = false;
    }

    // Breakdown bigger than the count is bogus; drop it whole.
    if (stats.demonInfo.counted() > stats.demons) {
        stats.demonInfo = {};
        stats.hasDemonInfo = false;
    }
}

PlayerStats statsFromLocalSave() {
    PlayerStats stats;
    auto* gsm = GameStatsManager::sharedState();
    if (!gsm) return stats;

    stats.stars       = gsm->getStat("6");
    stats.moons       = gsm->getStat("28");
    stats.diamonds    = gsm->getStat("13");
    stats.demons      = gsm->getStat("5");
    stats.userCoins   = gsm->getStat("12");
    stats.secretCoins = gsm->getStat("8");
    return stats;
}

ExpReport computeExp(PlayerStats const& stats) {
    using namespace exp_values;

    ExpReport report;
    auto set = [&](int index, ExpSource source, int64_t exp, int64_t count) {
        report.entries[index] = {source, exp, count};
        report.total += exp;
    };

    set(0, ExpSource::Stars, static_cast<int64_t>(stats.stars) * kStar, stats.stars);
    set(1, ExpSource::Moons, static_cast<int64_t>(stats.moons) * kMoon, stats.moons);
    set(2, ExpSource::Diamonds, static_cast<int64_t>(stats.diamonds) * kDiamond, stats.diamonds);
    set(3, ExpSource::UserCoins, static_cast<int64_t>(stats.userCoins) * kUserCoin, stats.userCoins);
    set(4, ExpSource::SecretCoins, static_cast<int64_t>(stats.secretCoins) * kSecretCoin, stats.secretCoins);

    int64_t demonExp = 0;
    if (stats.hasDemonInfo) {
        auto const& d = stats.demonInfo;
        demonExp = static_cast<int64_t>(d.easyClassic)       * kDemonEasy
                 + static_cast<int64_t>(d.mediumClassic)     * kDemonMedium
                 + static_cast<int64_t>(d.hardClassic)       * kDemonHard
                 + static_cast<int64_t>(d.insaneClassic)     * kDemonInsane
                 + static_cast<int64_t>(d.extremeClassic)    * kDemonExtreme
                 + static_cast<int64_t>(d.easyPlatformer)    * kDemonEasyPlat
                 + static_cast<int64_t>(d.mediumPlatformer)  * kDemonMediumPlat
                 + static_cast<int64_t>(d.hardPlatformer)    * kDemonHardPlat
                 + static_cast<int64_t>(d.insanePlatformer)  * kDemonInsanePlat
                 + static_cast<int64_t>(d.extremePlatformer) * kDemonExtremePlat
                 + static_cast<int64_t>(d.weekly)            * kDemonWeeklyBonus
                 + static_cast<int64_t>(d.gauntlet)          * kDemonGauntletBonus;

        // Breakdown can trail the total right after a completion syncs.
        int const missing = stats.demons - stats.demonInfo.counted();
        if (missing > 0) demonExp += static_cast<int64_t>(missing) * kDemonFallback;
    } else {
        demonExp = static_cast<int64_t>(stats.demons) * kDemonFallback;
    }
    set(5, ExpSource::Demons, demonExp, stats.demons);

    set(6, ExpSource::CreatorPoints,
        static_cast<int64_t>(stats.creatorPoints) * kCreatorPoint, stats.creatorPoints);

    int64_t const mastery = masteryOf(stats.classicInfo) + masteryOf(stats.platformerInfo);
    int64_t const masteryCount = stats.classicInfo.counted() + stats.platformerInfo.counted();
    set(7, ExpSource::Mastery, mastery, masteryCount);

    // Only source the game doesn't publish; zero without a queued duel.
    set(8, ExpSource::Versus, stats.versusExp, stats.versusWins);

    return report;
}

std::string formatCount(int64_t value) {
    bool const negative = value < 0;
    std::string digits = std::to_string(negative ? -value : value);

    std::string out;
    out.reserve(digits.size() + digits.size() / 3 + 1);
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out.push_back(',');
        out.push_back(digits[i]);
    }
    if (negative) out.insert(out.begin(), '-');
    return out;
}

std::string shortCount(int64_t value) {
    auto trim = [](double scaled, char suffix) {
        // One decimal only while it fits in three characters.
        if (scaled < 10.0 && std::fabs(scaled - std::round(scaled)) > 0.05) {
            return fmt::format("{:.1f}{}", scaled, suffix);
        }
        return fmt::format("{}{}", static_cast<int64_t>(std::llround(scaled)), suffix);
    };

    if (value >= 1000000) return trim(value / 1000000.0, 'M');
    if (value >= 1000) return trim(value / 1000.0, 'K');
    return std::to_string(value);
}

char const* sourceId(ExpSource source) {
    switch (source) {
        case ExpSource::Stars:         return "stars";
        case ExpSource::Moons:         return "moons";
        case ExpSource::Diamonds:      return "diamonds";
        case ExpSource::UserCoins:     return "user-coins";
        case ExpSource::SecretCoins:   return "secret-coins";
        case ExpSource::Demons:        return "demons";
        case ExpSource::CreatorPoints: return "creator-points";
        case ExpSource::Mastery:       return "mastery";
        case ExpSource::Versus:        return "versus";
    }
    return "stars";
}

char const* sourceIconFrame(ExpSource source) {
    switch (source) {
        case ExpSource::Stars:         return "GJ_starsIcon_001.png";
        case ExpSource::Moons:         return "GJ_moonsIcon_001.png";
        case ExpSource::Diamonds:      return "GJ_diamondsIcon_001.png";
        case ExpSource::UserCoins:     return "GJ_coinsIcon2_001.png";
        case ExpSource::SecretCoins:   return "GJ_coinsIcon_001.png";
        case ExpSource::Demons:        return "GJ_demonIcon_001.png";
        case ExpSource::CreatorPoints: return "GJ_pointsIcon_001.png";
        case ExpSource::Mastery:       return "GJ_completesIcon_001.png";
        case ExpSource::Versus:        return "GJ_starBtn_001.png";
    }
    return "GJ_starsIcon_001.png";
}

} // namespace paimon::progression
