#pragma once

// Call EditorPauseLayer callbacks without opening the pause menu.
// Uses a real unattached pause layer, so this stays type-safe and does not
// require BetterEdit's helper or another runtime dependency.

#include <Geode/binding/EditorPauseLayer.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <functional>

namespace paimon::editor {

// Runs `fn(pause)` on a temporary, unattached EditorPauseLayer.
// Returns false if the editor is missing.
bool withFakePause(LevelEditorLayer* lel, std::function<void(EditorPauseLayer*)> const& fn);

bool runBuildHelper(LevelEditorLayer* lel);
bool runCreateLoop(LevelEditorLayer* lel);
bool runAlignX(LevelEditorLayer* lel);
bool runAlignY(LevelEditorLayer* lel);
bool runSaveLevel(LevelEditorLayer* lel);
bool runSelectAllLeft(LevelEditorLayer* lel);  // not pause — editor UI
bool runSelectAllRight(LevelEditorLayer* lel);

} // namespace paimon::editor
