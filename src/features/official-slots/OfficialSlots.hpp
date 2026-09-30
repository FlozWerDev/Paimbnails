#pragma once

// cosmetic slots layered over robtop's official level list: paint-only
// descriptions, never handed to currency code, so misses grant nothing.

#include <Geode/DefaultInclude.hpp>

#include <string>
#include <vector>

namespace paimon::officialslots {

constexpr char const* kModuleId = "paimbnails.officialslots.level";

// values match gjdifficultysprite::create so the faces are the game's own;
// auto is -1 there, hence the explicit values.
enum class Difficulty : int {
    Auto = -1,
    Unrated = 0,
    Easy = 1,
    Normal = 2,
    Hard = 3,
    Harder = 4,
    Insane = 5,
    Demon = 6,
    EasyDemon = 7,
    MediumDemon = 8,
    InsaneDemon = 9,
    ExtremeDemon = 10,
};

// rate tier, drawn through gjdifficultysprite::updatefeaturestate so the glow
// is the vanilla one rather than a coin we mount by hand at the wrong scale.
enum class Tier : int {
    None = 0,
    Featured = 1,
    Epic = 2,
    Legendary = 3,
    Mythic = 4,
};

enum class Source : int {
    LevelId = 0,  // downloaded from the servers on demand
    Gmd = 1,      // imported from a .gmd file kept in our save dir
};

constexpr int kMinStars = 0;
constexpr int kMaxStars = 100;

struct Slot {
    std::string id;            // our own uuid, stable across reorders
    Source source = Source::LevelId;

    int levelId = 0;           // source::levelid
    std::string gmdFile;       // source::gmd, filename inside our gmd folder

    std::string name;          // shown on the card and the official page
    std::string author;

    Difficulty difficulty = Difficulty::Unrated;
    Tier tier = Tier::None;
    int stars = 0;             // cosmetic only, never granted
    bool coins = false;        // draws the three silver coins

    // official page this slot replaces, or 0 to append after the vanilla ones.
    int replacesOfficialId = 0;

    bool enabled = true;
};

// difficulty <-> the value gjdifficultysprite wants. kept as a function instead
// of a cast so the auto = -1 hole stays in one place.
int difficultyFace(Difficulty difficulty);

// every difficulty in picker order.
std::vector<Difficulty> const& allDifficulties();
std::vector<Tier> const& allTiers();

// true for robtop's official levels (1..22). mirrors paimon::ismainlevelid
// without dragging in the thumbnail cache helpers.
bool isOfficialId(int levelId);

} // namespace paimon::officialslots
