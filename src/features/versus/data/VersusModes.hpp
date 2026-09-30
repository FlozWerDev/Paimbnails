#pragma once

#include "VersusTypes.hpp"

#include <array>
#include <string>
#include <vector>

namespace paimon::versus {

struct FormatDef {
    Format id;
    char const* key;
    char const* name;
    uint8_t modes;
    int timeLimit;      // seconds, 0 when the format has no clock
    int attemptLimit;   // 0 when attempts are unlimited
    bool ranked;        // friendly is the only one that never moves elo
    bool cards;         // only roulette deals
};

inline constexpr size_t kFormatCount = 10;

std::array<FormatDef, kFormatCount> const& allFormats();
FormatDef const& formatAt(Format format);
FormatDef const* findFormat(std::string const& key);

// formats that can be queued for in this mode, friendly excluded.
std::vector<FormatDef const*> rankedFormats(Mode mode);
// the same list minus the ones a disabled module would turn into a race under
// another name. everything that opens a queue asks this, not rankedformats.
std::vector<FormatDef const*> queueableFormats(Mode mode);
// what a friendly may be played under: the queueable set plus friendly, which
// has no rules of its own and is the point of the whole thing for some people.
std::vector<FormatDef const*> friendlyFormats(Mode mode);

std::string formatName(FormatDef const& def);
std::string formatWinCondition(FormatDef const& def);
std::string formatSprite(FormatDef const& def);

// each quarter segment is claimed once; relay results require all four segments to close.
inline constexpr int kLadderSegments = 4;
inline constexpr int kLadderToWin = 3;
int segmentForPercent(float percent);

// king of the hill: how long the lead has to hold.
inline constexpr float kHillSeconds = 45.f;

// tug of war: the rope sits at 0 and is won at 1 or -1. a steady lead of this
// many points pulls it all the way in kropeseconds.
inline constexpr float kRopeLead = 25.f;
inline constexpr float kRopeSeconds = 16.f;

} // namespace paimon::versus
