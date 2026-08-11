#include "VolumeScrollManager.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/OverlayManager.hpp>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::volscroll {

// Bottom-right volume chip/panel hosted above scenes and popups.
// The overlay is parented to geode::OverlayManager — the same top-most host the custom cursor
// uses — so it renders above every scene, popup and transition (the cursor sits at INT_MAX and
// we stay just below it).

namespace {
    // Dimensions — chip vs expanded.
    constexpr float kPanelHeight   =  34.f;
    constexpr float kPanelWidthMin =  56.f;
    constexpr float kPanelWidthMax = 168.f;

    constexpr float kMarginRight  = 210.f;
    constexpr float kMarginBottom =  22.f;

    constexpr float kSlideInTime  = 0.25f;
    constexpr float kExpandTime   = 0.30f;
    constexpr float kAutoHideTime = 1.30f;
    constexpr float kCollapseTime = 0.20f;
    constexpr float kSlideOutTime = 0.22f;

    constexpr float kSlideOffset = 50.f;

    constexpr float kVolumeLerpSpeed = 22.f;

    constexpr int kOverlayZOrder = 999500;

    constexpr float kPctLeftPad  = 12.f;
    constexpr float kBarCenterX  = 83.f;
    constexpr float kBarWidth    = 74.f;
    constexpr float kChipRightPad = 10.f;

    constexpr float kTrackH   = 12.f;
    constexpr float kBarFillH =  9.f;

    constexpr ccColor3B kMusicColor{160, 220, 255};
    constexpr ccColor3B kSfxColor{255, 200, 140};

    constexpr ccColor3B kBandColors[4] = {
        {255,  85,  85},
        {255, 165,  60},
        {255, 225,  70},
        {110, 225, 110},
    };

    // easeOutBack: slight overshoot then settle — the GD popup feel. Softened
    // c1 so the slide/width never overshoots more than ~6%.
    inline float easeOutBack(float t) {
        t = std::clamp(t, 0.f, 1.f);
        constexpr float c1 = 1.2f;
        constexpr float c3 = c1 + 1.f;
        const float u = t - 1.f;
        return 1.f + c3 * u * u * u + c1 * u * u;
    }

    // easeInQuad: smooth acceleration → exits leave without an abrupt cut.
    inline float easeInQuad(float t) {
        t = std::clamp(t, 0.f, 1.f);
        return t * t;
    }

    // easeOutQuint: very gradual deceleration → used for the extras fade-in.
    inline float easeOutQuint(float t) {
        t = std::clamp(t, 0.f, 1.f);
        float u = 1.f - t;
        return 1.f - u * u * u * u * u;
    }

    inline float easeInOutQuad(float t) {
        t = std::clamp(t, 0.f, 1.f);
        return t < 0.5f ? 2.f * t * t : 1.f - 0.5f * (2.f * t - 2.f) * (2.f * t - 2.f);
    }
}


VolumeScrollManager& VolumeScrollManager::get() {
    static VolumeScrollManager s_instance;
    return s_instance;
}

void VolumeScrollManager::init() {
    m_state = State::Hidden;
    m_animProgress = 0.f;
    m_expandProgress = 0.f;
    m_visibleTimer = 0.f;
    m_clock = 0.f;
    m_lastUseClock = -100.f;
}


