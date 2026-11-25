#include "VolumeScrollManager.hpp"

#include "../../../utils/SpriteHelper.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/OverlayManager.hpp>
#include <algorithm>
#include <cmath>
#include <string>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::volscroll {

// Parented to OverlayManager so it renders above scenes and popups, below the custom cursor.
namespace {

constexpr float kPi = 3.14159265358979323846f;

// Classic GD HUD card geometry (origin is card center), pinned to bottom-right corner.
constexpr float kCardH            = 48.f;
constexpr float kCardW            = 212.f;
constexpr float kIconFrameSize    = 34.f;
constexpr float kIconGlyphFit     = 21.f;

constexpr float kCornerMarginX    = 10.f;
constexpr float kCornerMarginY    = 10.f;
constexpr float kEnterSlideX      = 56.f;
constexpr float kEnterSlideY      = 10.f;
constexpr float kExitSlideX       = 52.f;
constexpr float kExitSlideY       = 8.f;

constexpr float kContentRightPad  = 11.f;
constexpr float kHeaderY          = 8.5f;
constexpr float kBarY             = -8.0f;
constexpr float kBarVisualH       = 9.5f;
constexpr float kBarInset         = 2.0f;
constexpr float kThumbDiameter    = 14.0f;

constexpr float kTitleBaseScale   = 0.41f;
constexpr float kPctBaseScale     = 0.35f;

// Dynamic, fluid corner-sweep timings.
constexpr float kEnterDuration    = 0.34f;
constexpr float kAutoHideTime     = 1.45f;
constexpr float kExitDuration     = 0.30f;
constexpr float kSparkleDuration  = 0.62f;
constexpr float kMaxAnimDt        = 1.f / 20.f;

constexpr int kOverlayZOrder      = 999500;

// Classic GD palette: Music shifts coral -> sky cyan -> GD lime; SFX shifts coral -> orange -> GD gold.
constexpr ccColor3B kLowColor       = {255, 105,  90};
constexpr ccColor3B kMusicMidColor  = { 75, 215, 255};
constexpr ccColor3B kMusicHighColor = {115, 255, 125};
constexpr ccColor3B kSfxMidColor    = {255, 175,  60};
constexpr ccColor3B kSfxHighColor   = {255, 238,  95};
constexpr ccColor3B kMutedRed       = {255, 115, 115};

inline float clamp01(float v) {
    return std::clamp(v, 0.f, 1.f);
}

inline float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

inline float subRange(float t, float start, float end) {
    if (end <= start) return t >= end ? 1.f : 0.f;
    return clamp01((t - start) / (end - start));
}

inline GLubyte toByte(float alpha) {
    return static_cast<GLubyte>(clamp01(alpha) * 255.f + 0.5f);
}

inline ccColor3B mixColor(ccColor3B a, ccColor3B b, float t) {
    t = clamp01(t);
    return {
        static_cast<GLubyte>(lerp(static_cast<float>(a.r), static_cast<float>(b.r), t) + 0.5f),
        static_cast<GLubyte>(lerp(static_cast<float>(a.g), static_cast<float>(b.g), t) + 0.5f),
        static_cast<GLubyte>(lerp(static_cast<float>(a.b), static_cast<float>(b.b), t) + 0.5f),
    };
}

// Crisp exponential-like glide with subtle dynamic overshoot (~2.8%) for energetic yet smooth arrival.
inline float easeOutDynamic(float t) {
    t = clamp01(t);
    constexpr float c1 = 0.72f;
    constexpr float c3 = c1 + 1.f;
    float const u = t - 1.f;
    return 1.f + c3 * u * u * u + c1 * u * u;
}

inline float easeOutQuint(float t) {
    float const u = 1.f - clamp01(t);
    float const u2 = u * u;
    return 1.f - u2 * u2 * u;
}

inline float easeOutQuart(float t) {
    float const u = 1.f - clamp01(t);
    float const u2 = u * u;
    return 1.f - u2 * u2;
}

inline float easeOutCubic(float t) {
    float const u = 1.f - clamp01(t);
    return 1.f - u * u * u;
}

inline float easeInOutCubic(float t) {
    t = clamp01(t);
    if (t < 0.5f) return 4.f * t * t * t;
    float const p = -2.f * t + 2.f;
    return 1.f - 0.5f * p * p * p;
}

// Native GD GJ_progressBar_001.png 9-slice, dynamically sliced so texture packs stay crisp.
CCScale9Sprite* createGDProgressCapsule(float& outArtHeight) {
    constexpr char const* kBarFile = "GJ_progressBar_001.png";
    auto* tex = CCTextureCache::sharedTextureCache()->addImage(kBarFile, false);
    if (!tex) return nullptr;
    CCSize const sz = tex->getContentSize();
    if (sz.width <= 4.f || sz.height <= 4.f) return nullptr;
    outArtHeight = sz.height;
    float const capX = std::min(12.f, sz.width * 0.25f);
    float const capY = std::min(6.f, sz.height * 0.30f);
    return CCScale9Sprite::create(
        kBarFile,
        CCRectMake(0.f, 0.f, sz.width, sz.height),
        CCRectMake(capX, capY, std::max(1.f, sz.width - capX * 2.f), std::max(1.f, sz.height - capY * 2.f))
    );
}

void applyOpacityTree(CCNode* node, GLubyte opacity) {
    if (!node) return;
    if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(node)) {
        rgba->setOpacity(opacity);
    }
    if (auto* children = node->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(children)) {
            applyOpacityTree(child, opacity);
        }
    }
}

