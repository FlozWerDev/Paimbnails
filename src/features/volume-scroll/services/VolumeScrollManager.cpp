#include "VolumeScrollManager.hpp"
#include "../../../utils/PaimonDrawNode.hpp"

#include <Geode/Geode.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::volscroll {

// Two-phase bidirectional overlay animation: a compact MUS/SFX chip that expands into a wider
// pill (% + bar) growing symmetrically from a bottom-center anchor. There is no white
// "Music"/"SFX" label — only the colored chip (blue for music, orange for sfx).

namespace {
    // Dimensions — chip vs expanded.
    constexpr float kPanelHeight   =  26.f;
    constexpr float kPanelWidthMin =  46.f;  // chip MUS/SFX
    constexpr float kPanelWidthMax = 138.f;  // expanded
    constexpr float kCornerRadius  = kPanelHeight * 0.5f;

    // On-screen position (from the bottom-right corner): 24 base margin + 130 offset.
    constexpr float kMarginRight  = 154.f;
    constexpr float kMarginBottom =  22.f;

    // Animation timings — snappy in, organic expand, quick out
    constexpr float kSlideInTime  = 0.22f; // chip slides up (fast, snappy)
    constexpr float kExpandTime   = 0.38f; // chip widens (slower, organic)
    constexpr float kAutoHideTime = 1.30f; // time visible while expanded
    constexpr float kCollapseTime = 0.22f; // collapses (fast)
    constexpr float kSlideOutTime = 0.25f; // chip slides down (smooth)

    // Vertical slide distance
    constexpr float kSlideOffset = 50.f;

    constexpr float kVolumeLerpSpeed = 22.f;

    constexpr int   kCornerSegments = 10;
    constexpr float kPi = 3.14159265358979323846f;

    // Easing curves. easeOutQuart: smooth deceleration, no overshoot → fluid slide-in
    inline float easeOutQuart(float t) {
        t = std::clamp(t, 0.f, 1.f);
        float u = 1.f - t;
        return 1.f - u * u * u * u;
    }

    // easeOutQuint: very gradual deceleration → smooth expansion
    inline float easeOutQuint(float t) {
        t = std::clamp(t, 0.f, 1.f);
        float u = 1.f - t;
        return 1.f - u * u * u * u * u;
    }

    // easeInQuad: smooth acceleration → exit without an abrupt cut
    inline float easeInQuad(float t) {
        t = std::clamp(t, 0.f, 1.f);
        return t * t;
    }

    // easeInOutCubic: symmetric → smooth collapse
    inline float easeInOutCubic(float t) {
        t = std::clamp(t, 0.f, 1.f);
        return t < 0.5f ? 4.f * t * t * t : 1.f - 0.5f * (-2.f * t + 2.f) * (-2.f * t + 2.f) * (-2.f * t + 2.f);
    }

    // easeOutCubic: kept for the staggered extras fade
    inline float easeOutCubic(float t) {
        t = std::clamp(t, 0.f, 1.f);
        float u = 1.f - t;
        return 1.f - u * u * u;
    }

    // easeInOutQuad: smooth symmetric transition for opacity
    inline float easeInOutQuad(float t) {
        t = std::clamp(t, 0.f, 1.f);
        return t < 0.5f ? 2.f * t * t : 1.f - 0.5f * (2.f * t - 2.f) * (2.f * t - 2.f);
    }

    // Build a rounded-rect outline [0..w] x [0..h] with radius r (clamped to avoid artifacts).
    std::vector<CCPoint> buildPillOutline(float w, float h, float r) {
        r = std::min({r, w * 0.5f, h * 0.5f});
        std::vector<CCPoint> pts;
        pts.reserve(4 * kCornerSegments);

        auto addArc = [&](float cx, float cy, float startAngle) {
            for (int i = 0; i < kCornerSegments; ++i) {
                float a = startAngle + (kPi * 0.5f) *
                          (static_cast<float>(i) / static_cast<float>(kCornerSegments));
                pts.push_back({cx + cosf(a) * r, cy + sinf(a) * r});
            }
        };
        // BL, BR, TR, TL (CCW)
        addArc(r,     r,     kPi);
        addArc(w - r, r,     kPi * 1.5f);
        addArc(w - r, h - r, 0.f);
        addArc(r,     h - r, kPi * 0.5f);
        return pts;
    }
}

