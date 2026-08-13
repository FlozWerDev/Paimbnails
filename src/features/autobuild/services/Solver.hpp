#pragma once

// Choosing which piece goes where. Pure computation: it never touches the
// editor, so it can be reasoned about (and re-run with another seed) on its own.

#include <vector>

#include "../AutobuildTypes.hpp"

namespace paimon::autobuild {

struct Point {
    float x = 0.f;
    float y = 0.f;
};

// One place the build may fill: a marker, a selected object or a cell of an area.
struct Target {
    Point pos;
};

struct Placement {
    int piece = -1;
    Point pos;
    PieceTransform transform;
};

struct SolveStats {
    int cells = 0;
    int filled = 0;
    int gaps = 0;       // cells the wave left empty on purpose
    int forced = 0;     // cells no tile fit, filled with the closest match
    int backtracks = 0;
    int smartExact = 0;
    int smartRemapped = 0;
    int smartSimplified = 0;
    long long ms = 0;
    bool budgetExceeded = false;
};

std::vector<Placement> solveWave(Template const& tpl, Options const& opts,
                                 std::vector<Target> const& targets,
                                 unsigned seed, SolveStats& stats);

std::vector<Placement> solveStamps(Template const& tpl, Options const& opts,
                                   std::vector<Target> const& targets,
                                   unsigned seed, SolveStats& stats);

} // namespace paimon::autobuild