ccColor3B computeBarFillColor(float vol, float kindBlend) {
    vol = clamp01(vol);
    ccColor3B const midCol  = mixColor(kMusicMidColor,  kSfxMidColor,  kindBlend);
    ccColor3B const highCol = mixColor(kMusicHighColor, kSfxHighColor, kindBlend);
    if (vol < 0.25f) {
        return mixColor(kLowColor, midCol, vol / 0.25f);
    }
    return mixColor(midCol, highCol, (vol - 0.25f) / 0.75f);
}

float fitScale(CCSprite* spr, float maxExtent) {
    if (!spr) return 1.f;
    CCSize const sz = spr->getContentSize();
    float const longest = std::max({sz.width, sz.height, 1.f});
    return maxExtent / longest;
}

} // namespace


VolumeScrollManager& VolumeScrollManager::get() {
    static VolumeScrollManager s_instance;
    return s_instance;
}

void VolumeScrollManager::init() {
    m_phase = Phase::Hidden;
    m_phaseProgress = 0.f;
    m_visibleTimer = 0.f;
    m_tickEnergy = 0.f;
    m_swapPulse = 0.f;
    m_limitRecoil = 0.f;
    m_sparkleActive = false;
    m_sparkleTimer = 0.f;
    m_lastDisplayedPct = -1;
}

