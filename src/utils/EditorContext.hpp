#pragma once

// editor isolation switch: detect by running scene, not typeid (fragile with $modify).

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
