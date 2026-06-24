#pragma once

// EditorContext.hpp — single source of truth for detecting the editor.
//
// The mod must fully isolate itself from the editor: no broad hook (button
// position capture, popup animations/blur, slider skin, etc.) should change
// behavior while the editor is active. We detect the editor by the running
// scene, not parent typeid (fragile when other mods $modify editor classes).

#include <Geode/Geode.hpp>

namespace paimon {

inline bool isEditorScene() {
    auto* director = cocos2d::CCDirector::get();
    if (!director) return false;
    auto* scene = director->getRunningScene();
    if (!scene) return false;
    return scene->getChildByType<LevelEditorLayer>(0) != nullptr ||
           scene->getChildByType<EditorUI>(0) != nullptr;
}

} // namespace paimon