void VolumeScrollManager::ensureOverlayBuilt() {
    if (m_overlay) return;

    // Origin (0, 0) is the visual center of the HUD card; keeping a zero content size
    // and (0, 0) anchor ensures children at (-kCardW/2..+kCardW/2) sit directly at
    // m_overlay's position (restX, restY) and scale/rotate around the card's center.
    auto* root = CCNode::create();
    root->setAnchorPoint({0.f, 0.f});
    root->setContentSize({0.f, 0.f});
    root->setID("paimon-volume-scroll-overlay"_spr);
    root->setVisible(false);

    // 1. Classic GD drop shadow (square02b_001.png).
    auto* shadow = paimon::SpriteHelper::safeCreateNineSliceFromFile("square02b_001.png");
    if (shadow) {
        shadow->setScaleMultiplier(0.45f);
        shadow->setAnchorPoint({0.5f, 0.5f});
        shadow->setPosition({0.f, -3.f});
        shadow->setContentSize({kCardW + 4.f, kCardH + 2.f});
        shadow->setColor({0, 0, 0});
        shadow->setOpacity(0);
        shadow->setID("paimon-vs-shadow"_spr);
        root->addChild(shadow, 0);
    }
    m_shadow = shadow;

    // 2. Classic GD blue beveled panel (GJ_square01.png).
    auto* panel = paimon::SpriteHelper::safeCreateNineSliceFromFile("GJ_square01.png");
    if (panel) {
        panel->setScaleMultiplier(0.54f);
        panel->setAnchorPoint({0.5f, 0.5f});
        panel->setPosition({0.f, 0.f});
        panel->setContentSize({kCardW, kCardH});
        panel->setID("paimon-vs-bg"_spr);
        root->addChild(panel, 1);
    }
    m_panel = panel;

    // 3. Recessed dark inner well (square02b_001.png) for clean contrast.
    auto* well = paimon::SpriteHelper::safeCreateNineSliceFromFile("square02b_001.png");
    if (well) {
        well->setScaleMultiplier(0.38f);
        well->setAnchorPoint({0.5f, 0.5f});
        well->setPosition({0.f, 0.f});
        well->setContentSize({kCardW - 12.f, kCardH - 12.f});
        well->setColor({0, 0, 0});
        well->setOpacity(0);
        well->setID("paimon-vs-well"_spr);
        root->addChild(well, 2);
    }
    m_innerWell = well;

    // 4. Right content zone (header, percentage, classic GD progress bar + slider knob).
    float const zoneLeft  = -kCardW * 0.5f + kCardH - 2.f;
    float const zoneRight =  kCardW * 0.5f - kContentRightPad;
    float const barLeft   = zoneLeft + 1.f;
    float const barRight  = zoneRight - 1.f;
    float const barW      = barRight - barLeft;

    auto* title = CCLabelBMFont::create("MUSIC", "goldFont.fnt");
    if (title) {
        title->setAnchorPoint({0.f, 0.5f});
        title->setScale(kTitleBaseScale);
        title->setPosition({zoneLeft, kHeaderY});
        title->setColor({255, 255, 255});
        title->setID("paimon-vs-title"_spr);
        root->addChild(title, 3);
    }
    m_titleLabel = title;

    auto* pct = CCLabelBMFont::create("0%", "bigFont.fnt");
    if (pct) {
        pct->setAnchorPoint({1.f, 0.5f});
        pct->setScale(kPctBaseScale);
        pct->setPosition({zoneRight, kHeaderY});
        pct->setColor({255, 255, 255});
        pct->setID("paimon-vs-pct"_spr);
        root->addChild(pct, 3);
    }
    m_pctLabel = pct;

    // Native GD progress bar groove & fill (GJ_progressBar_001.png).
    m_barArtH = 20.f;
    auto* groove = createGDProgressCapsule(m_barArtH);
    m_barScale = kBarVisualH / std::max(1.f, m_barArtH);
    if (groove) {
        groove->setAnchorPoint({0.f, 0.5f});
        groove->setScale(m_barScale);
        groove->setPosition({barLeft, kBarY});
        groove->setContentSize({barW / m_barScale, m_barArtH});
        groove->setColor({0, 0, 0});
        groove->setID("paimon-vs-bar-groove"_spr);
        root->addChild(groove, 3);
    }
    m_barGroove = groove;

    float unusedH = 20.f;
    auto* fill = createGDProgressCapsule(unusedH);
    if (fill) {
        fill->setAnchorPoint({0.f, 0.5f});
        fill->setScale(m_barScale);
        fill->setPosition({barLeft + kBarInset * m_barScale, kBarY});
        fill->setColor(kMusicMidColor);
        fill->setID("paimon-vs-bar-fill"_spr);
        root->addChild(fill, 4);
    }
    m_barFill = fill;

    // Classic GD slider knob (sliderthumb.png).
    auto* thumb = paimon::SpriteHelper::safeCreate("sliderthumb.png");
    if (thumb) {
        m_thumbBaseScale = fitScale(thumb, kThumbDiameter);
        thumb->setScale(m_thumbBaseScale);
        thumb->setPosition({barLeft + 5.5f, kBarY});
        thumb->setID("paimon-vs-thumb"_spr);
        root->addChild(thumb, 5);
    }
    m_barThumb = thumb;

    // 5. Classic GD framed icon badge on the left (GJ_square05.png + cross-fading native icons).
    auto* iconSlot = CCNode::create();
    iconSlot->setPosition({-kCardW * 0.5f + kCardH * 0.5f, 0.f});
    iconSlot->setID("paimon-vs-icon-slot"_spr);
    root->addChild(iconSlot, 4);
    m_iconSlot = iconSlot;

    auto* iconFrame = paimon::SpriteHelper::safeCreateNineSliceFromFile("GJ_square05.png");
    if (iconFrame) {
        iconFrame->setScaleMultiplier(0.38f);
        iconFrame->setContentSize({kIconFrameSize, kIconFrameSize});
        iconFrame->setAnchorPoint({0.5f, 0.5f});
        iconFrame->setID("paimon-vs-icon-frame"_spr);
        iconSlot->addChild(iconFrame, 0);
    }
    m_iconFrame = iconFrame;

    auto* musicSpr = paimon::SpriteHelper::createWithFallback(
        "GJ_musicOnBtn_001.png", "GJ_musicIcon_001.png");
    if (!musicSpr) musicSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_sMusicIcon_001.png");
    if (musicSpr) {
        m_musicIconBaseScale = fitScale(musicSpr, kIconGlyphFit);
        musicSpr->setScale(m_musicIconBaseScale);
        musicSpr->setID("paimon-vs-icon-music"_spr);
        iconSlot->addChild(musicSpr, 1);
    }
    m_musicIcon = musicSpr;

    auto* sfxSpr = paimon::SpriteHelper::createWithFallback(
        "GJ_audioOnBtn_001.png", "edit_eSFXBtn_001.png");
    if (!sfxSpr) sfxSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_musicOnBtn_001.png");
    if (sfxSpr) {
        m_sfxIconBaseScale = fitScale(sfxSpr, kIconGlyphFit);
        sfxSpr->setScale(m_sfxIconBaseScale);
        sfxSpr->setVisible(false);
        sfxSpr->setID("paimon-vs-icon-sfx"_spr);
        iconSlot->addChild(sfxSpr, 1);
    }
    m_sfxIcon = sfxSpr;

    auto* muteSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_audioOffBtn_001.png");
    if (muteSpr) {
        m_muteIconBaseScale = fitScale(muteSpr, kIconGlyphFit);
        muteSpr->setScale(m_muteIconBaseScale);
        muteSpr->setVisible(false);
        muteSpr->setID("paimon-vs-icon-mute"_spr);
        iconSlot->addChild(muteSpr, 2);
    }
    m_muteIcon = muteSpr;

    // 6. Classic GD corner star twinkle (GJ_sStar_001.png) on arrival & 100%.
    for (std::size_t i = 0; i < m_sparkles.size(); ++i) {
        auto* star = paimon::SpriteHelper::createWithFallback("GJ_sStar_001.png", "particle_01_001.png");
        if (star) {
            star->setColor({255, 236, 130});
            star->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
            star->setVisible(false);
            root->addChild(star, 6);
        }
        m_sparkles[i] = star;
    }

    m_overlay = root;
    m_lastDisplayedPct = -1;
    refreshChannelVisuals();
}

