#include "NowPlayingToast.hpp"

#include "../../services/MenuMusicEffects.hpp"
#include "../../services/MenuMusicLibrary.hpp"
#include "../../services/MenuMusicPlayer.hpp"
#include "../../../editor-music/services/EditorMusicPlayer.hpp"

#include <Geode/binding/LevelEditorLayer.hpp>

#include "../../../../utils/PaimonDrawNode.hpp"
#include "../../../../utils/SpriteHelper.hpp"
#include "../../../../utils/TextureBudget.hpp"

#include <fmt/format.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <vector>

using namespace geode::prelude;

namespace paimon::menumusic {

namespace {

constexpr float kPi = 3.14159265358979323846f;

// classic GD banner geometry in local node space; origin (0, 0) is the card center.
constexpr float kCardH = 62.f;
constexpr float kMinCardW = 244.f;
constexpr float kMaxCardW = 336.f;
constexpr float kTopMargin = 8.f;

constexpr float kArtworkFrameSize = 44.f;  // GJ_square05 frame inside the 62px GD panel
constexpr float kCoverSize = 35.f;         // square album cover clipped cleanly inside GJ_square05
constexpr float kTextLeftPad = 5.f;        // gap between left artwork slot and text column

constexpr int kEqCount = 5;
constexpr float kEqBarWidth = 3.2f;
constexpr float kEqBarGap = 2.0f;
constexpr float kEqMaxHeight = 22.f;
constexpr float kEqMinHeight = 4.f;
constexpr float kEqGroupWidth =
    static_cast<float>(kEqCount) * kEqBarWidth + static_cast<float>(kEqCount - 1) * kEqBarGap;
constexpr float kEqRightPad = 14.f;
constexpr float kEqTextGap = 10.f;

constexpr float kProgressY = -kCardH * 0.5f + 10.5f;
constexpr float kProgressVisualH = 7.f;
constexpr float kRowGap = 2.2f;

constexpr float kDefaultWaitDur = 0.16f;
constexpr float kMaxAnimDt = 1.f / 20.f;

constexpr int kToastZOrder = 500;
constexpr int kPreviewZOrder = 10000;

const std::string kToastId = "now-playing-toast"_spr;

struct AnimationInfo {
    char const* id;
    char const* label;
};

constexpr AnimationInfo kAnimations[] = {
    {"expand",  "Expansion"},
    {"slide",   "Deslizar"},
    {"bounce",  "Rebote suave"},
    {"elastic", "Elastico"},
    {"fade",    "Desvanecer"},
    {"none",    "Ninguna"},
};
constexpr std::size_t kAnimationCount = sizeof(kAnimations) / sizeof(kAnimations[0]);
static_assert(kAnimationCount == static_cast<std::size_t>(ToastAnimation::Count),
    "every ToastAnimation needs an id and a label");

struct Timing {
    float in;
    float out;
};

Timing timingFor(ToastAnimation style) {
    switch (style) {
        case ToastAnimation::Expand:  return {0.66f, 0.46f};
        case ToastAnimation::Slide:   return {0.48f, 0.30f};
        case ToastAnimation::Bounce:  return {0.58f, 0.30f};
        case ToastAnimation::Elastic: return {0.62f, 0.34f};
        case ToastAnimation::Fade:    return {0.38f, 0.28f};
        case ToastAnimation::None:
        case ToastAnimation::Count:
            break;
    }
    return {0.f, 0.f};
}

float clamp01(float value) {
    return std::clamp(value, 0.f, 1.f);
}

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

float smoothstep(float edge0, float edge1, float x) {
    if (edge1 <= edge0) return x >= edge1 ? 1.f : 0.f;
    float const t = clamp01((x - edge0) / (edge1 - edge0));
    return t * t * (3.f - 2.f * t);
}

float subRange(float t, float start, float end) {
    if (end <= start) return t >= end ? 1.f : 0.f;
    return clamp01((t - start) / (end - start));
}

GLubyte toByte(float alpha) {
    return static_cast<GLubyte>(clamp01(alpha) * 255.f + 0.5f);
}

ccColor3B mixColor3B(ccColor3B a, ccColor3B b, float t) {
    t = clamp01(t);
    return {
        static_cast<GLubyte>(lerp(static_cast<float>(a.r), static_cast<float>(b.r), t) + 0.5f),
        static_cast<GLubyte>(lerp(static_cast<float>(a.g), static_cast<float>(b.g), t) + 0.5f),
        static_cast<GLubyte>(lerp(static_cast<float>(a.b), static_cast<float>(b.b), t) + 0.5f),
    };
}

float easeOutCubic(float t) {
    float const u = 1.f - clamp01(t);
    return 1.f - u * u * u;
}

float easeInCubic(float t) {
    t = clamp01(t);
    return t * t * t;
}

float easeOutQuart(float t) {
    float const u = 1.f - clamp01(t);
    float const u2 = u * u;
    return 1.f - u2 * u2;
}

float easeInQuart(float t) {
    t = clamp01(t);
    float const t2 = t * t;
    return t2 * t2;
}

float easeOutQuint(float t) {
    float const u = 1.f - clamp01(t);
    float const u2 = u * u;
    return 1.f - u2 * u2 * u;
}

float easeInQuint(float t) {
    t = clamp01(t);
    float const t2 = t * t;
    return t2 * t2 * t;
}

float easeInOutCubic(float t) {
    t = clamp01(t);
    if (t < 0.5f) return 4.f * t * t * t;
    float const p = -2.f * t + 2.f;
    return 1.f - 0.5f * p * p * p;
}

// analytical underdamped/critically-damped harmonic spring normalized to [0 -> 1].
float springOut(float t, float omega, float zeta) {
    t = clamp01(t);
    if (t <= 0.f) return 0.f;
    if (t >= 1.f) return 1.f;

    float raw = 0.f;
    if (zeta < 0.999f) {
        float const root = std::sqrt(1.f - zeta * zeta);
        float const wd = omega * root;
        float const env = std::exp(-zeta * omega * t);
        raw = 1.f - env * (std::cos(wd * t) + (zeta / root) * std::sin(wd * t));
    } else {
        float const env = std::exp(-omega * t);
        raw = 1.f - env * (1.f + omega * t);
    }

    float const tail = smoothstep(0.84f, 1.f, t);
    return raw + (1.f - raw) * tail;
}

float springVelocity(float t, float omega, float zeta) {
    constexpr float kEps = 0.008f;
    float const lo = std::max(0.f, t - kEps);
    float const hi = std::min(1.f, t + kEps);
    float const span = hi - lo;
    return span > 0.0001f ? (springOut(hi, omega, zeta) - springOut(lo, omega, zeta)) / span : 0.f;
}

// rounded rectangle used solely for invisible CCClippingNode stencils.
std::vector<CCPoint> roundedRectPoints(float width, float height, float radius, CCPoint center) {
    constexpr int kArcSegments = 8;
    float const r = std::clamp(radius, 0.f, std::min(width, height) * 0.5f);
    float const hx = std::max(0.f, width * 0.5f - r);
    float const hy = std::max(0.f, height * 0.5f - r);

    std::vector<CCPoint> points;
    points.reserve(static_cast<std::size_t>(4 * kArcSegments));

    auto addArc = [&](float cx, float cy, float start) {
        for (int i = 0; i < kArcSegments; ++i) {
            float const a = start + (kPi * 0.5f) * (static_cast<float>(i) / static_cast<float>(kArcSegments));
            points.push_back({center.x + cx + std::cos(a) * r, center.y + cy + std::sin(a) * r});
        }
    };
    addArc(-hx, -hy, kPi);
    addArc( hx, -hy, kPi * 1.5f);
    addArc( hx,  hy, 0.f);
    addArc(-hx,  hy, kPi * 0.5f);
    return points;
}

// native GD progress bar 9-slice, dynamically sized to stay texture-pack safe.
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

std::string upperCase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return text;
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

std::string prefixSetting() {
    std::string prefix = Mod::get()->getSettingValue<std::string>("menuLoopCustomPrefix");
    return prefix == "[Empty]" ? std::string() : prefix;
}

std::string subtitleFor(MusicTrack const& track) {
    std::string source;
    switch (track.source) {
        case TrackSource::Downloaded:   source = "Downloaded"; break;
        case TrackSource::Local:        source = "Local"; break;
        case TrackSource::GeometryDash: source = "Geometry Dash"; break;
        default: break;
    }
    if (track.artist.empty()) return source;
    return source.empty() ? track.artist : fmt::format("{} - {}", source, track.artist);
}

} // namespace

char const* toastAnimationId(ToastAnimation animation) {
    auto const index = static_cast<std::size_t>(animation);
    return index < kAnimationCount ? kAnimations[index].id : kAnimations[0].id;
}

char const* toastAnimationLabel(ToastAnimation animation) {
    auto const index = static_cast<std::size_t>(animation);
    return index < kAnimationCount ? kAnimations[index].label : kAnimations[0].label;
}

ToastAnimation toastAnimationFromId(std::string const& id) {
    for (std::size_t i = 0; i < kAnimationCount; ++i) {
        if (id == kAnimations[i].id) return static_cast<ToastAnimation>(i);
    }
    return ToastAnimation::Expand;
}

std::vector<std::string> toastAnimationLabels() {
    std::vector<std::string> labels;
    labels.reserve(kAnimationCount);
    for (auto const& animation : kAnimations) {
        labels.emplace_back(animation.label);
    }
    return labels;
}

struct NowPlayingToast::Pose {
    float y = 0.f;
    float width = kCardH;
    float scaleX = 1.f;
    float scaleY = 1.f;
    float alpha = 1.f;
    float contentProgress = 1.f; // drives per-row staggered reveal [0..1]
    float contentAlpha = 1.f;
    float contentShiftX = 0.f;
    float contentShiftY = 0.f;
    float eqReveal = 1.f;
    float progressAlpha = 1.f;
    float artScale = 1.f;
    float artTilt = 0.f;         // tactile tilt settle for the square GD artwork frame
    float sparkleT = -1.f;       // [0..1] classic GD corner star twinkle on arrival
};

NowPlayingToast::Options NowPlayingToast::readOptions() {
    auto* mod = Mod::get();
    Options options;
    options.style = toastAnimationFromId(mod->getSettingValue<std::string>("menuLoopNotificationStyle"));
    options.speed = static_cast<float>(mod->getSettingValue<double>("menuLoopNotificationSpeed"));
    options.intensity = static_cast<float>(mod->getSettingValue<double>("menuLoopNotificationIntensity"));
    options.holdSeconds = static_cast<float>(mod->getSettingValue<double>("menuLoopNotificationTime"));
    return options;
}

NowPlayingToast::Pose NowPlayingToast::poseAt(Phase phase, float t) const {
    Pose p;
    p.y = m_showY;
    p.width = m_cardW;

    if (phase == Phase::Wait) {
        p.y = m_hideY;
        p.width = (m_style == ToastAnimation::Expand) ? kCardH : m_cardW;
        p.alpha = 0.f;
        p.contentProgress = 0.f;
        p.contentAlpha = 0.f;
        p.eqReveal = 0.f;
        p.progressAlpha = 0.f;
        return p;
    }

    if (phase == Phase::Hold) {
        if (m_style != ToastAnimation::None) {
            float const env = smoothstep(0.f, 0.16f, t) * (1.f - smoothstep(0.84f, 1.f, t));
            float const hoverAmp = (0.50f + 0.80f * m_intensity) * env;
            p.y = m_showY + std::sin(m_holdClock * 2.5f) * hoverAmp;
        }
        return p;
    }

    if (phase == Phase::Done) {
        p.alpha = 0.f;
        p.contentAlpha = 0.f;
        p.eqReveal = 0.f;
        p.progressAlpha = 0.f;
        return p;
    }

    t = clamp01(t);
    float const k = m_intensity;
    bool const entering = (phase == Phase::In);

    switch (m_style) {
        case ToastAnimation::Expand: {
            // Classic GD square box drops from above -> fluidly expands into the full GD banner
            // -> staggered text & green equalizer reveal -> subtle classic GD star twinkle.
            if (entering) {
                float const tDrop = subRange(t, 0.f, 0.44f);
                float const dropZeta = 0.94f - 0.30f * k;
                float const dropPos = springOut(tDrop, 8.8f, dropZeta);
                float const dropVel = springVelocity(tDrop, 8.8f, dropZeta);

                p.y = lerp(m_hideY, m_showY, dropPos);
                p.alpha = smoothstep(0.f, 0.16f, t);

                float const basePop = lerp(0.58f, 1.f, easeOutQuart(tDrop));
                float const stretch = std::clamp(dropVel * 0.032f * k, -0.05f, 0.08f);
                float const landSquash = std::sin(tDrop * kPi) * (1.f - tDrop) * 0.05f * k;

                float const tExp = subRange(t, 0.26f, 0.90f);
                float const expZeta = 0.95f - 0.36f * k;
                float const expVal = springOut(tExp, 9.2f, expZeta);
                p.width = std::max(kCardH, lerp(kCardH, m_cardW, expVal));

                float const morphSqueeze = std::sin(tExp * kPi) * 0.045f * k;
                p.scaleX = basePop * (1.f - stretch + landSquash);
                p.scaleY = basePop * (1.f + stretch - landSquash - morphSqueeze);

                p.artScale = lerp(0.84f, 1.f, springOut(subRange(t, 0.05f, 0.65f), 10.f, 0.72f));
                p.artTilt = -8.f * (0.4f + 0.6f * k) * (1.f - easeOutQuart(subRange(t, 0.08f, 0.74f)));

                p.contentProgress = subRange(t, 0.42f, 0.94f);
                p.contentAlpha = smoothstep(0.40f, 0.56f, t);
                p.eqReveal = subRange(t, 0.52f, 0.95f);
                p.progressAlpha = smoothstep(0.56f, 0.88f, t);

                if (t > 0.52f) {
                    p.sparkleT = subRange(t, 0.52f, 1.f);
                }
            } else {
                float const tContent = subRange(t, 0.f, 0.34f);
                p.contentAlpha = 1.f - easeInCubic(tContent);
                p.contentShiftX = -10.f * easeInCubic(tContent);
                p.eqReveal = 1.f - easeInCubic(subRange(t, 0.f, 0.30f));
                p.progressAlpha = 1.f - smoothstep(0.f, 0.22f, t);

                float const tCollapse = subRange(t, 0.05f, 0.56f);
                float const swell = std::sin(subRange(tCollapse, 0.f, 0.22f) * kPi) * 4.5f * k;
                p.width = std::max(kCardH, lerp(m_cardW, kCardH, easeInOutCubic(tCollapse)) + swell);

                float const tLift = subRange(t, 0.42f, 1.f);
                float const dip = std::sin(subRange(tLift, 0.f, 0.26f) * kPi) * (2.2f + 2.8f * k);
                float const liftMotion = easeInQuart(tLift);
                p.y = lerp(m_showY, m_hideY, liftMotion) - dip;

                float const liftScale = lerp(1.f, 0.64f, easeInCubic(tLift));
                float const liftStretch = liftMotion * 0.08f * k;
                p.scaleX = liftScale * (1.f - liftStretch * 0.6f);
                p.scaleY = liftScale * (1.f + liftStretch);
                p.alpha = 1.f - smoothstep(0.62f, 1.f, t);
                p.artTilt = 6.f * easeInCubic(tCollapse);
            }
            break;
        }

        case ToastAnimation::Slide: {
            if (entering) {
                float const zeta = 0.92f - 0.34f * k;
                float const motion = springOut(t, 9.8f, zeta);
                float const vel = springVelocity(t, 9.8f, zeta);

                p.y = lerp(m_hideY, m_showY, motion);
                p.alpha = smoothstep(0.f, 0.20f, t);

                float const stretch = std::clamp(vel * 0.024f * k, -0.04f, 0.07f);
                p.scaleX = 1.f - stretch * 0.65f;
                p.scaleY = 1.f + stretch;

                p.artScale = lerp(0.84f, 1.f, easeOutQuart(subRange(t, 0.05f, 0.58f)));
                p.artTilt = -6.f * (0.4f + 0.6f * k) * (1.f - easeOutQuart(subRange(t, 0.06f, 0.70f)));

                p.contentProgress = subRange(t, 0.16f, 0.86f);
                p.contentAlpha = smoothstep(0.14f, 0.38f, t);
                p.contentShiftY = 6.f * (1.f - easeOutCubic(subRange(t, 0.16f, 0.72f)));
                p.eqReveal = subRange(t, 0.26f, 0.88f);
                p.progressAlpha = smoothstep(0.36f, 0.80f, t);

                if (t > 0.42f) {
                    p.sparkleT = subRange(t, 0.42f, 0.98f);
                }
            } else {
                float const dip = std::sin(subRange(t, 0.f, 0.26f) * kPi) * (1.8f + 3.0f * k);
                float const lift = easeInQuart(t);
                p.y = lerp(m_showY, m_hideY, lift) - dip;
                p.scaleX = 1.f - 0.04f * k * lift;
                p.scaleY = 1.f + 0.06f * k * lift;
                p.alpha = 1.f - smoothstep(0.45f, 1.f, t);
                p.contentAlpha = 1.f - smoothstep(0.10f, 0.65f, t);
                p.eqReveal = 1.f - smoothstep(0.f, 0.50f, t);
                p.progressAlpha = 1.f - smoothstep(0.f, 0.40f, t);
            }
            break;
        }

        case ToastAnimation::Bounce: {
            if (entering) {
                constexpr float kLand = 0.42f;
                float heightFrac = 0.f;
                float squash = 0.f;
                if (t < kLand) {
                    float const u = t / kLand;
                    heightFrac = 1.f - u * u;
                    squash = -0.055f * k * u * u;
                } else {
                    float const u = (t - kLand) / (1.f - kLand);
                    float const bounceAmp = 0.06f + 0.16f * k;
                    heightFrac = std::exp(-5.4f * u) * std::abs(std::sin(u * kPi * 2.f)) * bounceAmp;
                    squash = std::exp(-8.5f * u) * std::cos(u * kPi * 3.f) * (0.04f + 0.075f * k);
                }

                p.y = m_showY + (m_hideY - m_showY) * heightFrac;
                p.scaleX = 1.f + squash;
                p.scaleY = 1.f - squash * 0.92f;
                p.alpha = smoothstep(0.f, 0.16f, t);

                p.artScale = lerp(0.84f, 1.f, easeOutQuart(subRange(t, 0.10f, 0.60f)));
                p.artTilt = -7.f * (0.4f + 0.6f * k) * (1.f - easeOutCubic(subRange(t, 0.15f, 0.78f)));

                p.contentProgress = subRange(t, 0.24f, 0.88f);
                p.contentAlpha = smoothstep(0.22f, 0.44f, t);
                p.eqReveal = subRange(t, 0.34f, 0.92f);
                p.progressAlpha = smoothstep(0.42f, 0.84f, t);

                if (t > 0.46f) {
                    p.sparkleT = subRange(t, 0.46f, 1.f);
                }
            } else {
                float const crouch = std::sin(subRange(t, 0.f, 0.28f) * kPi) * (2.5f + 4.0f * k);
                float const squash = std::sin(subRange(t, 0.f, 0.28f) * kPi) * 0.055f * k;
                float const launch = easeInQuart(subRange(t, 0.12f, 1.f));
                p.y = lerp(m_showY, m_hideY, launch) - crouch;
                p.scaleX = 1.f + squash - 0.05f * k * launch;
                p.scaleY = 1.f - squash + 0.07f * k * launch;
                p.alpha = 1.f - smoothstep(0.50f, 1.f, t);
                p.contentAlpha = 1.f - smoothstep(0.15f, 0.65f, t);
                p.eqReveal = 1.f - smoothstep(0.f, 0.45f, t);
                p.progressAlpha = 1.f - smoothstep(0.f, 0.35f, t);
            }
            break;
        }

        case ToastAnimation::Elastic: {
            if (entering) {
                float const zeta = 0.86f - 0.42f * k;
                float const sScale = springOut(t, 10.5f, zeta);
                float const sWidth = springOut(subRange(t, 0.04f, 1.f), 9.6f, std::min(0.95f, zeta + 0.06f));

                float const drift = 8.f + 8.f * k;
                p.y = m_showY + drift * (1.f - easeOutQuart(subRange(t, 0.f, 0.55f)));
                p.width = std::max(kCardH, lerp(m_cardW * 0.64f, m_cardW, sWidth));

                p.scaleX = lerp(0.76f, 1.f, sScale);
                p.scaleY = lerp(0.84f, 1.f, springOut(t, 11.2f, std::min(0.96f, zeta + 0.10f)));
                p.alpha = smoothstep(0.f, 0.18f, t);

                p.artScale = lerp(0.74f, 1.f, springOut(subRange(t, 0.04f, 0.78f), 11.f, 0.62f));
                p.artTilt = -10.f * (0.4f + 0.6f * k) * (1.f - easeOutQuart(subRange(t, 0.f, 0.78f)));

                p.contentProgress = subRange(t, 0.18f, 0.88f);
                p.contentAlpha = smoothstep(0.16f, 0.36f, t);
                p.eqReveal = subRange(t, 0.28f, 0.90f);
                p.progressAlpha = smoothstep(0.38f, 0.84f, t);

                if (t > 0.44f) {
                    p.sparkleT = subRange(t, 0.44f, 1.f);
                }
            } else {
                float const pinch = easeInCubic(t);
                float const pop = std::sin(subRange(t, 0.f, 0.28f) * kPi) * 0.035f * k;
                p.y = m_showY + 8.f * pinch;
                p.width = std::max(kCardH, lerp(m_cardW, m_cardW * 0.70f, pinch));
                p.scaleX = (1.f + pop) * lerp(1.f, 0.82f, pinch);
                p.scaleY = (1.f + pop) * lerp(1.f, 0.86f, pinch);
                p.alpha = 1.f - smoothstep(0.18f, 1.f, t);
                p.contentAlpha = 1.f - smoothstep(0.f, 0.45f, t);
                p.eqReveal = 1.f - smoothstep(0.f, 0.40f, t);
                p.progressAlpha = 1.f - smoothstep(0.f, 0.35f, t);
            }
            break;
        }

        case ToastAnimation::Fade: {
            float const drift = 10.f + 12.f * k;
            if (entering) {
                float const motion = easeOutQuint(t);
                p.y = m_showY + drift * (1.f - motion);
                float const sc = lerp(0.95f - 0.02f * k, 1.f, motion);
                p.scaleX = sc;
                p.scaleY = sc;
                p.alpha = easeOutCubic(t);
                p.artScale = lerp(0.92f, 1.f, motion);
                p.contentProgress = subRange(t, 0.10f, 0.92f);
                p.contentAlpha = easeOutCubic(t);
                p.eqReveal = subRange(t, 0.18f, 0.94f);
                p.progressAlpha = smoothstep(0.25f, 0.85f, t);
            } else {
                float const motion = easeInQuint(t);
                p.y = m_showY + drift * motion;
                float const sc = lerp(1.f, 0.96f, motion);
                p.scaleX = sc;
                p.scaleY = sc;
                p.alpha = 1.f - easeInCubic(t);
                p.contentAlpha = p.alpha;
                p.eqReveal = p.alpha;
                p.progressAlpha = p.alpha;
            }
            break;
        }

        case ToastAnimation::None:
            if (!entering) {
                p.alpha = 0.f;
                p.contentAlpha = 0.f;
                p.eqReveal = 0.f;
                p.progressAlpha = 0.f;
            }
            break;

        case ToastAnimation::Count:
            break;
    }

    return p;
}

void NowPlayingToast::updateEqualizer(float dt, float eqReveal) {
    bool const inEditor = LevelEditorLayer::get() != nullptr
        && paimon::editormusic::EditorMusicPlayer::get().hasActiveTrack();
    bool const paused = inEditor
        ? !paimon::editormusic::EditorMusicPlayer::get().isPlaying()
        : MenuMusicPlayer::get().isPaused();
    float const livePeak = inEditor
        ? paimon::editormusic::EditorMusicPlayer::get().outputPeak()
        : MenuMusicEffects::get().outputPeak();

    constexpr std::array<float, kEqCount> kFreqA = {6.2f, 8.4f, 7.1f, 9.3f, 5.8f};
    constexpr std::array<float, kEqCount> kFreqB = {3.1f, 4.7f, 5.5f, 3.9f, 4.3f};
    constexpr std::array<float, kEqCount> kPhase = {0.4f, 1.7f, 2.9f, 4.2f, 5.5f};
    constexpr std::array<float, kEqCount> kProfile = {0.72f, 0.95f, 1.00f, 0.90f, 0.68f};

    float energySum = 0.f;
    float const smoothRate = std::min(1.f, dt * 16.f);

    for (std::size_t i = 0; i < m_eqLevels.size(); ++i) {
        float target = 0.14f;
        if (!paused) {
            float const w1 = 0.5f + 0.5f * std::sin(m_clock * kFreqA[i] + kPhase[i]);
            float const w2 = 0.5f + 0.5f * std::sin(m_clock * kFreqB[i] * 1.37f + kPhase[i] * 1.9f);
            float const beat = 0.5f + 0.5f * std::sin(m_clock * 3.6f);
            float const synth = (0.22f + 0.50f * w1 + 0.28f * w2) * (0.76f + 0.24f * beat);
            float const drive = livePeak > 0.01f ? lerp(synth, std::min(1.f, livePeak * 1.35f), 0.55f) : synth;
            target = std::clamp(drive * kProfile[i], 0.14f, 1.f);
        }
        m_eqLevels[i] = lerp(m_eqLevels[i], target, smoothRate);
        energySum += m_eqLevels[i];
    }

    float const rawEnergy = (energySum / static_cast<float>(m_eqLevels.size())) * clamp01(eqReveal);
    m_audioEnergy = lerp(m_audioEnergy, paused ? 0.06f : rawEnergy, std::min(1.f, dt * 10.f));
}

void NowPlayingToast::updateClipStencil(float width) {
    if (!m_clipStencil) return;
    if (std::fabs(width - m_lastClipW) <= 0.15f) return;
    m_lastClipW = width;
    m_clipStencil->clear();

    float const innerW = std::max(4.f, width - 14.f);
    float const innerH = kCardH - 12.f;
    auto pts = roundedRectPoints(innerW, innerH, 5.f, {0.f, 0.f});
    m_clipStencil->drawPolygon(
        pts.data(), static_cast<unsigned int>(pts.size()),
        {1.f, 1.f, 1.f, 1.f}, 0.f, {0.f, 0.f, 0.f, 0.f}
    );
}

void NowPlayingToast::applyPose(Pose const& pose, float dt, float progress) {
    float const alpha = clamp01(pose.alpha);
    float const curW = std::max(kCardH, pose.width);
    bool const inEditor = LevelEditorLayer::get() != nullptr
        && paimon::editormusic::EditorMusicPlayer::get().hasActiveTrack();
    bool const paused = inEditor
        ? !paimon::editormusic::EditorMusicPlayer::get().isPlaying()
        : MenuMusicPlayer::get().isPaused();

    this->setPosition({m_centerX, pose.y});
    this->setScaleX(pose.scaleX * m_layoutScale);
    this->setScaleY(pose.scaleY * m_layoutScale);

    // 1. Classic GD GJ_square01 panel + square02b drop shadow + dark inner well.
    if (m_shadow) {
        m_shadow->setContentSize({curW + 4.f, kCardH + 2.f});
        m_shadow->setOpacity(toByte(0.42f * alpha));
    }
    if (m_panel) {
        m_panel->setContentSize({curW, kCardH});
        m_panel->setOpacity(toByte(alpha));
    }
    if (m_innerWell) {
        float const wellW = std::max(14.f, curW - 14.f);
        float const openRatio = smoothstep(kCardH * 1.08f, m_cardW * 0.65f, curW);
        m_innerWell->setContentSize({wellW, kCardH - 14.f});
        m_innerWell->setOpacity(toByte(0.32f * alpha * openRatio));
    }

    updateClipStencil(curW);

    // 2. Classic GD square artwork slot (GJ_square05 + cover or GJ_musicOnBtn).
    if (m_hasCover && m_coverBlend < 1.f) {
        m_coverBlend = std::min(1.f, m_coverBlend + dt * 5.0f);
    }
    if (m_artwork) {
        float const artX = -curW * 0.5f + kCardH * 0.5f;
        float const beatPulse = paused ? 1.f : (1.f + 0.022f * m_audioEnergy);
        m_artwork->setPosition({artX, 0.f});
        m_artwork->setScale(pose.artScale * beatPulse);
        m_artwork->setRotation(pose.artTilt);
    }
    if (m_artworkFrame) {
        m_artworkFrame->setOpacity(toByte(alpha));
    }
    if (m_coverSprite) {
        m_coverSprite->setOpacity(toByte(alpha * m_coverBlend));
    }
    if (m_fallbackIcon) {
        bool const showFb = m_coverBlend < 0.999f;
        m_fallbackIcon->setVisible(showFb);
        if (showFb) {
            m_fallbackIcon->setOpacity(toByte(alpha * (1.f - m_coverBlend)));
            float const iconBounce = paused ? 1.f : (1.f + 0.05f * m_audioEnergy);
            m_fallbackIcon->setScale(m_fallbackBaseScale * iconBounce);
        }
    }

    // 3. Staggered GD typography (anchored to moving left edge so it never overlaps artwork).
    float const leftEdgeShiftX = -(curW - m_cardW) * 0.5f;
    float const globalContent = clamp01(pose.contentAlpha) * alpha;
    for (auto const& row : m_rows) {
        if (!row.label) continue;
        float const rowT = easeOutQuart(subRange(pose.contentProgress, row.revealStart, row.revealEnd));
        float const rowAlpha = globalContent * rowT;
        float const inv = 1.f - rowT;
        row.label->setPosition({
            row.basePos.x + leftEdgeShiftX + row.slideX * inv + pose.contentShiftX,
            row.basePos.y + row.slideY * inv + pose.contentShiftY,
        });
        applyOpacityTree(row.label, toByte(rowAlpha));

        if (row.hasPrefixIcon && m_prefixIcon) {
            m_prefixIcon->setPosition({
                m_prefixIconBase.x + leftEdgeShiftX + row.slideX * inv + pose.contentShiftX,
                m_prefixIconBase.y + row.slideY * inv + pose.contentShiftY,
            });
            m_prefixIcon->setOpacity(toByte(rowAlpha));
        }
    }

    // 4. Classic GD GJ_progressBar_001 along the bottom of the text/right zone.
    float const barLeft = -curW * 0.5f + kCardH + kTextLeftPad;
    float const barRight = curW * 0.5f - kEqRightPad;
    float const barW = barRight - barLeft;
    float const widthOpen = smoothstep(kCardH * 1.45f, m_cardW * 0.82f, curW);
    float const barAlpha = clamp01(pose.progressAlpha) * alpha * widthOpen;

    bool const showBar = (barAlpha > 0.004f && barW > 18.f);
    if (m_progressGroove) {
        m_progressGroove->setVisible(showBar);
        if (showBar) {
            m_progressGroove->setPosition({barLeft, kProgressY});
            m_progressGroove->setContentSize({barW / m_progressScale, m_progressArtH});
            m_progressGroove->setOpacity(toByte(0.58f * barAlpha));
        }
    }
    if (m_progressFill) {
        float const clampedProg = clamp01(progress);
        bool const showFill = showBar && clampedProg > 0.004f;
        m_progressFill->setVisible(showFill);
        if (showFill) {
            constexpr float kInset = 2.2f;
            float const innerH = std::max(4.f, m_progressArtH - kInset * 2.f);
            float const trackW = std::max(innerH, barW / m_progressScale - kInset * 2.f);
            m_progressFill->setPosition({barLeft + kInset * m_progressScale, kProgressY});
            m_progressFill->setContentSize({std::max(innerH, trackW * clampedProg), innerH});
            m_progressFill->setOpacity(toByte(barAlpha));
        }
    }

    // 5. Classic GD 5-bar equalizer using native square02b_001 NineSlice bars.
    float const eqLeft = curW * 0.5f - kEqRightPad - kEqGroupWidth;
    float const eqCenterY = 4.f;
    for (int i = 0; i < kEqCount; ++i) {
        float const barStart = static_cast<float>(i) * 0.09f;
        float const barT = easeOutQuart(subRange(pose.eqReveal, barStart, std::min(1.f, barStart + 0.55f)));
        float const barA = alpha * barT;
        float const x = eqLeft + kEqBarWidth * 0.5f + (kEqBarWidth + kEqBarGap) * static_cast<float>(i);

        auto* track = m_eqTracks[static_cast<std::size_t>(i)];
        auto* bar = m_eqBars[static_cast<std::size_t>(i)];
        if (track) {
            track->setVisible(barA > 0.004f);
            if (barA > 0.004f) {
                track->setPosition({x, eqCenterY});
                track->setOpacity(toByte(0.35f * barA));
            }
        }
        if (bar) {
            bar->setVisible(barA > 0.004f);
            if (barA > 0.004f) {
                float const level = m_eqLevels[static_cast<std::size_t>(i)];
                float const h = std::max(kEqMinHeight, (kEqMinHeight + (kEqMaxHeight - kEqMinHeight) * level) * barT);
                bar->setContentSize({kEqBarWidth, h});
                bar->setPosition({x, eqCenterY - kEqMaxHeight * 0.5f});
                // classic GD lime-green blending to bright yellow-green at energetic peaks.
                bar->setColor(mixColor3B({105, 245, 70}, {245, 255, 115}, level * 0.85f));
                bar->setOpacity(toByte(barA));
            }
        }
    }

    // 6. Subtle classic GD corner star twinkle (GJ_sStar_001 / particle_01_001) on arrival.
    for (std::size_t i = 0; i < m_sparkles.size(); ++i) {
        auto* star = m_sparkles[i];
        if (!star) continue;
        float const localT = pose.sparkleT >= 0.f
            ? subRange(pose.sparkleT, i == 0 ? 0.f : 0.16f, i == 0 ? 0.84f : 1.f)
            : 0.f;
        bool const active = (pose.sparkleT > 0.001f && localT > 0.001f && localT < 0.999f && alpha > 0.01f);
        star->setVisible(active);
        if (active) {
            float const env = std::sin(localT * kPi);
            float const sx = (i == 0 ? 1.f : -1.f) * (curW * 0.5f - 8.f);
            float const sy = (i == 0 ? 1.f : -1.f) * (kCardH * 0.5f - 7.f);
            star->setPosition({sx, sy});
            star->setScale((0.18f + 0.34f * env) * (0.7f + 0.3f * m_intensity));
            star->setRotation(localT * (i == 0 ? 135.f : -120.f));
            star->setOpacity(toByte(0.88f * env * alpha));
        }
    }
}

void NowPlayingToast::buildPanel() {
    m_shadow = paimon::SpriteHelper::safeCreateNineSliceFromFile("square02b_001.png");
    if (m_shadow) {
        m_shadow->setScaleMultiplier(0.5f);
        m_shadow->setAnchorPoint({0.5f, 0.5f});
        m_shadow->setPosition({0.f, -3.5f});
        m_shadow->setColor({0, 0, 0});
        this->addChild(m_shadow, 0);
    }

    m_panel = paimon::SpriteHelper::safeCreateNineSliceFromFile("GJ_square01.png");
    if (m_panel) {
        m_panel->setScaleMultiplier(0.66f);
        m_panel->setAnchorPoint({0.5f, 0.5f});
        m_panel->setPosition({0.f, 0.f});
        this->addChild(m_panel, 1);
    }

    m_innerWell = paimon::SpriteHelper::safeCreateNineSliceFromFile("square02b_001.png");
    if (m_innerWell) {
        m_innerWell->setScaleMultiplier(0.45f);
        m_innerWell->setAnchorPoint({0.5f, 0.5f});
        m_innerWell->setPosition({0.f, 0.f});
        m_innerWell->setColor({0, 0, 0});
        this->addChild(m_innerWell, 2);
    }
}

void NowPlayingToast::buildText(NowPlayingToastContent const& content) {
    struct Spec {
        std::string text;
        char const* font;
        float scale;
        float minScale;
        float height;
        ccColor3B color;
        float revealStart;
        float revealEnd;
        float slideX;
        float slideY;
        bool hasPrefixIcon;
    };

    std::vector<Spec> specs;
    if (!content.prefix.empty()) {
        specs.push_back({
            upperCase(content.prefix), "goldFont.fnt",
            0.31f, 0.21f, 8.5f,
            {255, 255, 255}, // preserve goldFont's iconic GD gold gradient
            0.00f, 0.56f, -10.f, -2.f, true
        });
    }
    specs.push_back({
        content.title.empty() ? std::string("Unknown") : content.title, "bigFont.fnt",
        0.41f, 0.26f, 13.f,
        {255, 255, 255},
        0.12f, 0.78f, -14.f, -1.5f, false
    });
    if (!content.subtitle.empty()) {
        specs.push_back({
            content.subtitle, "chatFont.fnt",
            0.38f, 0.26f, 8.5f,
            {225, 232, 245},
            0.24f, 0.92f, -10.f, -1.f, false
        });
    }

    // measure natural label widths so the GD panel fits cleanly without awkward empty space.
    std::vector<CCLabelBMFont*> labels;
    labels.reserve(specs.size());
    float maxNaturalW = 0.f;
    for (auto const& spec : specs) {
        auto* lbl = CCLabelBMFont::create(spec.text.c_str(), spec.font);
        labels.push_back(lbl);
        if (!lbl) continue;
        lbl->setScale(spec.scale);
        float const iconExtra = spec.hasPrefixIcon ? 11.f : 0.f;
        maxNaturalW = std::max(maxNaturalW, lbl->getScaledContentSize().width + iconExtra);
    }

    float const fixedChrome = kCardH + kTextLeftPad + kEqTextGap + kEqGroupWidth + kEqRightPad;
    m_cardW = std::clamp(fixedChrome + maxNaturalW + 6.f, kMinCardW, kMaxCardW);

    float const textX = -m_cardW * 0.5f + kCardH + kTextLeftPad;
    float const eqLeft = m_cardW * 0.5f - kEqRightPad - kEqGroupWidth;
    float const textMax = std::max(60.f, eqLeft - kEqTextGap - textX);

    float totalH = 0.f;
    for (auto const& spec : specs) totalH += spec.height;
    if (specs.size() > 1) totalH += kRowGap * static_cast<float>(specs.size() - 1);

    // lift text block slightly above center so the classic GD progress bar sits cleanly below it.
    float top = totalH * 0.5f + 4.5f;
    m_rows.clear();

    for (std::size_t i = 0; i < specs.size(); ++i) {
        auto const& spec = specs[i];
        float const centerY = top - spec.height * 0.5f;
        top -= spec.height + kRowGap;

        auto* lbl = labels[i];
        if (!lbl) continue;

        float iconOffset = 0.f;
        if (spec.hasPrefixIcon) {
            m_prefixIcon = paimon::SpriteHelper::createWithFallback(
                "GJ_sMusicIcon_001.png", "GJ_musicIcon_001.png");
            if (m_prefixIcon) {
                CCSize const sz = m_prefixIcon->getContentSize();
                float const s = 9.5f / std::max({sz.width, sz.height, 1.f});
                m_prefixIcon->setScale(s);
                m_prefixIcon->setColor({255, 224, 96});
                m_prefixIconBase = CCPoint{textX + 4.5f, centerY + 0.5f};
                m_prefixIcon->setPosition(m_prefixIconBase);
                m_textLayer->addChild(m_prefixIcon, 2);
                iconOffset = 11.5f;
            }
        }

        lbl->limitLabelWidth(std::max(40.f, textMax - iconOffset), spec.scale, spec.minScale);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setColor(spec.color);
        CCPoint const basePos = {textX + iconOffset, centerY};
        lbl->setPosition(basePos);
        m_textLayer->addChild(lbl, 2);

        m_rows.push_back({
            lbl, basePos,
            spec.revealStart, spec.revealEnd,
            spec.slideX, spec.slideY,
            spec.hasPrefixIcon && m_prefixIcon != nullptr
        });
    }
}

void NowPlayingToast::buildProgressBar() {
    m_progressGroove = createGDProgressCapsule(m_progressArtH);
    if (m_progressGroove) {
        m_progressScale = kProgressVisualH / std::max(1.f, m_progressArtH);
        m_progressGroove->setAnchorPoint({0.f, 0.5f});
        m_progressGroove->setScale(m_progressScale);
        m_progressGroove->setColor({0, 0, 0});
        m_contentClip->addChild(m_progressGroove, 1);
    }

    float unusedH = 20.f;
    m_progressFill = createGDProgressCapsule(unusedH);
    if (m_progressFill) {
        m_progressFill->setAnchorPoint({0.f, 0.5f});
        m_progressFill->setScale(m_progressScale);
        m_progressFill->setColor({115, 255, 75}); // iconic GD level progress green
        m_contentClip->addChild(m_progressFill, 2);
    }
}

void NowPlayingToast::buildEqualizer() {
    for (int i = 0; i < kEqCount; ++i) {
        auto* track = paimon::SpriteHelper::safeCreateNineSliceFromFile("square02b_001.png");
        if (track) {
            track->setScaleMultiplier(0.08f);
            track->setContentSize({kEqBarWidth, kEqMaxHeight});
            track->setAnchorPoint({0.5f, 0.5f});
            track->setColor({0, 0, 0});
            m_contentClip->addChild(track, 1);
        }
        m_eqTracks[static_cast<std::size_t>(i)] = track;

        auto* bar = paimon::SpriteHelper::safeCreateNineSliceFromFile("square02b_001.png");
        if (bar) {
            bar->setScaleMultiplier(0.08f);
            bar->setContentSize({kEqBarWidth, kEqMinHeight});
            bar->setAnchorPoint({0.5f, 0.f});
            bar->setColor({115, 255, 75});
            m_contentClip->addChild(bar, 2);
        }
        m_eqBars[static_cast<std::size_t>(i)] = bar;
    }
}

void NowPlayingToast::buildArtwork() {
    m_artwork = CCNode::create();
    this->addChild(m_artwork, 4);

    // classic dark GD inner icon frame.
    m_artworkFrame = paimon::SpriteHelper::safeCreateNineSliceFromFile("GJ_square05.png");
    if (m_artworkFrame) {
        m_artworkFrame->setScaleMultiplier(0.45f);
        m_artworkFrame->setContentSize({kArtworkFrameSize, kArtworkFrameSize});
        m_artworkFrame->setAnchorPoint({0.5f, 0.5f});
        m_artwork->addChild(m_artworkFrame, 0);
    }

    // classic GD music note badge fallback when no cover is available.
    m_fallbackIcon = paimon::SpriteHelper::createWithFallback(
        "GJ_musicOnBtn_001.png", "GJ_musicIcon_001.png");
    if (m_fallbackIcon) {
        auto const size = m_fallbackIcon->getContentSize();
        m_fallbackBaseScale = 27.f / std::max({size.width, size.height, 1.f});
        m_fallbackIcon->setScale(m_fallbackBaseScale);
        m_artwork->addChild(m_fallbackIcon, 1);
    }

    // rounded-square stencil matching GJ_square05's inner bevel (sibling of m_contentClip).
    auto* coverStencil = PaimonDrawNode::create();
    auto shape = roundedRectPoints(kCoverSize, kCoverSize, 4.f, {0.f, 0.f});
    coverStencil->drawPolygon(
        shape.data(), static_cast<unsigned int>(shape.size()),
        {1.f, 1.f, 1.f, 1.f}, 0.f, {0.f, 0.f, 0.f, 0.f}
    );
    m_coverClip = CCClippingNode::create();
    m_coverClip->setStencil(coverStencil);
    m_coverClip->setAlphaThreshold(0.05f);
    m_artwork->addChild(m_coverClip, 2);
}

void NowPlayingToast::buildSparkles() {
    for (auto& star : m_sparkles) {
        star = paimon::SpriteHelper::createWithFallback("GJ_sStar_001.png", "particle_01_001.png");
        if (star) {
            star->setColor({255, 232, 115});
            star->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
            star->setVisible(false);
            this->addChild(star, 5);
        }
    }
}

void NowPlayingToast::loadCover(std::string const& path) {
    auto const generation = ++m_coverGeneration;
    geode::WeakRef<NowPlayingToast> weak = this;
    paimon::image::loadBudgetedAsync(path, [weak, generation](CCTexture2D* texture) {
        auto self = weak.lock();
        if (!self || !texture || generation != self->m_coverGeneration) return;
        self->applyCover(texture);
    });
}

void NowPlayingToast::applyCover(CCTexture2D* texture) {
    if (!m_coverClip || m_coverSprite || !texture) return;

    auto* sprite = CCSprite::createWithTexture(texture);
    if (!sprite) return;
    CCSize const size = sprite->getContentSize();
    float const minSide = std::min(size.width, size.height);
    if (minSide <= 0.f) return;

    sprite->setScale((kCoverSize + 0.5f) / minSide);
    sprite->setOpacity(0);
    m_coverClip->addChild(sprite, 0);
    m_coverSprite = sprite;
    m_hasCover = true;
}

void NowPlayingToast::enter(Phase phase, float duration) {
    m_phase = phase;
    m_phaseTime = 0.f;
    m_phaseDuration = std::max(0.f, duration);
    if (phase == Phase::Hold) {
        m_holdClock = 0.f;
    }
}

bool NowPlayingToast::init(NowPlayingToastContent const& content, Options const& options, float waitSeconds) {
    if (!CCNode::init()) return false;

    m_style = options.style;
    m_intensity = std::clamp(options.intensity, 0.f, 1.f);
    m_holdDuration = std::clamp(options.holdSeconds, 0.5f, 5.f);
    float const speed = std::clamp(options.speed, 0.25f, 3.f);
    Timing const timing = timingFor(m_style);
    m_inDuration = timing.in / speed;
    m_outDuration = timing.out / speed;

    // children are laid out around (0, 0) = center of the GD banner.
    this->ignoreAnchorPointForPosition(true);
    this->setID(kToastId.c_str());

    buildPanel();

    // interior clip mask keeps text, equalizer, and GD progress bar cleanly inside GJ_square01.
    m_clipStencil = PaimonDrawNode::create();
    m_contentClip = CCClippingNode::create();
    m_contentClip->setStencil(m_clipStencil);
    m_contentClip->setAlphaThreshold(0.05f);
    this->addChild(m_contentClip, 3);

    m_textLayer = CCNode::create();
    m_contentClip->addChild(m_textLayer, 3);

    buildText(content);
    this->setContentSize({m_cardW, kCardH});

    buildProgressBar();
    buildEqualizer();
    buildArtwork();
    buildSparkles();

    CCSize const win = CCDirector::get()->getWinSize();
    m_layoutScale = std::min(1.f, std::max(0.1f, (win.width - 20.f) / m_cardW));
    m_centerX = win.width * 0.5f;
    m_showY = win.height - kTopMargin - kCardH * m_layoutScale * 0.5f;
    m_hideY = win.height + kCardH * m_layoutScale * 0.85f + 12.f;

    if (!content.coverPath.empty()) {
        loadCover(content.coverPath);
    }

    Pose const startPose = poseAt(waitSeconds > 0.f ? Phase::Wait : Phase::In, 0.f);
    applyPose(startPose, 0.f, 1.f);

    if (waitSeconds > 0.f) {
        this->setVisible(false);
        enter(Phase::Wait, waitSeconds);
    } else {
        this->setVisible(true);
        enter(Phase::In, m_inDuration);
    }

    this->schedule(schedule_selector(NowPlayingToast::onTick));
    return true;
}

void NowPlayingToast::onExit() {
    ++m_coverGeneration;
    this->unschedule(schedule_selector(NowPlayingToast::onTick));
    CCNode::onExit();
}

void NowPlayingToast::finish() {
    m_phase = Phase::Done;
    this->unschedule(schedule_selector(NowPlayingToast::onTick));
    this->removeFromParent();
}

void NowPlayingToast::onTick(float dt) {
    if (m_phase == Phase::Done) return;

    float const safeDt = std::clamp(dt, 0.f, m_phase == Phase::Wait ? 0.10f : kMaxAnimDt);
    m_clock += safeDt;
    m_phaseTime += safeDt;

    float const raw = m_phaseDuration > 0.f ? clamp01(m_phaseTime / m_phaseDuration) : 1.f;
    float progress = 1.f;
    Pose currentPose;

    switch (m_phase) {
        case Phase::Wait:
            if (raw >= 1.f) {
                this->setVisible(true);
                enter(Phase::In, m_inDuration);
                currentPose = poseAt(Phase::In, 0.f);
                updateEqualizer(safeDt, currentPose.eqReveal);
                applyPose(currentPose, safeDt, 1.f);
            }
            return;

        case Phase::In:
            currentPose = poseAt(Phase::In, raw);
            updateEqualizer(safeDt, currentPose.eqReveal);
            applyPose(currentPose, safeDt, 1.f);
            if (raw >= 1.f) enter(Phase::Hold, m_holdDuration);
            return;

        case Phase::Hold:
            m_holdClock += safeDt;
            currentPose = poseAt(Phase::Hold, raw);
            updateEqualizer(safeDt, currentPose.eqReveal);
            progress = 1.f - raw;
            applyPose(currentPose, safeDt, progress);
            if (raw >= 1.f) enter(Phase::Out, m_outDuration);
            return;

        case Phase::Out:
            currentPose = poseAt(Phase::Out, raw);
            updateEqualizer(safeDt, currentPose.eqReveal);
            applyPose(currentPose, safeDt, 0.f);
            if (raw >= 1.f) {
                finish();
            }
            return;

        case Phase::Done:
            return;
    }
}

void NowPlayingToast::present(
    CCNode* parent,
    NowPlayingToastContent const& content,
    int zOrder,
    float waitSeconds
) {
    if (!parent) return;

    bool replacedExisting = false;
    if (auto* old = parent->getChildByIDRecursive(kToastId.c_str())) {
        old->removeFromParentAndCleanup(true);
        replacedExisting = true;
    }

    float const effectiveWait = replacedExisting ? 0.f : waitSeconds;

    auto* toast = new NowPlayingToast();
    if (toast && toast->init(content, readOptions(), effectiveWait)) {
        toast->autorelease();
        parent->addChild(toast, zOrder);
    } else {
        CC_SAFE_DELETE(toast);
    }
}

void NowPlayingToast::showForCurrent(CCNode* parent) {
    if (!parent) return;
    if (!Mod::get()->getSettingValue<bool>("menuLoopEnableNotification")) return;

    auto const* track = MenuMusicPlayer::get().currentTrack();
    if (!track) return;

    NowPlayingToastContent content;
    content.prefix = prefixSetting();
    content.title = track->displayName;
    content.subtitle = subtitleFor(*track);
    content.coverPath = track->coverPath;
    present(parent, content, kToastZOrder, kDefaultWaitDur);
}

void NowPlayingToast::showContent(CCNode* parent, NowPlayingToastContent const& content) {
    present(parent, content, kToastZOrder, kDefaultWaitDur);
}

void NowPlayingToast::showPreview(CCNode* parent) {
    if (!parent) return;

    NowPlayingToastContent content;
    content.prefix = prefixSetting();
    if (auto const* track = MenuMusicPlayer::get().currentTrack()) {
        content.title = track->displayName;
        content.subtitle = subtitleFor(*track);
        content.coverPath = track->coverPath;
    } else {
        content.title = "Sample Song";
        content.subtitle = "Animation preview";
    }
    present(parent, content, kPreviewZOrder, 0.f);
}

} // namespace paimon::menumusic