// Singleton

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

// Lazy overlay construction

void VolumeScrollManager::ensureOverlayBuilt() {
    if (m_overlay) return;

    auto container = CCLayerRGBA::create();
    container->setContentSize({kPanelWidthMin, kPanelHeight});
    container->setAnchorPoint({0.5f, 0.f}); // bottom-center → bidirectional expansion
    container->setID("paimon-volume-scroll-overlay"_spr);
    container->setZOrder(99999);
    container->setCascadeOpacityEnabled(true);
    container->setCascadeColorEnabled(true);
    container->setTouchEnabled(false);

    // Background pill (PaimonDrawNode, dynamic width)
    auto pill = PaimonDrawNode::create();
    pill->setPosition({0.f, 0.f});
    pill->setID("paimon-vs-pill"_spr);
    container->addChild(pill, 0);
    m_pillNode = pill;

    // MUS/SFX chip — the only type label, colored. Anchored to the container's right edge so it
    // shifts right as the container expands, freeing space on the left for the % and bar.
    auto icon = CCLabelBMFont::create("MUS", "bigFont.fnt");
    icon->setScale(0.34f);
    icon->setAnchorPoint({1.f, 0.5f});
    icon->setPosition({kPanelWidthMin - 12.f, kPanelHeight * 0.5f});
    icon->setColor({160, 220, 255});
    icon->setID("paimon-vs-icon"_spr);
    container->addChild(icon, 3);
    m_iconLabel = icon;

    // (No white "Music"/"SFX" label — m_kindLabel stays nullptr)
    m_kindLabel = nullptr;

    // Mini bar (rounded, two PaimonDrawNode: bg + fill)
    auto barBg = PaimonDrawNode::create();
    barBg->setID("paimon-vs-bar-bg"_spr);
    barBg->setOpacity(0);
    barBg->setVisible(false);
    container->addChild(barBg, 1);
    m_barBg = barBg;

    auto barFill = PaimonDrawNode::create();
    barFill->setID("paimon-vs-bar-fill"_spr);
    barFill->setOpacity(0);
    barFill->setVisible(false);
    container->addChild(barFill, 2);
    m_barFill = barFill;

    // Percentage label (only visible when expanded)
    auto pctLabel = CCLabelBMFont::create("0%", "bigFont.fnt");
    pctLabel->setScale(0.28f);
    pctLabel->setAnchorPoint({1.f, 0.5f});
    pctLabel->setColor({225, 225, 235});
    pctLabel->setID("paimon-vs-pct"_spr);
    pctLabel->setOpacity(0);
    pctLabel->setVisible(false);
    container->addChild(pctLabel, 2);
    m_label = pctLabel;

    // Initial state: invisible
    container->setOpacity(0);
    container->setVisible(false);

    m_overlay = container;
    redrawPill(); // initial render at min width
    redrawBar();
}

// Draw the background pill at the container's current width.

void VolumeScrollManager::redrawPill() {
    if (!m_pillNode || !m_overlay) return;

    auto* pill = static_cast<PaimonDrawNode*>(m_pillNode.data());
    pill->clear();

    const float w = m_overlay->getContentSize().width;
    const float h = m_overlay->getContentSize().height;
    const float r = kCornerRadius;

    auto pts = buildPillOutline(w, h, r);

    // Always a dark cool fill for legibility.
    const ccColor4F fill = ccc4FFromccc4B({18, 20, 32, 230});

    // Border tinted by active kind so music/sfx are distinguishable without reading the chip.
    const ccColor4F stroke = (m_currentKind == VolumeKind::Music)
        ? ccc4FFromccc4B({160, 220, 255, 160})  // blue (same as Music chip)
        : ccc4FFromccc4B({255, 200, 140, 160}); // orange (same as SFX chip)

    pill->drawPolygon(
        pts.data(),
        static_cast<unsigned int>(pts.size()),
        fill,
        0.6f,
        stroke
    );
}

