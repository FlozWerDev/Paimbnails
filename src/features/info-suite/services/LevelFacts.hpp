#pragma once

// flat, tab-grouped rows for extendedinfopopup; shared extraction so the
// popup only deals with layout.

#include <Geode/binding/GJGameLevel.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace paimon::info {

enum class FactTab {
    Level = 0,
    Song,
    Stats,
    Raw,
    Count
};

// rows that do more than show text when tapped.
enum class FactAction {
    None = 0,
    OpenOriginal,  // jump to the level this one was copied from
    OpenSong,      // open the song in the song browser
    OpenCreator,   // open the creator's profile
};

struct Fact {
    std::string label;
    std::string value;
    std::string copyText;   // empty → copy `value`
    FactTab tab = FactTab::Level;
    FactAction action = FactAction::None;
    int actionArg = 0;      // level id / song id / account id for the action
    bool accent = false;    // draw the value in the highlight color
};

// every readable field of the level, in display order. empty/unknown fields are
// skipped so a local level does not show a wall of zeroes.
std::vector<Fact> collectFacts(GJGameLevel* level);

char const* tabName(FactTab tab);

// human readable helpers, shared with the cells and history browsers.

// rating as gjdifficultysprite counts it: -1 auto, 0 unrated, 1-5 easy..insane,
// 6-10 the demon tiers.
int difficultyValue(GJGameLevel* level);
// same scale, but from the stars the creator requested: what an unrated level
// has instead of a rating. 0 when nothing was requested.
int requestedDifficultyValue(GJGameLevel* level);
// nombre de una cara suelta, para lo que no viene de un gjgamelevel (el
// historial del nivel trabaja con caras sacadas de la api, no con niveles).
std::string difficultyFaceName(int face);
std::string difficultyName(GJGameLevel* level);
std::string lengthName(int length);
std::string featureName(int featured, int isEpic);
std::string formatDuration(int64_t seconds);
std::string formatThousands(int64_t value);

} // namespace paimon::info
