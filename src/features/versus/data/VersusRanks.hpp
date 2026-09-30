#pragma once

#include "VersusTypes.hpp"

#include <Geode/cocos/include/ccTypes.h>
#include <string>

namespace paimon::versus {

// the vs ladder reuses the twenty progression tiers by name and colour, so a
// new tier there is still one row and shows up here for free.
inline constexpr int kStartElo = 1000;
inline constexpr int kPlacementMatches = 5;

// paimon is handed out by the server to the top twenty of each mode, not
// reached by elo, so it has no floor of its own.
inline constexpr int kPaimonTier = 19;

struct RankInfo {
    int tierIndex = 0;
    int division = 0;       // 1 is the highest, 0 when the tier has none
    int elo = kStartElo;
    int intoTier = 0;
    int tierSpan = 1;
    int placementsLeft = 0;

    bool placing() const { return placementsLeft > 0; }
    bool hasDivision() const { return division > 0; }
    float tierProgress() const;
};

RankInfo rankFor(int elo, int placementsLeft = 0, bool paimon = false);

std::string rankName(RankInfo const& rank);
std::string rankShortName(RankInfo const& rank);
cocos2d::ccColor3B rankColor(RankInfo const& rank);

} // namespace paimon::versus
