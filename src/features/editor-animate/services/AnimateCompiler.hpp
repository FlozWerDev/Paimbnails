#pragma once

#include "../AnimateTypes.hpp"

#include <Geode/Geode.hpp>

#include <cstddef>

class LevelEditorLayer;

namespace paimon::animate {

struct CompileReport {
    int triggers = 0;
    int steps = 0;
    int groups = 0;
    float duration = 0.f;
};

geode::Result<CompileReport> compileClip(LevelEditorLayer* editor, Clip& clip);
std::size_t removeCompiled(LevelEditorLayer* editor, Clip& clip);

// clips flagged compile-on-save that changed since their last bake.
int compileStale(LevelEditorLayer* editor);

} // namespace paimon::animate
