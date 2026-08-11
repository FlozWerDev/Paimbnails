#pragma once
// Copied icon sets. The profile button snapshots the icons of whoever you are
// looking at, and the garage list puts one of those snapshots back on.

#include <Geode/Enums.hpp>
#include <matjson.hpp>

#include <cstdint>
#include <string>
#include <vector>

class GJUserScore;

namespace paimon::iconcopy {

struct IconSet {
    std::string username;
    int accountID = 0;
    int64_t copiedAt = 0;
    int cube = 1;
    int ship = 1;
    int ball = 1;
    int ufo = 1;
    int wave = 1;
    int robot = 1;
    int spider = 1;
    int swing = 1;
    int jetpack = 1;
    int trail = 0;         // 0 = the profile didn't report one, keep ours
    int deathEffect = 0;
    int color1 = 0;
    int color2 = 3;
    int glowColor = -1;  // -1 = no custom glow, GD falls back to color2
    bool glow = false;

    int iconFor(IconType type) const;
    std::string label() const;
};

bool enabled();

IconSet snapshot(GJUserScore* score);

// Hands the set over to GameManager: from here on it is the player's selection.
void apply(IconSet const& set);

// Stored sets, newest first.
std::vector<IconSet> const& entries();
void add(IconSet set);
void removeUser(IconSet const& set);
void clear();

}  // namespace paimon::iconcopy

template <>
struct matjson::Serialize<paimon::iconcopy::IconSet> {
    static geode::Result<paimon::iconcopy::IconSet> fromJson(matjson::Value const& value);
    static matjson::Value toJson(paimon::iconcopy::IconSet const& set);
};
