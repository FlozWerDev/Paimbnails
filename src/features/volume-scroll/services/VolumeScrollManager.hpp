#pragma once

#include <Geode/Geode.hpp>

namespace paimon::volscroll {

enum class VolumeKind {
    Music,  // Ctrl + scroll
    SFX     // Shift + scroll
};

// volume overlay, FMOD updates, auto-hide, and the scroll-held state Quick Hub reads.

class VolumeScrollManager {
public:
    static VolumeScrollManager& get();

    void init();
    void update(float dt);
    void onSceneChange();
    void releaseSharedResources();

    bool onScroll(VolumeKind kind, float delta);

private:
    VolumeScrollManager() = default;

    // Hidden -> SlidingIn -> Expanding -> Visible -> Collapsing -> SlidingOut.
    enum class State {
        Hidden,
        SlidingIn,
        Expanding,
        Visible,
        Collapsing,
        SlidingOut
    };

    void ensureOverlayBuilt();
    void attachToRunningScene();
    void detachFromScene();
    void rebuildContent();
    void resetAutoHideTimer();
    void redrawPill();
    void redrawBar();
    void applyExpandProgress();

    float readVolume(VolumeKind kind) const;
    void  writeVolume(VolumeKind kind, float value);

    State m_state = State::Hidden;
    VolumeKind m_currentKind = VolumeKind::Music;
    float m_animProgress = 0.f;    // 0 hidden, 1 visible
    float m_expandProgress = 0.f;  // 0 compact, 1 expanded
    float m_visibleTimer = 0.f;    // time left visible
    float m_displayedVolume = 0.f; // smoothed display value
    float m_targetVolume = 0.f;    // target value

    geode::Ref<cocos2d::CCLayerRGBA> m_overlay;
    geode::Ref<cocos2d::CCNode> m_pillNode;
    geode::Ref<cocos2d::CCLabelBMFont> m_iconLabel;
    geode::Ref<cocos2d::CCLabelBMFont> m_label;
    geode::Ref<cocos2d::CCDrawNode> m_barDraw;
    float m_barAlpha = 0.f;
    cocos2d::CCScene* m_attachedScene = nullptr;
};

// true while a volume-scroll bind is held, so smooth-scroll doesn't replay
// one wheel tick as momentum.
bool isVolumeGestureActive();

}

void initVolumeScrollTicker();
void shutdownVolumeScrollTicker();
