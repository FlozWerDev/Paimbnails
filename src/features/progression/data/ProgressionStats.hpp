#pragma once

#include <array>
#include <cstdint>
#include <string>

class GJUserScore;

namespace paimon::progression {

// per-unit exp, public so the popup prints the numbers it awards.
namespace exp_values {
    inline constexpr int kStar          = 5;
    inline constexpr int kMoon          = 6;
    inline constexpr int kDiamond       = 2;
    inline constexpr int kUserCoin      = 20;
    inline constexpr int kSecretCoin    = 100;
    inline constexpr int kCreatorPoint  = 1500;

    // missing breakdown in older cached scores.
    inline constexpr int kDemonFallback = 75;

    inline constexpr int kDemonEasy      = 100;
    inline constexpr int kDemonMedium    = 150;
    inline constexpr int kDemonHard      = 225;
    inline constexpr int kDemonInsane    = 400;
    inline constexpr int kDemonExtreme   = 800;
    inline constexpr int kDemonEasyPlat    = 125;
    inline constexpr int kDemonMediumPlat  = 190;
    inline constexpr int kDemonHardPlat    = 280;
    inline constexpr int kDemonInsanePlat  = 500;
    inline constexpr int kDemonExtremePlat = 1000;

    // already counted in the buckets above; bonus only.
    inline constexpr int kDemonWeeklyBonus   = 75;
    inline constexpr int kDemonGauntletBonus = 40;

    inline constexpr int kMasteryAuto     = 0;
    inline constexpr int kMasteryEasy     = 2;
    inline constexpr int kMasteryNormal   = 4;
    inline constexpr int kMasteryHard     = 8;
    inline constexpr int kMasteryHarder   = 14;
    inline constexpr int kMasteryInsane   = 22;
    inline constexpr int kMasteryDaily    = 10;
    inline constexpr int kMasteryGauntlet = 8;
    inline constexpr int kMasteryMapPack  = 30;
}

struct DemonBreakdown {
    int easyClassic = 0;
    int mediumClassic = 0;
    int hardClassic = 0;
    int insaneClassic = 0;
    int extremeClassic = 0;
    int easyPlatformer = 0;
    int mediumPlatformer = 0;
    int hardPlatformer = 0;
    int insanePlatformer = 0;
    int extremePlatformer = 0;
    int weekly = 0;
    int gauntlet = 0;

    int counted() const;
    int extreme() const { return extremeClassic + extremePlatformer; }
    int insane() const { return insaneClassic + insanePlatformer; }
    int hard() const { return hardClassic + hardPlatformer; }
    int medium() const { return mediumClassic + mediumPlatformer; }
    int easy() const { return easyClassic + easyPlatformer; }
    int platformer() const;
};

// shared shape of the star/moon info strings.
struct DifficultyBreakdown {
    int autos = 0;
    int easy = 0;
    int normal = 0;
    int hard = 0;
    int harder = 0;
    int insane = 0;
    int daily = 0;
    int gauntlet = 0;
    int mapPacks = 0;

    int counted() const;
};

struct PlayerStats {
    int stars = 0;
    int moons = 0;
    int diamonds = 0;
    int secretCoins = 0;
    int userCoins = 0;
    int demons = 0;
    int creatorPoints = 0;
    int globalRank = 0;

    // from the versus server, not from the game.
    int64_t versusExp = 0;
    int versusWins = 0;

    DemonBreakdown demonInfo;
    DifficultyBreakdown classicInfo;
    DifficultyBreakdown platformerInfo;

    bool hasDemonInfo = false;
    bool hasClassicInfo = false;
    bool hasPlatformerInfo = false;
};

enum class ExpSource {
    Stars,
    Moons,
    Diamonds,
    UserCoins,
    SecretCoins,
    Demons,
    CreatorPoints,
    Mastery,
    Versus,
};

inline constexpr int kExpSourceCount = 9;

struct ExpEntry {
    ExpSource source = ExpSource::Stars;
    int64_t exp = 0;
    int64_t count = 0;
};

struct ExpReport {
    std::array<ExpEntry, kExpSourceCount> entries{};
    int64_t total = 0;
};

PlayerStats statsFromScore(GJUserScore* score);

// own profile only: live save beats the lagging server cache.
PlayerStats statsFromLocalSave();

// server breakdown can outlive its account; the game demon total decides.
void reconcileDemons(PlayerStats& stats);

ExpReport computeExp(PlayerStats const& stats);

char const* sourceId(ExpSource source);
char const* sourceIconFrame(ExpSource source);

// thousand-grouped number for labels ("1,204,500").
std::string formatCount(int64_t value);

// same number squeezed into a badge tile ("500", "35k", "2m").
std::string shortCount(int64_t value);

// comma separated gd info strings; missing trailing fields stay at zero.
DemonBreakdown parseDemonInfo(std::string const& raw, bool* ok = nullptr);
DifficultyBreakdown parseDifficultyInfo(std::string const& raw, bool* ok = nullptr);

} // namespace paimon::progression