void VolumeScrollManager::refreshChannelVisuals() {
    if (!m_overlay) return;
    if (m_titleLabel) {
        m_titleLabel->setString(m_currentKind == VolumeKind::Music ? "MUSIC" : "SFX");
    }
}

void VolumeScrollManager::attachToRunningScene() {
    ensureOverlayBuilt();
    if (!m_overlay) return;

    if (auto* host = geode::OverlayManager::get()) {
        if (m_overlay->getParent() != host) {
            m_overlay->removeFromParent();
            host->addChild(m_overlay.data(), kOverlayZOrder);
        }
        m_attachedScene = nullptr;
    } else if (auto* scene = CCDirector::get()->getRunningScene()) {
        if (m_overlay->getParent() != scene) {
            m_overlay->removeFromParent();
            scene->addChild(m_overlay.data(), 99999);
        }
        m_attachedScene = scene;
    }
}

void VolumeScrollManager::detachFromScene() {
    if (m_overlay && m_overlay->getParent()) {
        m_overlay->removeFromParent();
    }
    m_attachedScene = nullptr;
}

void VolumeScrollManager::onSceneChange() {
    if (m_phase == Phase::Hidden) {
        if (m_overlay && m_overlay->getParent()) {
            m_overlay->removeFromParent();
            m_attachedScene = nullptr;
        }
        return;
    }
    if (!m_overlay || !m_overlay->getParent() || m_attachedScene) {
        attachToRunningScene();
    }
}

void VolumeScrollManager::releaseSharedResources() {
    detachFromScene();
    m_overlay = nullptr;
    m_shadow = nullptr;
    m_panel = nullptr;
    m_innerWell = nullptr;
    m_titleLabel = nullptr;
    m_pctLabel = nullptr;
    m_barGroove = nullptr;
    m_barFill = nullptr;
    m_barThumb = nullptr;
    m_iconSlot = nullptr;
    m_iconFrame = nullptr;
    m_musicIcon = nullptr;
    m_sfxIcon = nullptr;
    m_muteIcon = nullptr;
    for (auto& star : m_sparkles) {
        star = nullptr;
    }
    m_phase = Phase::Hidden;
    m_lastDisplayedPct = -1;
}

float VolumeScrollManager::readVolume(VolumeKind kind) const {
    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine) return 0.f;
    if (kind == VolumeKind::Music) {
        return std::clamp(engine->m_musicVolume, 0.f, 1.f);
    }
    return std::clamp(engine->m_sfxVolume, 0.f, 1.f);
}

