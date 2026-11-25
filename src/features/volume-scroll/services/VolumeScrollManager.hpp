#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/NineSlice.hpp>
#include <array>

namespace paimon::volscroll {

enum class VolumeKind {
    Music,  // ctrl + scroll
    SFX     // shift + scroll
};

// Classic GD volume HUD overlay anchored to the bottom-right corner.
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

    enum class Phase {
        Hidden,
        Entering,
        Visible,
        Exiting
    };

    void ensureOverlayBuilt();
    void attachToRunningScene();
    void detachFromScene();
    void refreshChannelVisuals();
    void applyVisualPose(float dt);
    void resetAutoHideTimer();

    float readVolume(VolumeKind kind) const;
    void  writeVolume(VolumeKind kind, float value);

    Phase m_phase = Phase::Hidden;
    VolumeKind m_currentKind = VolumeKind::Music;
    float m_phaseProgress = 0.f;    // 0 -> 1 on Entering, 1 -> 0 on Exiting
    float m_visibleTimer = 0.f;     // seconds left before Exiting
    float m_displayedVolume = 0.f;  // smoothly interpolated volume
    float m_targetVolume = 0.f;     // authoritative target volume
    float m_kindBlend = 0.f;        // 0 = Music palette, 1 = SFX palette

    // Smooth icon cross-fade weights (Music, SFX, Mute).
    float m_musicIconAlpha = 1.f;
    float m_sfxIconAlpha = 0.f;
    float m_muteIconAlpha = 0.f;
    float m_muteBlend = 0.f;

    // Dynamic kinetic impulses (all smoothly damped inside update).
    float m_tickEnergy = 0.f;       // 0..1 lively pulse on scroll ticks
    float m_tickDir = 0.f;          // +1 up, -1 down
    float m_swapPulse = 0.f;        // 0..1 crisp transition pulse on channel/mute change
    float m_limitRecoil = 0.f;      // 0..1 corner recoil when hitting 0% / 100%
    float m_limitDir = 1.f;
    bool  m_sparkleActive = false;
    float m_sparkleTimer = 0.f;
    int m_lastDisplayedPct = -1;

    float m_barArtH = 20.f;
    float m_barScale = 0.5f;
    float m_thumbBaseScale = 0.5f;
    float m_musicIconBaseScale = 0.5f;
    float m_sfxIconBaseScale = 0.5f;
    float m_muteIconBaseScale = 0.5f;

    geode::Ref<cocos2d::CCNode> m_overlay;
    geode::Ref<geode::NineSlice> m_shadow;
    geode::Ref<geode::NineSlice> m_panel;
    geode::Ref<geode::NineSlice> m_innerWell;

    geode::Ref<cocos2d::CCLabelBMFont> m_titleLabel;
    geode::Ref<cocos2d::CCLabelBMFont> m_pctLabel;
    geode::Ref<cocos2d::extension::CCScale9Sprite> m_barGroove;
    geode::Ref<cocos2d::extension::CCScale9Sprite> m_barFill;
    geode::Ref<cocos2d::CCSprite> m_barThumb;

    geode::Ref<cocos2d::CCNode> m_iconSlot;
    geode::Ref<geode::NineSlice> m_iconFrame;
    geode::Ref<cocos2d::CCSprite> m_musicIcon;
    geode::Ref<cocos2d::CCSprite> m_sfxIcon;
    geode::Ref<cocos2d::CCSprite> m_muteIcon;

    std::array<geode::Ref<cocos2d::CCSprite>, 2> m_sparkles;
    cocos2d::CCScene* m_attachedScene = nullptr;
};

// true while a volume-scroll bind is held, so smooth-scroll doesn't replay
// one wheel tick as momentum.
bool isVolumeGestureActive();

}

void initVolumeScrollTicker();
void shutdownVolumeScrollTicker();
