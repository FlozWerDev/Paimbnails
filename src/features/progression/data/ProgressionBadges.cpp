#include "ProgressionBadges.hpp"
#include "ProgressionTiers.hpp"
#include "../../../utils/Localization.hpp"

#include <algorithm>

namespace paimon::progression {

namespace {

using M = BadgeMetric;
using R = BadgeRarity;

constexpr char const* kStar     = "GJ_starsIcon_001.png";
constexpr char const* kMoon     = "GJ_moonsIcon_001.png";
constexpr char const* kDiamond  = "GJ_diamondsIcon_001.png";
constexpr char const* kCoin     = "GJ_coinsIcon_001.png";
constexpr char const* kUserCoin = "GJ_coinsIcon2_001.png";
constexpr char const* kDemon    = "GJ_demonIcon_001.png";
// diffIcon_* is the list-cell face without the difficulty caption baked in; the
// caption turns to mush at tile size and the faces alone already differ.
constexpr char const* kEasyD    = "diffIcon_07_btn_001.png";
constexpr char const* kMedD     = "diffIcon_08_btn_001.png";
constexpr char const* kHardD    = "diffIcon_06_btn_001.png";
constexpr char const* kInsD     = "diffIcon_09_btn_001.png";
constexpr char const* kExtD     = "diffIcon_10_btn_001.png";
constexpr char const* kPlatD    = "diffIcon_10_btn_001.png";
constexpr char const* kWeekly   = "gj_eventCrown_001.png";
constexpr char const* kGauntlet = "GJ_gauntletsBtn_001.png";
constexpr char const* kPoints   = "GJ_pointsIcon_001.png";
constexpr char const* kMapPack  = "GJ_mapPacksBtn_001.png";
constexpr char const* kDaily    = "gj_dailyCrown_001.png";
constexpr char const* kInsane   = "diffIcon_05_btn_001.png";
constexpr char const* kHarder   = "diffIcon_04_btn_001.png";
constexpr char const* kBigStar  = "GJ_bigStar_001.png";
constexpr char const* kMagic    = "GJ_sMagicIcon_001.png";

std::vector<BadgeDef> buildBadges() {
    return {
        {"star-100",      "stars", "Stargazer",          kStar, M::Stars,   100,   R::Common},
        {"star-500",      "stars", "Star Collector",     kStar, M::Stars,   500,   R::Common},
        {"star-1k",       "stars", "Thousand Lights",    kStar, M::Stars,   1000,  R::Uncommon},
        {"star-2500",     "stars", "Constellation",      kStar, M::Stars,   2500,  R::Uncommon},
        {"star-5k",       "stars", "Star Hoarder",       kStar, M::Stars,   5000,  R::Rare},
        {"star-10k",      "stars", "Galactic",           kStar, M::Stars,   10000, R::Epic},
        {"star-20k",      "stars", "Supernova",          kStar, M::Stars,   20000, R::Legendary},
        {"star-35k",      "stars", "Cosmic Sovereign",   kStar, M::Stars,   35000, R::Mythic},

        {"moon-50",       "moons", "Moonlit",            kMoon, M::Moons,   50,    R::Common},
        {"moon-250",      "moons", "Lunar Walker",       kMoon, M::Moons,   250,   R::Common},
        {"moon-500",      "moons", "Crescent",           kMoon, M::Moons,   500,   R::Uncommon},
        {"moon-1k",       "moons", "Selenophile",        kMoon, M::Moons,   1000,  R::Rare},
        {"moon-2500",     "moons", "Tidal Force",        kMoon, M::Moons,   2500,  R::Epic},
        {"moon-5k",       "moons", "Eclipse Bringer",    kMoon, M::Moons,   5000,  R::Legendary},
        {"moon-10k",      "moons", "Lord of Moons",      kMoon, M::Moons,   10000, R::Mythic},

        {"dia-100",       "diamonds", "Prospector",      kDiamond, M::Diamonds, 100,   R::Common},
        {"dia-500",       "diamonds", "Gem Cutter",      kDiamond, M::Diamonds, 500,   R::Common},
        {"dia-2k",        "diamonds", "Jeweler",         kDiamond, M::Diamonds, 2000,  R::Uncommon},
        {"dia-5k",        "diamonds", "Treasury",        kDiamond, M::Diamonds, 5000,  R::Rare},
        {"dia-15k",       "diamonds", "Vault Keeper",    kDiamond, M::Diamonds, 15000, R::Epic},
        {"dia-30k",       "diamonds", "Diamond Baron",   kDiamond, M::Diamonds, 30000, R::Legendary},
        {"dia-65k",       "diamonds", "Kingdom of Glass",kDiamond, M::Diamonds, 65000, R::Mythic},

        {"scoin-10",      "coins", "Coin Hunter",        kCoin, M::SecretCoins, 10,  R::Common},
        {"scoin-30",      "coins", "Cave Diver",         kCoin, M::SecretCoins, 30,  R::Uncommon},
        {"scoin-60",      "coins", "Secret Seeker",      kCoin, M::SecretCoins, 60,  R::Rare},
        {"scoin-100",     "coins", "Hundred Golds",      kCoin, M::SecretCoins, 100, R::Epic},
        {"scoin-149",     "coins", "Nothing Left Hidden",kCoin, M::SecretCoins, 149, R::Legendary},

        {"ucoin-50",      "coins", "Silver Touch",       kUserCoin, M::UserCoins, 50,    R::Common},
        {"ucoin-250",     "coins", "Coin Magnet",        kUserCoin, M::UserCoins, 250,   R::Common},
        {"ucoin-500",     "coins", "Silver Rush",        kUserCoin, M::UserCoins, 500,   R::Uncommon},
        {"ucoin-1k",      "coins", "Vaultbreaker",       kUserCoin, M::UserCoins, 1000,  R::Rare},
        {"ucoin-3k",      "coins", "Silver Tide",        kUserCoin, M::UserCoins, 3000,  R::Epic},
        {"ucoin-6k",      "coins", "Mint Master",        kUserCoin, M::UserCoins, 6000,  R::Legendary},
        {"ucoin-10k",     "coins", "Coin Singularity",   kUserCoin, M::UserCoins, 10000, R::Mythic},

        {"demon-10",      "demons", "Demon Curious",     kDemon, M::Demons, 10,   R::Common},
        {"demon-50",      "demons", "Demon Hunter",      kDemon, M::Demons, 50,   R::Common},
        {"demon-100",     "demons", "A Century Below",   kDemon, M::Demons, 100,  R::Uncommon},
        {"demon-250",     "demons", "Demon Slayer",      kDemon, M::Demons, 250,  R::Rare},
        {"demon-500",     "demons", "Infernal",          kDemon, M::Demons, 500,  R::Epic},
        {"demon-1k",      "demons", "Thousand Torments", kDemon, M::Demons, 1000, R::Legendary},
        {"demon-2k",      "demons", "Abyss Walker",      kDemon, M::Demons, 2000, R::Legendary},
        {"demon-5k",      "demons", "Ruler of Hell",     kDemon, M::Demons, 5000, R::Mythic},

        {"easyd-1",       "demons", "First Blood",       kEasyD, M::EasyDemons, 1,   R::Common},
        {"easyd-25",      "demons", "Warming Up",        kEasyD, M::EasyDemons, 25,  R::Common},
        {"easyd-100",     "demons", "Easy Does It",      kEasyD, M::EasyDemons, 100, R::Uncommon},
        {"easyd-500",     "demons", "Gatekeeper",        kEasyD, M::EasyDemons, 500, R::Rare},

        {"medd-1",        "demons", "Stepping Deeper",   kMedD, M::MediumDemons, 1,   R::Common},
        {"medd-25",       "demons", "Middle Circle",     kMedD, M::MediumDemons, 25,  R::Uncommon},
        {"medd-100",      "demons", "Steady Hands",      kMedD, M::MediumDemons, 100, R::Rare},
        {"medd-400",      "demons", "Purgatory Regular", kMedD, M::MediumDemons, 400, R::Epic},

        {"hardd-1",       "demons", "Real Demon",        kHardD, M::HardDemons, 1,   R::Common},
        {"hardd-25",      "demons", "Hardened",          kHardD, M::HardDemons, 25,  R::Uncommon},
        {"hardd-100",     "demons", "Iron Will",         kHardD, M::HardDemons, 100, R::Rare},
        {"hardd-300",     "demons", "Unbreakable",       kHardD, M::HardDemons, 300, R::Epic},
        {"hardd-750",     "demons", "Forged in Fire",    kHardD, M::HardDemons, 750, R::Legendary},

        {"insd-1",        "demons", "Into the Insane",   kInsD, M::InsaneDemons, 1,   R::Uncommon},
        {"insd-10",       "demons", "Losing It",         kInsD, M::InsaneDemons, 10,  R::Uncommon},
        {"insd-50",       "demons", "Certified Insane",  kInsD, M::InsaneDemons, 50,  R::Rare},
        {"insd-150",      "demons", "Beyond Reason",     kInsD, M::InsaneDemons, 150, R::Epic},
        {"insd-400",      "demons", "Madness Incarnate", kInsD, M::InsaneDemons, 400, R::Legendary},
        {"insd-1k",       "demons", "The Asylum",        kInsD, M::InsaneDemons, 1000,R::Mythic},

        {"extd-1",        "extremes", "First Extreme",   kExtD, M::ExtremeDemons, 1,   R::Rare},
        {"extd-5",        "extremes", "Extremist",       kExtD, M::ExtremeDemons, 5,   R::Rare},
        {"extd-10",       "extremes", "Double Digits",   kExtD, M::ExtremeDemons, 10,  R::Epic},
        {"extd-25",       "extremes", "Peak Chaser",     kExtD, M::ExtremeDemons, 25,  R::Epic},
        {"extd-50",       "extremes", "Summit Keeper",   kExtD, M::ExtremeDemons, 50,  R::Legendary},
        {"extd-100",      "extremes", "Century of Pain", kExtD, M::ExtremeDemons, 100, R::Legendary},
        {"extd-200",      "extremes", "Top Player",      kExtD, M::ExtremeDemons, 200, R::Mythic},
        {"extd-400",      "extremes", "Impossible Made Real", kExtD, M::ExtremeDemons, 400, R::Mythic},

        {"platd-1",       "platformer", "New Ground",    kPlatD, M::PlatformerDemons, 1,   R::Uncommon},
        {"platd-10",      "platformer", "Sidestepper",   kPlatD, M::PlatformerDemons, 10,  R::Rare},
        {"platd-50",      "platformer", "Free Runner",   kPlatD, M::PlatformerDemons, 50,  R::Epic},
        {"platd-150",     "platformer", "Dimension Walker", kPlatD, M::PlatformerDemons, 150, R::Legendary},
        {"platd-400",     "platformer", "Platform Sovereign", kPlatD, M::PlatformerDemons, 400, R::Mythic},

        {"weekly-1",      "events", "Weekly Debut",      kWeekly, M::WeeklyDemons, 1,   R::Common},
        {"weekly-10",     "events", "Weekly Regular",    kWeekly, M::WeeklyDemons, 10,  R::Uncommon},
        {"weekly-50",     "events", "Never Miss a Week", kWeekly, M::WeeklyDemons, 50,  R::Rare},
        {"weekly-150",    "events", "Three Years Down",  kWeekly, M::WeeklyDemons, 150, R::Epic},
        {"weekly-300",    "events", "Keeper of Weeks",   kWeekly, M::WeeklyDemons, 300, R::Legendary},

        {"gaunt-1",       "events", "Gauntlet Runner",   kGauntlet, M::GauntletDemons, 1,   R::Common},
        {"gaunt-10",      "events", "Chain Breaker",     kGauntlet, M::GauntletDemons, 10,  R::Uncommon},
        {"gaunt-30",      "events", "Gauntlet Veteran",  kGauntlet, M::GauntletDemons, 30,  R::Rare},
        {"gaunt-60",      "events", "Gauntlet Complete", kGauntlet, M::GauntletDemons, 60,  R::Epic},

        {"daily-10",      "events", "Daily Habit",       kDaily, M::DailyLevels, 10,   R::Common},
        {"daily-50",      "events", "Streaker",          kDaily, M::DailyLevels, 50,   R::Uncommon},
        {"daily-200",     "events", "Calendar Keeper",   kDaily, M::DailyLevels, 200,  R::Rare},
        {"daily-500",     "events", "Every Single Day",  kDaily, M::DailyLevels, 500,  R::Epic},
        {"daily-1k",      "events", "Timeless",          kDaily, M::DailyLevels, 1000, R::Legendary},

        {"cp-1",          "creator", "Creator",          kPoints, M::CreatorPoints, 1,  R::Rare},
        {"cp-5",          "creator", "Rated Builder",    kPoints, M::CreatorPoints, 5,  R::Epic},
        {"cp-15",         "creator", "Known Creator",    kPoints, M::CreatorPoints, 15, R::Epic},
        {"cp-30",         "creator", "Famous Creator",   kPoints, M::CreatorPoints, 30, R::Legendary},
        {"cp-60",         "creator", "Legend of the Editor", kPoints, M::CreatorPoints, 60, R::Mythic},

        {"map-5",         "mastery", "Pack Opener",      kMapPack, M::MapPacks, 5,  R::Common},
        {"map-15",        "mastery", "Pack Collector",   kMapPack, M::MapPacks, 15, R::Uncommon},
        {"map-30",        "mastery", "Pack Completionist", kMapPack, M::MapPacks, 30, R::Rare},
        {"map-50",        "mastery", "All Packed Up",    kMapPack, M::MapPacks, 50, R::Epic},

        {"harder-25",     "mastery", "Getting Harder",   kHarder, M::HarderLevels, 25,   R::Common},
        {"harder-150",    "mastery", "No Sweat",         kHarder, M::HarderLevels, 150,  R::Uncommon},
        {"harder-600",    "mastery", "Hard Labour",      kHarder, M::HarderLevels, 600,  R::Rare},
        {"harder-1500",   "mastery", "Difficulty Denier",kHarder, M::HarderLevels, 1500, R::Epic},

        {"insane-25",     "mastery", "Insanity Starter", kInsane, M::InsaneLevels, 25,   R::Common},
        {"insane-150",    "mastery", "Insane Regular",   kInsane, M::InsaneLevels, 150,  R::Uncommon},
        {"insane-500",    "mastery", "Insane Devotee",   kInsane, M::InsaneLevels, 500,  R::Rare},
        {"insane-1200",   "mastery", "Sanity Optional",  kInsane, M::InsaneLevels, 1200, R::Epic},
        {"insane-3000",   "mastery", "Pure Insanity",    kInsane, M::InsaneLevels, 3000, R::Legendary},

        {"rank-100k",     "ranking", "On the Board",     "rankIcon_all_001.png",      M::GlobalRank, 100000, R::Common},
        {"rank-10k",      "ranking", "Top 10.000",       "rankIcon_top10000_001.png", M::GlobalRank, 10000,  R::Uncommon},
        {"rank-1k",       "ranking", "Top 1.000",        "rankIcon_top1000_001.png",  M::GlobalRank, 1000,   R::Rare},
        {"rank-100",      "ranking", "Top 100",          "rankIcon_top100_001.png",   M::GlobalRank, 100,    R::Epic},
        {"rank-10",       "ranking", "Top 10",           "rankIcon_top10_001.png",    M::GlobalRank, 10,     R::Legendary},
        {"rank-1",        "ranking", "Number One",       "rankIcon_1_001.png",        M::GlobalRank, 1,      R::Mythic},

        {"lvl-10",        "journey", "Getting Started",  kBigStar, M::Level, 10,  R::Common},
        {"lvl-25",        "journey", "Finding a Rhythm", kBigStar, M::Level, 25,  R::Common},
        {"lvl-50",        "journey", "Halfway to Somewhere", kBigStar, M::Level, 50,  R::Uncommon},
        {"lvl-75",        "journey", "Seasoned",         kBigStar, M::Level, 75,  R::Rare},
        {"lvl-100",       "journey", "Triple Digits",    kBigStar, M::Level, 100, R::Epic},
        {"lvl-125",       "journey", "Relentless",       kBigStar, M::Level, 125, R::Legendary},
        {"lvl-150",       "journey", "Beyond the Chart", kBigStar, M::Level, 150, R::Legendary},
        {"lvl-200",       "journey", "Maximum",          kBigStar, M::Level, kMaxLevel, R::Mythic},

        {"exp-50k",       "journey", "Fifty Thousand",   kMagic, M::TotalExp, 50000,   R::Common},
        {"exp-250k",      "journey", "Quarter Million",  kMagic, M::TotalExp, 250000,  R::Uncommon},
        {"exp-750k",      "journey", "Grinder",          kMagic, M::TotalExp, 750000,  R::Epic},
        {"exp-2m",        "journey", "Two Million",      kMagic, M::TotalExp, 2000000, R::Mythic},
    };
}

std::vector<BadgeCategory> buildCategories() {
    return {
        {"journey",    "Journey",    kBigStar,                   {255, 205, 100}},
        {"stars",      "Stars",      kStar,                      {255, 232,  96}},
        {"moons",      "Moons",      kMoon,                      {140, 185, 255}},
        {"diamonds",   "Diamonds",   kDiamond,                   {105, 235, 245}},
        {"coins",      "Coins",      kCoin,                      {250, 160,  55}},
        {"demons",     "Demons",     kDemon,                     {235,  85,  85}},
        {"extremes",   "Extremes",   kExtD,                      {255,  95, 175}},
        {"platformer", "Platformer", kPlatD,                     {115, 225, 135}},
        {"events",     "Events",     kWeekly,                    {185, 130, 255}},
        {"mastery",    "Mastery",    "GJ_completesIcon_001.png", { 85, 210, 195}},
        {"creator",    "Creator",    kPoints,                    {160, 245,  90}},
        {"ranking",    "Ranking",    "rankIcon_top100_001.png",  {200, 210, 230}},
    };
}

} // namespace

std::vector<BadgeDef> const& allBadges() {
    static std::vector<BadgeDef> const badges = buildBadges();
    return badges;
}

std::vector<BadgeCategory> const& allCategories() {
    static std::vector<BadgeCategory> const categories = buildCategories();
    return categories;
}

BadgeContext makeContext(PlayerStats const& stats) {
    BadgeContext ctx;
    ctx.stats = stats;
    reconcileDemons(ctx.stats);
    ctx.exp = computeExp(ctx.stats).total;
    ctx.level = levelForExp(ctx.exp);
    return ctx;
}

bool metricIsInverted(BadgeMetric metric) {
    return metric == BadgeMetric::GlobalRank;
}

int64_t metricValue(BadgeContext const& ctx, BadgeMetric metric) {
    auto const& s = ctx.stats;
    switch (metric) {
        case BadgeMetric::Stars:            return s.stars;
        case BadgeMetric::Moons:            return s.moons;
        case BadgeMetric::Diamonds:         return s.diamonds;
        case BadgeMetric::SecretCoins:      return s.secretCoins;
        case BadgeMetric::UserCoins:        return s.userCoins;
        case BadgeMetric::Demons:           return s.demons;
        case BadgeMetric::EasyDemons:       return s.demonInfo.easy();
        case BadgeMetric::MediumDemons:     return s.demonInfo.medium();
        case BadgeMetric::HardDemons:       return s.demonInfo.hard();
        case BadgeMetric::InsaneDemons:     return s.demonInfo.insane();
        case BadgeMetric::ExtremeDemons:    return s.demonInfo.extreme();
        case BadgeMetric::PlatformerDemons: return s.demonInfo.platformer();
        case BadgeMetric::WeeklyDemons:     return s.demonInfo.weekly;
        case BadgeMetric::GauntletDemons:   return s.demonInfo.gauntlet;
        case BadgeMetric::CreatorPoints:    return s.creatorPoints;
        case BadgeMetric::MapPacks:         return s.classicInfo.mapPacks + s.platformerInfo.mapPacks;
        case BadgeMetric::DailyLevels:      return s.classicInfo.daily + s.platformerInfo.daily;
        case BadgeMetric::InsaneLevels:     return s.classicInfo.insane + s.platformerInfo.insane;
        case BadgeMetric::HarderLevels:     return s.classicInfo.harder + s.platformerInfo.harder;
        case BadgeMetric::GlobalRank:       return s.globalRank;
        case BadgeMetric::Level:            return ctx.level;
        case BadgeMetric::TotalExp:         return ctx.exp;
    }
    return 0;
}

bool isUnlocked(BadgeDef const& badge, BadgeContext const& ctx) {
    int64_t const value = metricValue(ctx, badge.metric);
    if (metricIsInverted(badge.metric)) {
        return value > 0 && value <= badge.threshold;
    }
    return value >= badge.threshold;
}

float badgeProgress(BadgeDef const& badge, BadgeContext const& ctx) {
    if (isUnlocked(badge, ctx)) return 1.f;

    int64_t const value = metricValue(ctx, badge.metric);
    if (metricIsInverted(badge.metric)) {
        // Unranked players have nothing to interpolate from.
        if (value <= 0) return 0.f;
        return std::clamp(static_cast<float>(badge.threshold) / static_cast<float>(value), 0.f, 1.f);
    }
    if (badge.threshold <= 0) return 1.f;
    return std::clamp(static_cast<float>(value) / static_cast<float>(badge.threshold), 0.f, 1.f);
}

int unlockedCount(BadgeContext const& ctx) {
    int count = 0;
    for (auto const& badge : allBadges()) {
        if (isUnlocked(badge, ctx)) ++count;
    }
    return count;
}

cocos2d::ccColor3B rarityColor(BadgeRarity rarity) {
    switch (rarity) {
        case BadgeRarity::Common:    return {170, 180, 195};
        case BadgeRarity::Uncommon:  return {110, 205, 130};
        case BadgeRarity::Rare:      return { 85, 160, 250};
        case BadgeRarity::Epic:      return {180, 110, 250};
        case BadgeRarity::Legendary: return {252, 190,  70};
        case BadgeRarity::Mythic:    return {255, 105, 140};
    }
    return {170, 180, 195};
}

cocos2d::ccColor3B categoryColor(std::string_view categoryId) {
    for (auto const& category : allCategories()) {
        if (categoryId == category.id) return category.color;
    }
    return {200, 210, 230};
}

static char const* metricId(BadgeMetric metric) {
    switch (metric) {
        case BadgeMetric::Stars:            return "stars";
        case BadgeMetric::Moons:            return "moons";
        case BadgeMetric::Diamonds:         return "diamonds";
        case BadgeMetric::SecretCoins:      return "secret-coins";
        case BadgeMetric::UserCoins:        return "user-coins";
        case BadgeMetric::Demons:           return "demons";
        case BadgeMetric::EasyDemons:       return "easy-demons";
        case BadgeMetric::MediumDemons:     return "medium-demons";
        case BadgeMetric::HardDemons:       return "hard-demons";
        case BadgeMetric::InsaneDemons:     return "insane-demons";
        case BadgeMetric::ExtremeDemons:    return "extreme-demons";
        case BadgeMetric::PlatformerDemons: return "platformer-demons";
        case BadgeMetric::WeeklyDemons:     return "weekly-demons";
        case BadgeMetric::GauntletDemons:   return "gauntlet-demons";
        case BadgeMetric::CreatorPoints:    return "creator-points";
        case BadgeMetric::MapPacks:         return "map-packs";
        case BadgeMetric::DailyLevels:      return "daily-levels";
        case BadgeMetric::InsaneLevels:     return "insane-levels";
        case BadgeMetric::HarderLevels:     return "harder-levels";
        case BadgeMetric::GlobalRank:       return "global-rank";
        case BadgeMetric::Level:            return "level";
        case BadgeMetric::TotalExp:         return "total-exp";
    }
    return "stars";
}

std::string metricLabel(BadgeMetric metric) {
    return Localization::get().getString(std::string("progression.metric.") + metricId(metric));
}

std::string rarityLabel(BadgeRarity rarity) {
    return Localization::get().getString(std::string("progression.rarity.") + rarityId(rarity));
}

std::string categoryLabel(std::string_view categoryId) {
    return Localization::get().getString("progression.category." + std::string(categoryId));
}

std::string badgeRequirement(BadgeDef const& badge) {
    auto& loc = Localization::get();
    if (badge.metric == BadgeMetric::GlobalRank) {
        return fmt::format(fmt::runtime(loc.getString("progression.req.rank")),
                           formatCount(badge.threshold));
    }
    if (badge.metric == BadgeMetric::Level) {
        return fmt::format(fmt::runtime(loc.getString("progression.req.level")),
                           formatCount(badge.threshold));
    }
    return fmt::format(fmt::runtime(loc.getString("progression.req.reach")),
                       formatCount(badge.threshold), metricLabel(badge.metric));
}

std::string badgeShortGoal(BadgeDef const& badge) {
    // A rank counts down, so it needs the hash to not read as a total.
    if (badge.metric == BadgeMetric::GlobalRank) return "#" + shortCount(badge.threshold);
    return shortCount(badge.threshold);
}

std::string badgeProgressText(BadgeDef const& badge, BadgeContext const& ctx) {
    int64_t const value = metricValue(ctx, badge.metric);
    if (badge.metric == BadgeMetric::GlobalRank) {
        if (value <= 0) return Localization::get().getString("progression.req.unranked");
        return fmt::format("#{} / #{}", formatCount(value), formatCount(badge.threshold));
    }
    return fmt::format("{} / {}", formatCount(value), formatCount(badge.threshold));
}

char const* rarityId(BadgeRarity rarity) {
    switch (rarity) {
        case BadgeRarity::Common:    return "common";
        case BadgeRarity::Uncommon:  return "uncommon";
        case BadgeRarity::Rare:      return "rare";
        case BadgeRarity::Epic:      return "epic";
        case BadgeRarity::Legendary: return "legendary";
        case BadgeRarity::Mythic:    return "mythic";
    }
    return "common";
}

} // namespace paimon::progression
