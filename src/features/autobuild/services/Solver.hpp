#pragma once

// Choosing which piece goes where. Pure computation: it never touches the
// editor, so it can be reasoned about (and re-run with another seed) on its own.

#include <cocos2d.h>

#include <vector>

#include "../AutobuildTypes.hpp"

namespace paimon::autobuild {

// One place the build may fill: a marker, a selected object or a cell of an area.
struct Target {
    cocos2d::CCPoint pos = {0.f, 0.f};
};

struct Placement {
    int piece = -1;
    cocos2d::CCPoint pos = {0.f, 0.f};
};

struct SolveStats {
    int cells = 0;
    int filled = 0;
    int gaps = 0;       // cells the wave left empty on purpose
    int forced = 0;     // cells no tile fit, filled with the closest match
    int backtracks = 0;
    long long ms = 0;
    bool timedOut = false;
};

std::vector<Placement> solveWave(Template const& tpl, Options const& opts,
                                 std::vector<Target> const& targets,
                                 unsigned seed, SolveStats& stats);

std::vector<Placement> solveStamps(Template const& tpl, Options const& opts,
                                   std::vector<Target> const& targets,
                                   unsigned seed, SolveStats& stats);

} // namespace paimon::autobuild
