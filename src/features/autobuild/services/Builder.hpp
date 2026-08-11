#pragma once

// Running a template on the level: where to build, pasting the result and
// rolling it back.

#include <Geode/Geode.hpp>

#include "../AutobuildTypes.hpp"
#include "Solver.hpp"

class EditorUI;

namespace paimon::autobuild {

struct BuildReport {
    int targets = 0;
    int objects = 0;
    int gaps = 0;
    int forced = 0;
    unsigned seed = 0;
    long long ms = 0;
    bool truncated = false;

    std::string describe() const;
};

// Where a run would build, resolved from the current selection.
struct BuildPlan {
    std::vector<Target> targets;
    std::vector<GameObject*> markers;  // consumed by the build when asked to
};

geode::Result<BuildPlan> planBuild(EditorUI* ui, Options const& opts, float cell);

geode::Result<BuildReport> generate(EditorUI* ui, Template const& tpl, Options const& opts);

// Same targets as the last build, new seed: undoes the previous result first.
geode::Result<BuildReport> regenerate(EditorUI* ui, Template const& tpl, Options const& opts);

bool canUndo(EditorUI* ui);
geode::Result<> undoLast(EditorUI* ui);
void forgetSession();

} // namespace paimon::autobuild
