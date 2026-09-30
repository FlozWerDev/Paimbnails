#include "VersusRanks.hpp"
#include "../../progression/data/ProgressionTiers.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <array>

namespace paimon::versus {

namespace {

// lower bound of every tier. paimon has none: the server grants it to the top
// twenty of each mode and takes it away when they drop out.
constexpr std::array<int, 20> kFloor = {
       0,  800,  900, 1000, 1100, 1200, 1300, 1400, 1500, 1600,
    1700, 1800, 1900, 2000, 2125, 2250, 2375, 2500, 2700, 2700,
};

// divisions stop at paragon; above it the ladder is a single list per tier.
constexpr int kLastDividedTier = 12;

int ceilingOf(int tier) {
    if (tier >= kPaimonTier - 1) return kFloor[kPaimonTier - 1] + 400;
    return kFloor[tier + 1];
}

} // namespace

float RankInfo::tierProgress() const {
    if (tierSpan <= 0) return 1.f;
    return std::clamp(static_cast<float>(intoTier) / static_cast<float>(tierSpan), 0.f, 1.f);
}

RankInfo rankFor(int elo, int placementsLeft, bool paimon) {
    RankInfo rank;
    rank.elo = std::max(0, elo);
    rank.placementsLeft = std::max(0, placementsLeft);

    if (paimon) {
        rank.tierIndex = kPaimonTier;
        rank.tierSpan = 1;
        rank.intoTier = 1;
        return rank;
    }

    int tier = 0;
    for (int i = static_cast<int>(kFloor.size()) - 2; i >= 0; i--) {
        if (rank.elo >= kFloor[i]) { tier = i; break; }
    }

    rank.tierIndex = tier;
    rank.tierSpan = std::max(1, ceilingOf(tier) - kFloor[tier]);
    rank.intoTier = rank.elo - kFloor[tier];

    if (tier <= kLastDividedTier) {
        int const step = std::max(1, rank.tierSpan / 4);
        int const idx = std::min(3, rank.intoTier / step);
        rank.division = 4 - idx;
    }
    return rank;
}

std::string rankName(RankInfo const& rank) {
    auto const& tier = progression::tierAt(rank.tierIndex);
    if (!rank.hasDivision()) return tier.name;

    static char const* kNumerals[] = {"", "I", "II", "III", "IV"};
    return fmt::format("{} {}", tier.name, kNumerals[std::clamp(rank.division, 1, 4)]);
}

std::string rankShortName(RankInfo const& rank) {
    return progression::tierAt(rank.tierIndex).name;
}

cocos2d::ccColor3B rankColor(RankInfo const& rank) {
    return progression::tierAt(rank.tierIndex).base;
}

} // namespace paimon::versus
