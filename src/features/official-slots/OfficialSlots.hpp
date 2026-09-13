#pragma once

// Cosmetic slots layered over RobTop's official level list.
//
// The whole point of this feature is that it is *decorative*. A slot never
// touches GameStatsManager, GJAccountManager or the vanilla save: it only
// describes how a page in the official list should be drawn. That is why the
// star count here is an int we paint next to a star sprite and never a value
// handed to the game's currency code — collecting a slot's stars is impossible
// by construction, not by a check we could forget somewhere.
//
// Two kinds of entry live in the same list:
//   - Added slots: a custom level (by id, or imported from a .gmd) shown as if
//     it were an official one.
//   - Hidden officials: an id in [1, 22] the user does not want to see. The
//     level itself is untouched, we just skip drawing its page.

#include <Geode/DefaultInclude.hpp>

#include <string>
#include <vector>

namespace paimon::officialslots {

constexpr char const* kModuleId = "paimbnails.officialslots.level";

// Slot difficulty. The values match what GJDifficultySprite::create expects, so
// the faces are the game's own and identical to the ones the request list draws
// (see features/thumb-requests). Auto is -1 there, hence the explicit values.
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

// Rate tier, drawn through GJDifficultySprite::updateFeatureState so the glow
// is the vanilla one rather than a coin we mount by hand at the wrong scale.
enum class Tier : int {
    None = 0,
    Featured = 1,
    Epic = 2,
    Legendary = 3,
    Mythic = 4,
};

// Where the level data comes from.
enum class Source : int {
    LevelId = 0,  // downloaded from the servers on demand
    Gmd = 1,      // imported from a .gmd file kept in our save dir
};

constexpr int kMinStars = 0;
constexpr int kMaxStars = 100;

struct Slot {
    std::string id;            // our own uuid, stable across reorders
    Source source = Source::LevelId;

    int levelId = 0;           // Source::LevelId
    std::string gmdFile;       // Source::Gmd, filename inside our gmd folder

    std::string name;          // shown on the card and the official page
    std::string author;

    Difficulty difficulty = Difficulty::Unrated;
    Tier tier = Tier::None;
    int stars = 0;             // cosmetic only, never granted
    bool coins = false;        // draws the three silver coins

    // Official page this slot replaces, or 0 to append after the vanilla ones.
    int replacesOfficialId = 0;

    bool enabled = true;
};

// Difficulty <-> the value GJDifficultySprite wants. Kept as a function instead
// of a cast so the Auto = -1 hole stays in one place.
int difficultyFace(Difficulty difficulty);

// Display name, for labels and the difficulty picker.
char const* difficultyName(Difficulty difficulty);
char const* tierName(Tier tier);

// Every difficulty in picker order.
std::vector<Difficulty> const& allDifficulties();
std::vector<Tier> const& allTiers();

// True when the id is one of RobTop's official levels (1..22). Mirrors
// paimon::isMainLevelID; kept separate so this header does not drag in the
// thumbnail cache helpers.
bool isOfficialId(int levelId);

} // namespace paimon::officialslots