void VolumeScrollManager::ensureOverlayBuilt() {
    if (m_overlay) return;

    auto container = CCLayerRGBA::create();
    container->setContentSize({kPanelWidthMin, kPanelHeight});
    container->setAnchorPoint({1.f, 0.f}); // bottom-right → expands leftward, right edge pinned on screen
    container->setID("paimon-volume-scroll-overlay"_spr);
    container->setCascadeOpacityEnabled(true);
    container->setCascadeColorEnabled(true);
    container->setTouchEnabled(false);

    auto bg = CCScale9Sprite::create("GJ_square01.png");
    if (bg) {
        bg->setContentSize({kPanelWidthMin, kPanelHeight});
        bg->setPosition({kPanelWidthMin * 0.5f, kPanelHeight * 0.5f});
        bg->setID("paimon-vs-bg"_spr);
        container->addChild(bg, 0);
    }
    m_pillNode = bg;

    // MUS/SFX chip — goldFont like GD titles, tinted by kind. Anchored to the
    // container's right edge so it drifts right as the panel expands.
    auto icon = CCLabelBMFont::create("MUS", "goldFont.fnt");
    icon->setScale(0.42f);
    icon->setAnchorPoint({1.f, 0.5f});
    icon->setPosition({kPanelWidthMin - kChipRightPad, kPanelHeight * 0.5f});
    icon->setColor(kMusicColor);
    icon->setID("paimon-vs-icon"_spr);
    container->addChild(icon, 3);
    m_iconLabel = icon;

    m_kindLabel = nullptr;

    auto fill = CCDrawNode::create();
    if (fill) {
        fill->setID("paimon-vs-bar-fill"_spr);
        fill->setVisible(false);
        container->addChild(fill, 2);
    }
    m_barDraw = fill;

    // Percentage label (only visible when expanded) — bigFont like GD percents.
    auto pctLabel = CCLabelBMFont::create("0%", "bigFont.fnt");
    pctLabel->setScale(0.32f);
    pctLabel->setAnchorPoint({0.f, 0.5f});
    pctLabel->setColor({255, 255, 255});
    pctLabel->setID("paimon-vs-pct"_spr);
    pctLabel->setOpacity(0);
    pctLabel->setVisible(false);
    container->addChild(pctLabel, 2);
    m_label = pctLabel;

    container->setOpacity(0);
    container->setVisible(false);

    m_overlay = container;
    redrawPill();
    redrawBar();
}


void VolumeScrollManager::redrawPill() {
    if (!m_pillNode || !m_overlay) return;
    auto* bg = static_cast<CCScale9Sprite*>(m_pillNode.data());
    const auto sz = m_overlay->getContentSize();
    bg->setContentSize(sz);
    bg->setPosition({sz.width * 0.5f, sz.height * 0.5f});
}

// Blend the four band colors at normalized position t.
static ccColor4F sampleBandColor(float t, float alpha) {
    t = std::clamp(t, 0.f, 1.f);
    const float p = t * 4.f - 0.5f;
    const int i0 = std::clamp(static_cast<int>(std::floor(p)), 0, 3);
    const int i1 = std::clamp(i0 + 1, 0, 3);
    const float f = std::clamp(p - static_cast<float>(i0), 0.f, 1.f);
    const auto& a = kBandColors[i0];
    const auto& b = kBandColors[i1];
    return {
        (a.r + (b.r - a.r) * f) / 255.f,
        (a.g + (b.g - a.g) * f) / 255.f,
        (a.b + (b.b - a.b) * f) / 255.f,
        alpha
    };
}


namespace {
    constexpr float kPi = 3.14159265358979f;
    constexpr int   kArc = 10;

    void fillCapsule(CCDrawNode* draw, float x0, float x1, float cy, float r, const ccColor4F& col) {
        if (x1 < x0) std::swap(x0, x1);
        CCPoint pts[(kArc + 1) * 2];
        int n = 0;
        for (int i = 0; i <= kArc; ++i) {
            const float a = -kPi * 0.5f + kPi * i / kArc;
            pts[n++] = ccp(x1 + r * std::cos(a), cy + r * std::sin(a));
        }
        for (int i = 0; i <= kArc; ++i) {
            const float a = kPi * 0.5f + kPi * i / kArc;
            pts[n++] = ccp(x0 + r * std::cos(a), cy + r * std::sin(a));
        }
        draw->drawPolygon(pts, n, col, 0.f, {0.f, 0.f, 0.f, 0.f});
    }

    void fillHalfCap(CCDrawNode* draw, float cx, float cy, float r, bool leftSide, const ccColor4F& col) {
        CCPoint pts[kArc + 1];
        const float a0 = leftSide ? kPi * 0.5f : -kPi * 0.5f;
        for (int i = 0; i <= kArc; ++i) {
            const float a = a0 + kPi * i / kArc;
            pts[i] = ccp(cx + r * std::cos(a), cy + r * std::sin(a));
        }
        draw->drawPolygon(pts, kArc + 1, col, 0.f, {0.f, 0.f, 0.f, 0.f});
    }
}