// Draw the volume bar — rounded bg + rounded fill. Position/size come from
// applyExpandProgress; here we just render from each node's content size.

void VolumeScrollManager::redrawBar() {
    if (!m_barBg || !m_barFill) return;

    // Background: dark pill
    {
        auto* bg = static_cast<PaimonDrawNode*>(m_barBg.data());
        bg->clear();
        const auto sz = bg->getContentSize();
        if (sz.width > 1.f && sz.height > 0.5f) {
            float r = std::min(sz.height * 0.5f, sz.width * 0.5f);
            auto pts = buildPillOutline(sz.width, sz.height, r);
            const ccColor4F fillC = ccc4FFromccc4B({60, 60, 80, 220});
            bg->drawPolygon(
                pts.data(),
                static_cast<unsigned int>(pts.size()),
                fillC,
                0.f,
                ccc4f(0,0,0,0)
            );
        }
    }
    // Fill: pill tinted by level
    {
        auto* fill = static_cast<PaimonDrawNode*>(m_barFill.data());
        fill->clear();
        const auto sz = fill->getContentSize();
        if (sz.width > 1.f && sz.height > 0.5f) {
            float r = std::min(sz.height * 0.5f, sz.width * 0.5f);
            auto pts = buildPillOutline(sz.width, sz.height, r);

            ccColor3B c = {120, 200, 255};
            if (m_displayedVolume < 0.15f) c = {255, 130, 130};
            else if (m_displayedVolume > 0.85f) c = {180, 255, 180};

            const ccColor4F fillC = ccc4FFromccc4B({c.r, c.g, c.b, 240});
            fill->drawPolygon(
                pts.data(),
                static_cast<unsigned int>(pts.size()),
                fillC,
                0.f,
                ccc4f(0,0,0,0)
            );
        }
    }
}

// applyExpandProgress: interpolate the container width between chip and expanded panel
// (m_expandProgress in [0..1]) and update the chip position, extras opacity (staggered fade
// starting ~30% in), and the bar/% layout.

void VolumeScrollManager::applyExpandProgress() {
    if (!m_overlay) return;

    const float t = std::clamp(m_expandProgress, 0.f, 1.f);
    // Expand uses easeOutQuint; collapse uses easeInOutCubic.
    const float eased = (m_state == State::Collapsing) ? easeInOutCubic(t) : easeOutQuint(t);

    // Current container width; grows both ways thanks to the (0.5, 0) anchor.
    const float w = kPanelWidthMin + (kPanelWidthMax - kPanelWidthMin) * eased;
    m_overlay->setContentSize({w, kPanelHeight});

    // The chip stays at the right edge; since the container grows symmetrically, the chip drifts right on screen.
    if (m_iconLabel) {
        m_iconLabel->setPosition({w - 12.f, kPanelHeight * 0.5f});
    }

    // Extras opacity (% + bar): they fade in once the width is ~30% expanded.
    const float extraT = std::clamp((t - 0.30f) / 0.70f, 0.f, 1.f);
    const float extraEased = easeOutQuint(extraT);
    const GLubyte extraOpacity = static_cast<GLubyte>(extraEased * 255.f);
    const bool extrasVisible = extraT > 0.f;

    // Expanded layout — everything left of the chip: % at the left, bar just right of it.
    constexpr float kPctLeftPad   = 12.f;
    constexpr float kBarLeftAfter = 38.f;  // x where the bar starts
    constexpr float kBarWMax      = 56.f;
    constexpr float kBarH         = 5.f;
    const float barLeftX = kBarLeftAfter;
    const float barY     = (kPanelHeight - kBarH) * 0.5f;

    if (m_label) {
        m_label->setVisible(extrasVisible);
        m_label->setOpacity(extraOpacity);
        // % anchored left
        m_label->setAnchorPoint({0.f, 0.5f});
        m_label->setPosition({kPctLeftPad, kPanelHeight * 0.5f});
    }

    if (m_barBg) {
        auto* bg = m_barBg.data();
        bg->setVisible(extrasVisible);
        bg->setOpacity(extraOpacity);
        bg->setContentSize({kBarWMax, kBarH});
        bg->setPosition({barLeftX, barY});
    }

    if (m_barFill) {
        auto* fillNode = m_barFill.data();
        fillNode->setVisible(extrasVisible);
        fillNode->setOpacity(extraOpacity);
        const float fillW = kBarWMax * std::clamp(m_displayedVolume, 0.f, 1.f);
        fillNode->setContentSize({fillW, kBarH});
        fillNode->setPosition({barLeftX, barY});
    }
}

