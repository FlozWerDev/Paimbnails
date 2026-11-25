#include "../src/features/transitions/services/NativeSceneNavigation.hpp"

#include <array>
#include <cassert>
#include <iostream>

using namespace cocos2d;
using namespace paimon::transitions;

namespace {

template <class Layer>
struct Scene {
    CCScene scene;
    Layer layer;

    Scene() { scene.addChild(&layer); }
};

void editorRoundTrip() {
    CCScene menu;
    Scene<GJGameLoadingLayer> loading;
    Scene<LevelEditorLayer> editor;

    // Both legs must stay native, including returning to the menu after saving.
    assert(requiresNativeSceneNavigation(&menu, &loading.scene));
    assert(requiresNativeSceneNavigation(&loading.scene, &editor.scene));
    assert(requiresNativeSceneNavigation(&editor.scene, &loading.scene));
    assert(requiresNativeSceneNavigation(&loading.scene, &menu));
    assert(requiresNativeSceneNavigation(&editor.scene, &menu));
}

void nativeFadeKeepsItsDestination() {
    Scene<GJGameLoadingLayer> loading;
    Scene<LevelEditorLayer> editor;
    CCTransitionScene nativeFade;
    nativeFade.m_pInScene = &loading.scene;
    assert(requiresNativeSceneNavigation(&nativeFade));
    nativeFade.m_pInScene = &editor.scene;
    assert(requiresNativeSceneNavigation(&nativeFade));
    assert(nativeFade.m_pInScene == &editor.scene);
}

void saveAndPlayReturnsThroughTheNativeLoader() {
    Scene<LevelEditorLayer> editor;
    Scene<GJGameLoadingLayer> loading;
    Scene<PlayLayer> gameplay;
    // Repeated Save and Play cycles must preserve both loader handoffs.
    for (int cycle = 0; cycle < 3; ++cycle) {
        assert(requiresNativeSceneNavigation(&editor.scene, &loading.scene));
        assert(requiresNativeSceneNavigation(&loading.scene, &gameplay.scene));
        assert(requiresNativeSceneNavigation(&gameplay.scene, &loading.scene));
        assert(requiresNativeSceneNavigation(&loading.scene, &editor.scene));
        CCTransitionScene fade;
        fade.m_pInScene = &loading.scene;
        assert(requiresNativeSceneNavigation(&editor.scene, &fade));
        assert(requiresNativeSceneNavigation(&fade, &gameplay.scene));
    }
}
void unrelatedNavigationRemainsAvailable() {
    CCScene menu, browser;
    Scene<PlayLayer> gameplay;
    assert(!requiresNativeSceneNavigation(&menu, &browser));
    // Gameplay entry/exit effects still have their own transition path.
    assert(!requiresNativeSceneNavigation(&menu, &gameplay.scene));
    assert(!requiresNativeSceneNavigation(&gameplay.scene, &menu));
    assert(!isLoadingScene(&gameplay.scene));
}

void loadingAndEditorAreRecognizedInEitherDirection() {
    Scene<LoadingLayer> startup;
    Scene<GJGameLoadingLayer> loading;
    Scene<LevelEditorLayer> editor;
    Scene<EditorUI> editorUI;
    CCScene menu;
    for (auto* scene : std::array{&startup.scene, &loading.scene, &editor.scene, &editorUI.scene}) {
        assert(requiresNativeSceneNavigation(&menu, scene));
        assert(requiresNativeSceneNavigation(scene, &menu));
    }
    assert(isLoadingScene(&startup.scene));
    assert(isLoadingScene(&loading.scene));
    assert(!isLoadingScene(&editor.scene));
    assert(!isLoadingScene(&editorUI.scene));
}

void modifiedLayersAndEmptyScenes() {
    struct ModifiedLoadingLayer : GJGameLoadingLayer {};
    struct ModifiedEditorLayer : LevelEditorLayer {};
    Scene<ModifiedLoadingLayer> loading;
    Scene<ModifiedEditorLayer> editor;
    CCScene empty;
    CCTransitionScene emptyTransition;
    assert(requiresNativeSceneNavigation(&loading.scene));
    assert(requiresNativeSceneNavigation(&editor.scene));
    assert(!requiresNativeSceneNavigation(&empty));
    assert(!requiresNativeSceneNavigation(&emptyTransition));
    assert(!requiresNativeSceneNavigation(nullptr));
    assert(!isLoadingScene(nullptr));
}

}

int main() {
    editorRoundTrip();
    nativeFadeKeepsItsDestination();
    saveAndPlayReturnsThroughTheNativeLoader();
    unrelatedNavigationRemainsAvailable();
    loadingAndEditorAreRecognizedInEitherDirection();
    modifiedLayersAndEmptyScenes();
    std::cout << "Native scene navigation regression checks passed.\n";
}