void VolumeScrollManager::redrawBar() {
    if (!m_barDraw) return;
    auto* draw = static_cast<CCDrawNode*>(m_barDraw.data());
    draw->clear();

    const float alpha = std::clamp(m_barAlpha, 0.f, 1.f);
    if (alpha <= 0.f) return;

    const float pct    = std::clamp(m_displayedVolume, 0.f, 1.f);
    const float cy     = kPanelHeight * 0.5f;
    const float trackR = kTrackH * 0.5f;
    const float left   = kBarCenterX - kBarWidth * 0.5f + trackR;
    const float right  = kBarCenterX + kBarWidth * 0.5f - trackR;
    const float span   = std::max(right - left, 0.f);

    fillCapsule(draw, left, right, cy, trackR, {0.f, 0.f, 0.f, 0.55f * alpha});

    if (pct <= 0.f || span <= 0.f) return;

    const float fillHalf = kBarFillH * 0.5f;
    const float fillEndX = left + span * pct;

    fillHalfCap(draw, left,     cy, fillHalf, true,  sampleBandColor(0.f, alpha));
    fillHalfCap(draw, fillEndX, cy, fillHalf, false, sampleBandColor(pct, alpha));

    const float step    = 2.f;
    const float overlap = 2.5f;
    for (float x = left; x < fillEndX; x += step) {
        const float x1 = std::min(x + step + overlap, fillEndX);
        const float t  = (((x + x1) * 0.5f) - left) / span;
        const ccColor4F col = sampleBandColor(t, alpha);
        CCPoint verts[4] = {
            {x, cy - fillHalf}, {x1, cy - fillHalf}, {x1, cy + fillHalf}, {x, cy + fillHalf}
        };
        draw->drawPolygon(verts, 4, col, 0.f, {0.f, 0.f, 0.f, 0.f});
    }
}