void VolumeScrollManager::writeVolume(VolumeKind kind, float value) {
    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine) return;
    value = std::clamp(value, 0.f, 1.f);

    if (kind == VolumeKind::Music) {
        engine->m_musicVolume = value;
        engine->setBackgroundMusicVolume(value);
        if (engine->m_backgroundMusicChannel) {
            engine->m_backgroundMusicChannel->setVolume(value);
        }
    } else {
        engine->m_sfxVolume = value;
        engine->setEffectsVolume(value);
    }
}

void VolumeScrollManager::applyVisualPose(float /*dt*/) {
    if (!m_overlay) return;

    auto const winSize = CCDirector::get()->getWinSize();
    float const layoutScale = std::min(1.f, std::max(0.60f, (winSize.width - 20.f) / kCardW));
    float const scaledHalfW = (kCardW * 0.5f) * layoutScale;
    float const scaledHalfH = (kCardH * 0.5f) * layoutScale;

    // Pinned cleanly right inside the bottom-right corner.
    float const restX = winSize.width - kCornerMarginX - scaledHalfW;
    float const restY = kCornerMarginY + scaledHalfH;

    float slideX = 0.f;
    float slideY = 0.f;
    float cardScale = 1.f;
    float cardRot = 0.f;
    float alpha = 1.f;

    // Staggered inner choreography for dynamic kinetic depth.
    float iconShiftX = 0.f;
    float iconPopScale = 1.f;
    float iconTilt = 0.f;
    float headerShiftX = 0.f;
    float headerAlpha = 1.f;
    float barShiftX = 0.f;
    float barAlphaMul = 1.f;

    if (m_phase == Phase::Entering) {
        float const t = clamp01(m_phaseProgress);
        float const sweep = easeOutDynamic(t);
        float const glide = easeOutQuint(t);

        slideX    = kEnterSlideX * (1.f - sweep);
        slideY    = -kEnterSlideY * (1.f - glide);
        cardScale = lerp(0.88f, 1.f, easeOutQuart(t));
        cardRot   = 2.6f * (1.f - glide);
        alpha     = easeOutCubic(subRange(t, 0.f, 0.45f));

        float const iconT = easeOutDynamic(subRange(t, 0.04f, 0.78f));
        iconShiftX   = 10.f * (1.f - iconT);
        iconPopScale = lerp(0.82f, 1.f, iconT);
        iconTilt     = -7.f * (1.f - easeOutQuart(subRange(t, 0.04f, 0.78f)));

        float const headT = easeOutQuart(subRange(t, 0.10f, 0.80f));
        headerShiftX = 14.f * (1.f - headT);
        headerAlpha  = easeOutCubic(subRange(t, 0.08f, 0.62f));

        float const barT = easeOutQuart(subRange(t, 0.16f, 0.90f));
        barShiftX    = 18.f * (1.f - barT);
        barAlphaMul  = easeOutCubic(subRange(t, 0.14f, 0.70f));
    } else if (m_phase == Phase::Exiting) {
        float const t = clamp01(m_phaseProgress); // goes 1 -> 0
        float const out = 1.f - easeInOutCubic(t);

        slideX       = kExitSlideX * out;
        slideY       = -kExitSlideY * out;
        cardScale    = lerp(1.f, 0.88f, out);
        cardRot      = 2.2f * out;
        alpha        = easeInOutCubic(t);
        headerShiftX = 9.f * out;
        barShiftX    = 12.f * out;
        headerAlpha  = alpha;
        barAlphaMul  = alpha;
    }

    // Dynamic live scroll response: velocity-reactive thumb, subtle directional lift, corner recoil.
    float const kineticVel = std::clamp(std::fabs(m_targetVolume - m_displayedVolume) * 4.5f, 0.f, 1.f);
    float const pulseWave  = std::sin(clamp01(m_tickEnergy) * kPi);
    float const energy     = std::max(pulseWave, kineticVel * 0.65f);
    float const swapWave   = std::sin(clamp01(m_swapPulse) * kPi);

    float const dirKickY   = m_tickDir * 2.1f * pulseWave;
    float const recoilX    = m_limitDir * 3.4f * std::sin(clamp01(m_limitRecoil) * kPi * 2.f) * clamp01(m_limitRecoil);

    m_overlay->setPosition({restX + slideX + recoilX, restY + slideY + dirKickY});
    m_overlay->setScale(cardScale * layoutScale);
    m_overlay->setRotation(cardRot);
    m_overlay->setVisible(alpha > 0.003f);

    // 1. Layered classic GD card (shadow + GJ_square01 + dark inner well).
    if (m_shadow) {
        m_shadow->setOpacity(toByte(0.42f * alpha));
    }
    if (m_panel) {
        m_panel->setOpacity(toByte(alpha));
    }
    if (m_innerWell) {
        m_innerWell->setOpacity(toByte(0.35f * alpha));
    }

    // 2. Classic GD left icon slot with dynamic tilt & silky cross-fade.
    if (m_iconSlot) {
        m_iconSlot->setPosition({-kCardW * 0.5f + kCardH * 0.5f + iconShiftX, 0.f});
        m_iconSlot->setScale(iconPopScale * (1.f + 0.065f * pulseWave + 0.07f * swapWave));
        m_iconSlot->setRotation(iconTilt - m_tickDir * 5.0f * pulseWave);
    }
    if (m_iconFrame) {
        m_iconFrame->setColor(mixColor({255, 255, 255}, {255, 198, 198}, m_muteBlend));
        m_iconFrame->setOpacity(toByte(alpha));
    }

    auto applyIcon = [&](CCSprite* spr, float baseScale, float weight, bool tintMutedFallback) {
        if (!spr) return;
        float const effA = alpha * clamp01(weight);
        bool const vis = effA > 0.004f;
        spr->setVisible(vis);
        if (vis) {
            spr->setOpacity(toByte(effA));
            spr->setScale(baseScale * lerp(0.84f, 1.f, clamp01(weight)));
            if (tintMutedFallback) {
                spr->setColor(mixColor({255, 255, 255}, kMutedRed, m_muteBlend));
            }
        }
    };
    bool const fallbackTint = (m_muteIcon == nullptr);
    applyIcon(m_musicIcon.data(), m_musicIconBaseScale, m_musicIconAlpha, fallbackTint);
    applyIcon(m_sfxIcon.data(),   m_sfxIconBaseScale,   m_sfxIconAlpha,   fallbackTint);
    applyIcon(m_muteIcon.data(),  m_muteIconBaseScale,  m_muteIconAlpha,  false);

    // 3. Right content zone: header + percentage.
    float const zoneLeft  = -kCardW * 0.5f + kCardH - 2.f;
    float const zoneRight =  kCardW * 0.5f - kContentRightPad;
    float const textAlpha = alpha * clamp01(headerAlpha);

    if (m_titleLabel) {
        m_titleLabel->setPosition({zoneLeft + headerShiftX, kHeaderY});
        m_titleLabel->setScale(kTitleBaseScale * (1.f + 0.05f * swapWave));
        m_titleLabel->setColor(mixColor({255, 255, 255}, {255, 185, 185}, m_muteBlend));
        applyOpacityTree(m_titleLabel.data(), toByte(textAlpha));
    }

    int const pctInt = static_cast<int>(std::round(clamp01(m_displayedVolume) * 100.f));
    if (m_pctLabel) {
        if (pctInt != m_lastDisplayedPct) {
            m_lastDisplayedPct = pctInt;
            std::string const text = std::to_string(pctInt) + "%";
            m_pctLabel->setString(text.c_str());
        }
        m_pctLabel->setPosition({zoneRight + headerShiftX * 0.6f, kHeaderY});
        m_pctLabel->setScale(kPctBaseScale * (1.f + 0.11f * energy));

        ccColor3B pctColor{255, 255, 255};
        if (pctInt == 0) {
            pctColor = kMutedRed;
        } else if (pctInt >= 100) {
            pctColor = mixColor({155, 255, 130}, {255, 238, 110}, m_kindBlend);
        }
        m_pctLabel->setColor(pctColor);
        applyOpacityTree(m_pctLabel.data(), toByte(textAlpha));
    }

    // 4. Native GD progress bar (GJ_progressBar_001.png) + slider knob (sliderthumb.png).
    float const barLeft  = zoneLeft + 1.f + barShiftX;
    float const barRight = zoneRight - 1.f + barShiftX;
    float const barW     = barRight - barLeft;
    float const barAlpha = alpha * clamp01(barAlphaMul);

    if (m_barGroove) {
        m_barGroove->setPosition({barLeft, kBarY});
        m_barGroove->setOpacity(toByte(0.64f * barAlpha));
    }

    float const volClamped = clamp01(m_displayedVolume);
    if (m_barFill) {
        bool const showFill = (barAlpha > 0.004f && volClamped > 0.003f);
        m_barFill->setVisible(showFill);
        if (showFill) {
            float const innerH = std::max(4.f, m_barArtH - kBarInset * 2.f);
            float const trackArtW = std::max(innerH, barW / m_barScale - kBarInset * 2.f);
            m_barFill->setPosition({barLeft + kBarInset * m_barScale, kBarY});
            m_barFill->setContentSize({std::max(innerH, trackArtW * volClamped), innerH});
            m_barFill->setColor(computeBarFillColor(volClamped, m_kindBlend));
            m_barFill->setOpacity(toByte(barAlpha));
        }
    }

    if (m_barThumb) {
        float const capPad = 5.5f;
        float const minX = barLeft + capPad;
        float const maxX = std::max(minX, barRight - capPad);
        float const thumbX = lerp(minX, maxX, volClamped);
        m_barThumb->setPosition({thumbX, kBarY});
        m_barThumb->setScale(m_thumbBaseScale * (1.f + 0.20f * energy));
        m_barThumb->setColor(mixColor({255, 255, 255}, {215, 180, 180}, m_muteBlend));
        m_barThumb->setOpacity(toByte(barAlpha));
    }

    // 5. Classic GD corner star twinkle (GJ_sStar_001.png) on arrival & 100%.
    for (std::size_t i = 0; i < m_sparkles.size(); ++i) {
        auto* star = m_sparkles[i].data();
        if (!star) continue;
        float const normT = (m_sparkleActive && m_sparkleTimer >= 0.f)
            ? (m_sparkleTimer / kSparkleDuration)
            : -1.f;
        float const localT = (normT >= 0.f)
            ? subRange(normT, i == 0 ? 0.f : 0.16f, i == 0 ? 0.84f : 1.f)
            : 0.f;
        bool const active = (normT >= 0.f && localT > 0.001f && localT < 0.999f && alpha > 0.02f);
        star->setVisible(active);
        if (active) {
            float const env = std::sin(localT * kPi);
            float const sx = (i == 0 ? 1.f : -1.f) * (kCardW * 0.5f - 7.f);
            float const sy = (i == 0 ? 1.f : -1.f) * (kCardH * 0.5f - 6.f);
            star->setPosition({sx, sy});
            star->setScale(0.18f + 0.30f * env);
            star->setRotation(localT * (i == 0 ? 125.f : -110.f));
            star->setOpacity(toByte(0.84f * env * alpha));
        }
    }
}

