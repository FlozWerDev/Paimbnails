#pragma once

#include <Geode/Geode.hpp>

namespace paimon::transitions {

inline bool isLoadingScene(cocos2d::CCScene* scene) {
    return scene && (scene->getChildByType<LoadingLayer>(0) ||
        scene->getChildByType<GJGameLoadingLayer>(0));
}

inline bool requiresNativeSceneNavigation(cocos2d::CCScene* scene) {
    if (auto* transition = geode::cast::typeinfo_cast<cocos2d::CCTransitionScene*>(scene)) {
        scene = transition->m_pInScene;
    }
    // gJGameLoadingLayer is a separate CCLayer, not a LoadingLayer subclass.
    // its handoff must retain the original scenes and their lifecycle callbacks.
    return scene && (isLoadingScene(scene) ||
        scene->getChildByType<LevelEditorLayer>(0) ||
        scene->getChildByType<EditorUI>(0));
}

inline bool requiresNativeSceneNavigation(cocos2d::CCScene* source, cocos2d::CCScene* destination) {
    return requiresNativeSceneNavigation(source) || requiresNativeSceneNavigation(destination);
}

}