void VolumeScrollManager::applyExpandProgress() {
    if (!m_overlay) return;

    const float t = std::clamp(m_expandProgress, 0.f, 1.f);
    const float eased = (m_state == State::Collapsing) ? easeInQuad(t) : easeOutBack(t);

    const float w = kPanelWidthMin + (kPanelWidthMax - kPanelWidthMin) * eased;
    m_overlay->setContentSize({w, kPanelHeight});

    if (m_iconLabel) {
        m_iconLabel->setPosition({w - kChipRightPad, kPanelHeight * 0.5f});
    }

    const float extraT = std::clamp((t - 0.30f) / 0.70f, 0.f, 1.f);
    const float extraEased = easeOutQuint(extraT);
    const GLubyte extraOpacity = static_cast<GLubyte>(extraEased * 255.f);
    const bool extrasVisible = extraT > 0.f;

    if (m_label) {
        m_label->setVisible(extrasVisible);
        m_label->setOpacity(extraOpacity);
        m_label->setPosition({kPctLeftPad, kPanelHeight * 0.5f});
    }
    if (m_barDraw) {
        m_barDraw->setVisible(extrasVisible);
        m_barAlpha = extraEased;
        redrawBar();
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

    auto winSize = CCDirector::get()->getWinSize();
    m_overlay->setPosition({winSize.width - kMarginRight, kMarginBottom});
}

void VolumeScrollManager::detachFromScene() {
    if (m_overlay && m_overlay->getParent()) {
        m_overlay->removeFromParent();
    }
    m_attachedScene = nullptr;
}

void VolumeScrollManager::onSceneChange() {
    if (m_state != State::Hidden) {
        if (!m_overlay || !m_overlay->getParent() || m_attachedScene) {
            attachToRunningScene();
        }
    } else if (m_overlay && m_overlay->getParent()) {
        m_overlay->removeFromParent();
        m_attachedScene = nullptr;
    }
}

void VolumeScrollManager::releaseSharedResources() {
    detachFromScene();
    m_overlay = nullptr;
    m_iconLabel = nullptr;
    m_kindLabel = nullptr;
    m_label = nullptr;
    m_barDraw = nullptr;
    m_pillNode = nullptr;
    m_state = State::Hidden;
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


void VolumeScrollManager::update(float dt) {
    m_clock += dt;

    float diff = m_targetVolume - m_displayedVolume;
    m_displayedVolume += diff * std::clamp(kVolumeLerpSpeed * dt, 0.f, 1.f);

    if (m_state == State::Hidden) return;
    if (!m_overlay) {
        m_state = State::Hidden;
        return;
    }

    switch (m_state) {
        case State::SlidingIn:
            m_animProgress += dt / kSlideInTime;
            if (m_animProgress >= 1.f) {
                m_animProgress = 1.f;
                m_state = State::Expanding;
                m_expandProgress = 0.f;
            }
            break;

        case State::Expanding:
            m_expandProgress += dt / kExpandTime;
            if (m_expandProgress >= 1.f) {
                m_expandProgress = 1.f;
                m_state = State::Visible;
                m_visibleTimer = kAutoHideTime;
            }
            break;

        case State::Visible:
            m_visibleTimer -= dt;
            if (m_visibleTimer <= 0.f) {
                m_state = State::Collapsing;
            }
            break;

        case State::Collapsing:
            m_expandProgress -= dt / kCollapseTime;
            if (m_expandProgress <= 0.f) {
                m_expandProgress = 0.f;
                m_state = State::SlidingOut;
            }
            break;

        case State::SlidingOut:
            m_animProgress -= dt / kSlideOutTime;
            if (m_animProgress <= 0.f) {
                m_animProgress = 0.f;
                m_state = State::Hidden;
                m_overlay->setVisible(false);
                detachFromScene();
                return;
            }
            break;

        default: break;
    }

    applyExpandProgress();
    redrawPill();
    if (m_expandProgress > 0.f) {
        redrawBar();
    }

    // Vertical slide + container fade: entrance pops (easeOutBack), exit
    // accelerates away (easeInQuad) — same curve family as the expansion.
    const float slide = (m_state == State::SlidingOut)
                        ? easeInQuad(m_animProgress)
                        : easeOutBack(m_animProgress);

    const float opacity = easeInOutQuad(m_animProgress);

    const float y = kMarginBottom - kSlideOffset * (1.f - slide);
    auto winSize = CCDirector::get()->getWinSize();
    m_overlay->setPosition({winSize.width - kMarginRight, y});
    m_overlay->setOpacity(static_cast<GLubyte>(std::clamp(opacity, 0.f, 1.f) * 255.f));
    m_overlay->setVisible(true);

    if (m_label) {
        int pct = static_cast<int>(std::round(m_displayedVolume * 100.f));
        std::string s = std::to_string(pct) + "%";
        m_label->setString(s.c_str());
    }
}

void VolumeScrollManager::startSlideOut() {
    // Only used by cancel paths; the normal flow goes Collapsing -> SlidingOut. Kept for compatibility.
    if (m_state == State::SlidingOut || m_state == State::Hidden) return;
    m_state = State::Collapsing;
}

void VolumeScrollManager::resetAutoHideTimer() {
    m_visibleTimer = kAutoHideTime;
}

void VolumeScrollManager::rebuildContent() {
    if (!m_overlay) return;
    if (m_iconLabel) {
        m_iconLabel->setString(m_currentKind == VolumeKind::Music ? "MUS" : "SFX");
        m_iconLabel->setColor(m_currentKind == VolumeKind::Music ? kMusicColor : kSfxColor);
    }
    if (m_kindLabel) {
        m_kindLabel->setString(m_currentKind == VolumeKind::Music ? "Music" : "SFX");
    }
}


bool VolumeScrollManager::onScroll(VolumeKind kind, float delta) {
    float current = readVolume(kind);
    float next = std::clamp(current + delta, 0.f, 1.f);
    writeVolume(kind, next);
    m_targetVolume = next;
    if (m_state == State::Hidden) {
        m_displayedVolume = current;
    }

    if (kind != m_currentKind) {
        m_currentKind = kind;
        m_displayedVolume = next;
    }

    attachToRunningScene();
    rebuildContent();

    if (m_iconLabel && m_state != State::Hidden) {
        m_iconLabel->stopAllActions();
        m_iconLabel->runAction(CCSequence::create(
            CCScaleTo::create(0.06f, 0.48f),
            CCEaseOut::create(CCScaleTo::create(0.14f, 0.42f), 2.f),
            nullptr
        ));
    }

    switch (m_state) {
        case State::Hidden:
            m_state = State::SlidingIn;
            m_animProgress = 0.f;
            m_expandProgress = 0.f;
            break;

        case State::SlidingOut:
            m_state = State::SlidingIn;
            break;

        case State::Collapsing:
            // Was collapsing — expand again from the current progress.
            m_state = State::Expanding;
            break;

        case State::Visible:
            resetAutoHideTimer();
            break;

        case State::SlidingIn:
        case State::Expanding:
            break;
    }

    m_lastUseClock = m_clock;
    return true;
}

bool VolumeScrollManager::wasRecentlyUsed(float withinSeconds) const {
    return (m_clock - m_lastUseClock) < withinSeconds;
}

}