void VolumeScrollManager::update(float dt) {
    float const safeDt = std::clamp(dt, 0.f, kMaxAnimDt);

    // Responsive, frame-rate independent volume, palette, and icon cross-fade interpolation.
    float const volRate = 1.f - std::exp(-18.f * safeDt);
    m_displayedVolume = lerp(m_displayedVolume, m_targetVolume, volRate);
    if (std::fabs(m_targetVolume - m_displayedVolume) < 0.0005f) {
        m_displayedVolume = m_targetVolume;
    }

    float const kindRate = 1.f - std::exp(-13.f * safeDt);
    m_kindBlend = lerp(m_kindBlend, (m_currentKind == VolumeKind::SFX) ? 1.f : 0.f, kindRate);

    bool const muted = (m_targetVolume <= 0.001f);
    float const iconRate = 1.f - std::exp(-15.f * safeDt);
    float const wantMute  = (muted && m_muteIcon != nullptr) ? 1.f : 0.f;
    float const wantMusic = (wantMute < 0.5f && m_currentKind == VolumeKind::Music) ? 1.f : 0.f;
    float const wantSfx   = (wantMute < 0.5f && m_currentKind == VolumeKind::SFX)   ? 1.f : 0.f;
    m_muteIconAlpha  = lerp(m_muteIconAlpha,  wantMute,  iconRate);
    m_musicIconAlpha = lerp(m_musicIconAlpha, wantMusic, iconRate);
    m_sfxIconAlpha   = lerp(m_sfxIconAlpha,   wantSfx,   iconRate);
    m_muteBlend      = lerp(m_muteBlend, muted ? 1.f : 0.f, iconRate);

    // Smooth decay of kinetic impulses.
    if (m_tickEnergy > 0.f) {
        m_tickEnergy = std::max(0.f, m_tickEnergy - safeDt * 4.6f);
    }
    if (m_swapPulse > 0.f) {
        m_swapPulse = std::max(0.f, m_swapPulse - safeDt * 4.0f);
    }
    if (m_limitRecoil > 0.f) {
        m_limitRecoil = std::max(0.f, m_limitRecoil - safeDt * 4.4f);
    }
    if (m_sparkleActive) {
        m_sparkleTimer += safeDt;
        if (m_sparkleTimer >= kSparkleDuration) {
            m_sparkleActive = false;
            m_sparkleTimer = 0.f;
        }
    }

    if (m_phase == Phase::Hidden) return;
    if (!m_overlay) {
        m_phase = Phase::Hidden;
        return;
    }

    switch (m_phase) {
        case Phase::Entering:
            m_phaseProgress += safeDt / kEnterDuration;
            if (m_phaseProgress >= 1.f) {
                m_phaseProgress = 1.f;
                m_phase = Phase::Visible;
                m_visibleTimer = kAutoHideTime;
            }
            break;

        case Phase::Visible:
            m_visibleTimer -= safeDt;
            if (m_visibleTimer <= 0.f) {
                m_phase = Phase::Exiting;
                m_phaseProgress = 1.f;
            }
            break;

        case Phase::Exiting:
            m_phaseProgress -= safeDt / kExitDuration;
            if (m_phaseProgress <= 0.f) {
                m_phaseProgress = 0.f;
                m_phase = Phase::Hidden;
                m_overlay->setVisible(false);
                detachFromScene();
                return;
            }
            break;

        case Phase::Hidden:
            return;
    }

    applyVisualPose(safeDt);
}

