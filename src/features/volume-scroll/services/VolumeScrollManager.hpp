#pragma once

#include <Geode/Geode.hpp>

namespace paimon::volscroll {

enum class VolumeKind {
    Music,  // Ctrl + scroll
    SFX     // Shift + scroll
};

// Singleton for the volume overlay, FMOD volume updates, auto-hide, and the
// scroll-held state consumed by Quick Hub.

class VolumeScrollManager {
public:
    static VolumeScrollManager& get();

    void init();
    void update(float dt);
    void onSceneChange();
    void releaseSharedResources();

    // Apply a scroll delta and show the overlay; return whether it was consumed.
    bool onScroll(VolumeKind kind, float delta);

    // Whether recent volume scroll should suppress Quick Hub.
    bool wasRecentlyUsed(float withinSeconds = 0.35f) const;

    bool isOverlayVisible() const { return m_state != State::Hidden; }

private:
    VolumeScrollManager() = default;

    // Hidden → SlidingIn → Expanding → Visible → Collapsing → SlidingOut.
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
    void startSlideOut();
    void redrawPill();
    void redrawBar();
    void applyExpandProgress();

    // Read/write FMOD volumes, clamped to [0, 1].
    float readVolume(VolumeKind kind) const;
    void  writeVolume(VolumeKind kind, float value);

    State m_state = State::Hidden;
    VolumeKind m_currentKind = VolumeKind::Music;
    float m_animProgress = 0.f;    // 0 hidden, 1 visible
    float m_expandProgress = 0.f;  // 0 compact, 1 expanded
    float m_visibleTimer = 0.f;    // time left visible
    float m_lastUseClock = -100.f; // seconds since last scroll
    float m_clock = 0.f;
    float m_displayedVolume = 0.f; // smoothed display value
    float m_targetVolume = 0.f;    // target value

    geode::Ref<cocos2d::CCLayerRGBA> m_overlay;
    geode::Ref<cocos2d::CCNode> m_pillNode;
    geode::Ref<cocos2d::CCLabelBMFont> m_iconLabel;
    geode::Ref<cocos2d::CCLabelBMFont> m_kindLabel;
    geode::Ref<cocos2d::CCLabelBMFont> m_label;
    geode::Ref<cocos2d::CCDrawNode> m_barDraw;
    float m_barAlpha = 0.f;
    cocos2d::CCScene* m_attachedScene = nullptr;
};

// True while a volume-scroll bind is held. Smooth-scroll uses this to avoid
// replaying one wheel tick as momentum.
bool isVolumeGestureActive();

}

void initVolumeScrollTicker();
void shutdownVolumeScrollTicker();
