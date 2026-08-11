#pragma once
// Where an icon comes from, read out of the game's own unlock tables instead of
// a list we would have to keep in sync with every update.

#include <Geode/Enums.hpp>

#include <string>

namespace paimon::iconcopy {

// The countable part of a goal, so it can be drawn instead of spelled out:
// 50 moons becomes the moon sprite next to a 50.
struct UnlockRequirement {
    std::string sprite;  // GD frame name, empty when there is nothing to draw
    int amount = 0;      // 0 when the goal is not a count

    bool drawable() const { return !sprite.empty() || amount > 0; }
};

struct UnlockInfo {
    std::string source;  // short label for the chip: "Achievement", "The Shop"...
    std::string name;    // achievement title or a short name for the unlock method
    std::string detail;  // the goal spelled out, empty when the chip says it all
    std::string hint;    // line under the bar, empty when there is nothing to add
    UnlockRequirement requirement;
    int progress = -1;      // 0-100 on achievements, -1 where the game keeps none
    bool owned = false;     // whether this save file already has the icon
    bool equipped = false;  // whether it is the one worn for this gamemode
    int total = 0;          // how many icons this gamemode ships with
};

UnlockInfo unlockInfoFor(int iconID, IconType type);

}  // namespace paimon::iconcopy