void VolumeScrollManager::attachToRunningScene() {
    auto* scene = CCDirector::get()->getRunningScene();
    if (!scene) return;
    if (m_attachedScene == scene && m_overlay && m_overlay->getParent() == scene) return;

    ensureOverlayBuilt();
    if (!m_overlay) return;

    if (m_overlay->getParent()) {
        m_overlay->removeFromParent();
    }

    auto winSize = CCDirector::get()->getWinSize();
    m_overlay->setPosition({winSize.width - kMarginRight, kMarginBottom});
    scene->addChild(m_overlay.data(), 99999);
    m_attachedScene = scene;
}

void VolumeScrollManager::detachFromScene() {
    if (m_overlay && m_overlay->getParent()) {
        m_overlay->removeFromParent();
    }
    m_attachedScene = nullptr;
}

void VolumeScrollManager::onSceneChange() {
    if (m_state != State::Hidden) {
        attachToRunningScene();
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
    m_barBg = nullptr;
    m_barFill = nullptr;
    m_pillNode = nullptr;
    m_state = State::Hidden;
}

// Volume read/write via FMODAudioEngine

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

// Update: advance the state machine.

void VolumeScrollManager::update(float dt) {
    m_clock += dt;

    // Lerp the displayed value.
    float diff = m_targetVolume - m_displayedVolume;
    m_displayedVolume += diff * std::clamp(kVolumeLerpSpeed * dt, 0.f, 1.f);

    if (m_state == State::Hidden) return;
    if (!m_overlay) {
        m_state = State::Hidden;
        return;
    }

    // State machine
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

    // Apply width/content per expandProgress
    applyExpandProgress();
    redrawPill();
    if (m_expandProgress > 0.f) {
        redrawBar();
    }

    // Apply the vertical slide + container fade (in: easeOutQuart, out: easeInQuad).
    const float slide = (m_state == State::SlidingOut)
                        ? easeInQuad(m_animProgress)
                        : easeOutQuart(m_animProgress);

    // Separate opacity: easeInOutQuad for a smoother fade than the slide
    const float opacity = easeInOutQuad(m_animProgress);

    const float y = kMarginBottom - kSlideOffset * (1.f - std::clamp(slide, 0.f, 1.f));
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
        m_iconLabel->setColor(m_currentKind == VolumeKind::Music
                              ? ccColor3B{160, 220, 255}
                              : ccColor3B{255, 200, 140});
    }
    if (m_kindLabel) {
        m_kindLabel->setString(m_currentKind == VolumeKind::Music ? "Music" : "SFX");
    }
}

// onScroll

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

    // Wake the overlay based on the current state.
    switch (m_state) {
        case State::Hidden:
            m_state = State::SlidingIn;
            m_animProgress = 0.f;
            m_expandProgress = 0.f;
            break;

        case State::SlidingOut:
            // Re-engage slide-in from where it was; the chip expands again.
            m_state = State::SlidingIn;
            break;

        case State::Collapsing:
            // Was collapsing — expand again from the current progress.
            m_state = State::Expanding;
            break;

        case State::Visible:
            // Restart the auto-hide timer.
            resetAutoHideTimer();
            break;

        case State::SlidingIn:
        case State::Expanding:
            // Already entering/expanding. Leave it.
            break;
    }

    m_lastUseClock = m_clock;
    return true;
}

bool VolumeScrollManager::wasRecentlyUsed(float withinSeconds) const {
    return (m_clock - m_lastUseClock) < withinSeconds;
}

} // namespace paimon::volscroll
