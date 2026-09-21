#pragma once

#include "ProgressionStats.hpp"

#include <Geode/cocos/include/ccTypes.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace paimon::progression {

enum class BadgeMetric {
    Stars,
    Moons,
    Diamonds,
    SecretCoins,
    UserCoins,
    Demons,
    EasyDemons,
    MediumDemons,
    HardDemons,
    InsaneDemons,
    ExtremeDemons,
    PlatformerDemons,
    WeeklyDemons,
    GauntletDemons,
    CreatorPoints,
    MapPacks,
    DailyLevels,
    InsaneLevels,
    HarderLevels,
    GlobalRank,
    Level,
    TotalExp,
};

enum class BadgeRarity {
    Common,
    Uncommon,
    Rare,
    Epic,
    Legendary,
    Mythic,
};

struct BadgeDef {
    char const* id;
    char const* category;
    char const* name;
    char const* glyph;
    BadgeMetric metric;
    int64_t threshold;
    BadgeRarity rarity;
};

struct BadgeCategory {
    char const* id;
    char const* name;
    char const* glyph;
    cocos2d::ccColor3B color;
};

// Everything a badge is evaluated against, so callers build it once per profile.
struct BadgeContext {
    PlayerStats stats;
    int level = 1;
    int64_t exp = 0;
};

BadgeContext makeContext(PlayerStats const& stats);

std::vector<BadgeDef> const& allBadges();
std::vector<BadgeCategory> const& allCategories();

// Global rank counts down, everything else counts up.
bool metricIsInverted(BadgeMetric metric);
int64_t metricValue(BadgeContext const& ctx, BadgeMetric metric);

bool isUnlocked(BadgeDef const& badge, BadgeContext const& ctx);
float badgeProgress(BadgeDef const& badge, BadgeContext const& ctx);
int unlockedCount(BadgeContext const& ctx);

cocos2d::ccColor3B rarityColor(BadgeRarity rarity);
// Rarity paints the frame, the category paints the face behind the glyph.
cocos2d::ccColor3B categoryColor(std::string_view categoryId);
char const* rarityId(BadgeRarity rarity);

std::string metricLabel(BadgeMetric metric);
std::string rarityLabel(BadgeRarity rarity);
std::string categoryLabel(std::string_view categoryId);
std::string badgeRequirement(BadgeDef const& badge);
// Two or three characters for the tile: "35K", "#100".
std::string badgeShortGoal(BadgeDef const& badge);
std::string badgeProgressText(BadgeDef const& badge, BadgeContext const& ctx);

} // namespace paimon::progression