void VolumeScrollManager::resetAutoHideTimer() {
    m_visibleTimer = kAutoHideTime;
}

bool VolumeScrollManager::onScroll(VolumeKind kind, float delta) {
    float const current = readVolume(kind);
    float const next = std::clamp(current + delta, 0.f, 1.f);
    writeVolume(kind, next);

    bool const wasHidden = (m_phase == Phase::Hidden);
    bool const kindChanged = (kind != m_currentKind);
    bool const hitLimit = (std::fabs(next - current) < 0.0005f) && (std::fabs(delta) > 0.0001f);
    bool const muteToggled = ((current <= 0.001f) != (next <= 0.001f));
    bool const reachedMax = (current < 0.995f && next >= 0.995f);

    m_targetVolume = next;
    if (wasHidden || kindChanged) {
        m_currentKind = kind;
        m_displayedVolume = current;
    }

    attachToRunningScene();
    refreshChannelVisuals();

    // Snap visual states cleanly on cold entrance.
    if (wasHidden) {
        bool const muted = (next <= 0.001f);
        m_kindBlend      = (kind == VolumeKind::SFX) ? 1.f : 0.f;
        m_muteBlend      = muted ? 1.f : 0.f;
        m_muteIconAlpha  = (muted && m_muteIcon != nullptr) ? 1.f : 0.f;
        m_musicIconAlpha = (m_muteIconAlpha < 0.5f && kind == VolumeKind::Music) ? 1.f : 0.f;
        m_sfxIconAlpha   = (m_muteIconAlpha < 0.5f && kind == VolumeKind::SFX)   ? 1.f : 0.f;
    }

    // Dynamic feedback impulses.
    if (hitLimit) {
        m_limitRecoil = 1.f;
        m_limitDir = (delta > 0.f) ? 1.f : -1.f;
    } else if (!wasHidden) {
        m_tickEnergy = 1.f;
        m_tickDir = (delta >= 0.f) ? 1.f : -1.f;
    }

    if (!wasHidden && (kindChanged || muteToggled)) {
        m_swapPulse = 1.f;
    }
    if (reachedMax) {
        m_sparkleActive = true;
        m_sparkleTimer = 0.f;
    }

    switch (m_phase) {
        case Phase::Hidden:
            m_phase = Phase::Entering;
            m_phaseProgress = 0.f;
            m_sparkleActive = true;
            m_sparkleTimer = -0.14f;
            break;

        case Phase::Exiting: {
            float const currentVis = easeInOutCubic(m_phaseProgress);
            m_phase = Phase::Entering;
            m_phaseProgress = std::clamp(currentVis, 0.08f, 0.85f);
            break;
        }

        case Phase::Entering:
            break;

        case Phase::Visible:
            resetAutoHideTimer();
            break;
    }

    applyVisualPose(0.f);
    return true;
}

} // namespace paimon::volscroll
