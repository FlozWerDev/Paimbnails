#pragma once

#include <Geode/Geode.hpp>

namespace paimon::volscroll {

// Volume kind the overlay currently controls
enum class VolumeKind {
    Music,  // Ctrl + scroll → m_musicVolume / setBackgroundMusicVolume
    SFX     // Shift + scroll → m_sfxVolume   / setEffectsVolume
};

// VolumeScrollManager
//
// Singleton handling: the bottom-right overlay UI (slide in/out), reading/writing
// FMODAudioEngine volume, auto-hide after inactivity, and the "scroll in use" state
// that cancels the Quick Hub Ctrl hold. The ticker (VolumeScrollHook.cpp) feeds update(dt).

class VolumeScrollManager {
public:
    static VolumeScrollManager& get();

    // Lifecycle
    void init();
    void update(float dt);
    void onSceneChange();          // called when the running scene changes
    void releaseSharedResources(); // cleanup when leaving the game

    // Public API (called by VolumeScrollHook). Applies delta to the given volume and shows
    // the overlay (delta is typically +/-0.05 per scroll click). Returns true to consume the scroll.
    bool onScroll(VolumeKind kind, float delta);

    // Whether a volume scroll happened in the last N seconds — used by QuickHubKeybind
    // to avoid opening the radial during Ctrl+Scroll.
    bool wasRecentlyUsed(float withinSeconds = 0.35f) const;

    // Current visible state (for tests/debug)
    bool isOverlayVisible() const { return m_state != State::Hidden; }

private:
    VolumeScrollManager() = default;

    // Overlay animation states: Hidden -> SlidingIn -> Expanding -> Visible -> Collapsing -> SlidingOut -> Hidden.
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
    void rebuildContent();         // re-render icon + bar + label
    void resetAutoHideTimer();
    void startSlideOut();
    void redrawPill();             // resize the GJ_square background to the current width
    void redrawBar();              // redraw the volume fill (CCDrawNode, colored per 25%) + thumb
    void applyExpandProgress();    // position/opacity of children per m_expandProgress

    // Read/write volumes from FMODAudioEngine. Clamp to [0, 1].
    float readVolume(VolumeKind kind) const;
    void  writeVolume(VolumeKind kind, float value);

    // State
    State m_state = State::Hidden;
    VolumeKind m_currentKind = VolumeKind::Music;
    float m_animProgress = 0.f;    // 0 = hidden below, 1 = visible (slide)
    float m_expandProgress = 0.f;  // 0 = compact chip, 1 = expanded
    float m_visibleTimer = 0.f;    // time left in Visible state
    float m_lastUseClock = -100.f; // seconds since init() of the last scroll
    float m_clock = 0.f;
    float m_displayedVolume = 0.f; // value shown by the bar (smooth lerp)
    float m_targetVolume = 0.f;    // real value (what we want to show)

    // UI
    geode::Ref<cocos2d::CCLayerRGBA> m_overlay;     // container with cascade opacity
    geode::Ref<cocos2d::CCNode> m_pillNode;         // GJ_square01 background (CCScale9Sprite)
    geode::Ref<cocos2d::CCLabelBMFont> m_iconLabel; // "MUS"/"SFX" chip (always visible when overlay shown)
    geode::Ref<cocos2d::CCLabelBMFont> m_kindLabel; // "Music"/"SFX" (only when expanded)
    geode::Ref<cocos2d::CCLabelBMFont> m_label;     // "75%" text (only when expanded)
    geode::Ref<cocos2d::CCDrawNode> m_barDraw;      // fully drawn volume bar: track + colored fill (expanded only)
    float m_barAlpha = 0.f;                         // fade factor for the drawn bar (baked into vertex alpha)
    cocos2d::CCScene* m_attachedScene = nullptr;    // weak (Cocos retains parent; null when on OverlayManager)
};

// True if a volume-scroll keybind (music/sfx, current game/editor context) is
// currently held. Re-syncs modifiers from the OS first (Windows). Smooth-scroll
// queries this to skip smoothing on volume-scroll gestures (otherwise it replays
// one wheel tick as many momentum steps, jumping the volume to the top).
bool isVolumeGestureActive();

} // namespace paimon::volscroll

// Helpers to init/shutdown the ticker (registered with the global scheduler).
void initVolumeScrollTicker();
void shutdownVolumeScrollTicker();
