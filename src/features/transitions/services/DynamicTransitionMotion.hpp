#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace paimon::transitions::dynamic {

enum class Style { App, Card, Slide, Zoom, Push, Sheet, Fade, Reveal };
enum class Curve { Smooth, Spring, Soft, Linear, Expo, Emphasized };
enum class Origin { Button, Center, Bottom, Left, Right, Top };
enum class ReducedMotion { Fade, Instant };
enum class PanelStyle { Match, App, Card, Slide, Zoom, Push, Sheet, Fade, Reveal };

inline constexpr int kStyleCount = 8;
inline constexpr int kCurveCount = 6;
inline constexpr int kOriginCount = 6;
inline constexpr int kPanelStyleCount = 9;
inline constexpr float kPi = 3.14159265f;

struct Rect {
    float x = 0.f, y = 0.f, width = 0.f, height = 0.f;
};

struct Config {
    bool enabled = true;
    bool animateBack = true;
    bool animateKeyboardBack = true;
    bool animatePopups = true;
    bool animateDropdowns = true;
    bool animateBlockingLayers = true;
    bool animateDialogs = true;
    bool animateEditorPanels = true;
    bool animateGameplayPanels = true;
    bool animateOtherMods = false;
    bool includeInstant = true;
    bool onlyFromButton = false;
    bool syncSmoothUI = true;
    Style style = Style::App;
    Curve curve = Curve::Smooth;
    Origin origin = Origin::Button;
    ReducedMotion reducedMotion = ReducedMotion::Fade;
    PanelStyle panelStyle = PanelStyle::Match;
    float duration = .42f;
    float backDuration = .32f;
    float cornerRadius = 18.f;
    float backgroundScale = .96f;
    float dim = .22f;
    float spring = .035f;
    float buttonBlend = .24f;
    float shadow = .5f;
    float quality = 1.f;
};

inline float limit(float value, float fallback, float low, float high) {
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}

template<class Enum>
Enum validEnum(Enum value, int count, Enum fallback) {
    int index = static_cast<int>(value);
    return index >= 0 && index < count ? value : fallback;
}

inline Config sanitize(Config config) {
    config.style = validEnum(config.style, kStyleCount, Style::App);
    config.curve = validEnum(config.curve, kCurveCount, Curve::Smooth);
    config.origin = validEnum(config.origin, kOriginCount, Origin::Button);
    config.reducedMotion = validEnum(config.reducedMotion, 2, ReducedMotion::Fade);
    config.panelStyle = validEnum(config.panelStyle, kPanelStyleCount, PanelStyle::Match);
    config.duration = limit(config.duration, .42f, .12f, 1.5f);
    config.backDuration = limit(config.backDuration, .32f, .12f, 1.5f);
    config.cornerRadius = limit(config.cornerRadius, 18.f, 0.f, 48.f);
    config.backgroundScale = limit(config.backgroundScale, .96f, .85f, 1.f);
    config.dim = limit(config.dim, .22f, 0.f, .65f);
    config.spring = limit(config.spring, .035f, 0.f, .25f);
    config.buttonBlend = limit(config.buttonBlend, .24f, 0.f, .6f);
    config.shadow = limit(config.shadow, .5f, 0.f, 1.f);
    config.quality = limit(config.quality, 1.f, .5f, 1.f);
    return config;
}

inline float cubicBezier(float t, float x1, float y1, float x2, float y2) {
    auto sample = [](float a, float b, float s) {
        float inv = 1.f - s;
        return 3.f * inv * inv * s * a + 3.f * inv * s * s * b + s * s * s;
    };
    auto slope = [](float a, float b, float s) {
        float inv = 1.f - s;
        return 3.f * inv * inv * a + 6.f * inv * s * (b - a) + 3.f * s * s * (1.f - b);
    };
    float s = t;
    for (int i = 0; i < 6; ++i) {
        float error = sample(x1, x2, s) - t;
        if (std::abs(error) < 1e-5f) return sample(y1, y2, s);
        float derivative = slope(x1, x2, s);
        if (std::abs(derivative) < 1e-5f) break;
        s = std::clamp(s - error / derivative, 0.f, 1.f);
    }
    float low = 0.f, high = 1.f;
    s = t;
    for (int i = 0; i < 24; ++i) {
        float x = sample(x1, x2, s);
        if (std::abs(x - t) < 1e-5f) break;
        (x < t ? low : high) = s;
        s = (low + high) * .5f;
    }
    return sample(y1, y2, s);
}

// Fold in the damped spring's residual so t = 1 lands exactly.
inline float springEase(float t, float overshoot) {
    float amount = limit(overshoot, .035f, 0.f, .25f);
    float zeta = 1.f;
    if (amount > 1e-4f) {
        float l = std::log(amount);
        zeta = std::clamp(-l / std::sqrt(kPi * kPi + l * l), .25f, 1.f);
    }
    float omega = std::max(8.f, 4.6f / zeta);
    auto response = [zeta, omega](float s) {
        if (zeta >= .999f) return 1.f - std::exp(-omega * s) * (1.f + omega * s);
        float damped = omega * std::sqrt(1.f - zeta * zeta);
        return 1.f - std::exp(-zeta * omega * s) *
            (std::cos(damped * s) + zeta * omega / damped * std::sin(damped * s));
    };
    return response(t) + (1.f - response(1.f)) * t * t * t;
}

inline float ease(float progress, Curve curve, float spring) {
    float t = limit(progress, 0.f, 0.f, 1.f);
    if (t <= 0.f || t >= 1.f) return t;
    switch (curve) {
        case Curve::Smooth: return cubicBezier(t, .32f, .72f, 0.f, 1.f);
        case Curve::Spring: return springEase(t, spring);
        case Curve::Soft: return t * t * t * (t * (t * 6.f - 15.f) + 10.f);
        case Curve::Linear: return t;
        case Curve::Expo: return (1.f - std::exp2(-10.f * t)) / (1.f - std::exp2(-10.f));
        case Curve::Emphasized: return cubicBezier(t, .05f, .7f, .1f, 1.f);
    }
    return t;
}

inline float smooth01(float value) {
    float x = limit(value, 0.f, 0.f, 1.f);
    return x * x * (3.f - 2.f * x);
}

// Lengths are tuned for GD's 320-point design height, so smaller stages scale them down.
inline float unitScale(float height) {
    return limit(height / 320.f, 1.f, .05f, 4.f);
}

inline Rect normalizeOrigin(Rect rect, float width, float height) {
    width = std::max(width, 1.f);
    height = std::max(height, 1.f);
    rect.width = limit(rect.width, 40.f, std::min(8.f, width), width);
    rect.height = limit(rect.height, 40.f, std::min(8.f, height), height);
    rect.x = limit(rect.x, (width - rect.width) / 2.f, 0.f, width - rect.width);
    rect.y = limit(rect.y, (height - rect.height) / 2.f, 0.f, height - rect.height);
    return rect;
}

inline Rect fallbackOrigin(float width, float height, Origin origin) {
    float unit = unitScale(height);
    float size = std::min({48.f * unit, width, height});
    float margin = 12.f * unit;
    Rect rect{(width - size) / 2.f, (height - size) / 2.f, size, size};
    switch (origin) {
        case Origin::Bottom: rect.y = margin; break;
        case Origin::Top: rect.y = height - size - margin; break;
        case Origin::Left: rect.x = margin; break;
        case Origin::Right: rect.x = width - size - margin; break;
        default: break;
    }
    return normalizeOrigin(rect, width, height);
}

inline Rect cardRect(Rect origin, float width, float height) {
    float w = std::max(origin.width, width * .28f);
    float h = std::max(origin.height, height * .28f);
    return normalizeOrigin({origin.x + (origin.width - w) / 2.f,
        origin.y + (origin.height - h) / 2.f, w, h}, width, height);
}

inline Rect scaleRect(Rect rect, float scale, float cx, float cy) {
    return {cx + (rect.x - cx) * scale, cy + (rect.y - cy) * scale,
        rect.width * scale, rect.height * scale};
}

inline Rect offsetRect(Rect rect, float dx, float dy) {
    return {rect.x + dx, rect.y + dy, rect.width, rect.height};
}

// Negative expansion (spring undershoot on return) shrinks the start rect instead of inverting it.
inline Rect expandRect(Rect from, Rect to, float expansion) {
    if (expansion < 0.f) {
        return scaleRect(from, std::max(.2f, 1.f + expansion),
            from.x + from.width / 2.f, from.y + from.height / 2.f);
    }
    auto mix = [expansion](float a, float b) { return a + (b - a) * expansion; };
    return {mix(from.x, to.x), mix(from.y, to.y),
        std::max(1.f, mix(from.width, to.width)), std::max(1.f, mix(from.height, to.height))};
}

inline Rect aspectFill(Rect clip, float width, float height) {
    float scale = std::max(clip.width / width, clip.height / height);
    float cx = clip.x + clip.width / 2.f, cy = clip.y + clip.height / 2.f;
    return {cx - width * scale / 2.f, cy - height * scale / 2.f, width * scale, height * scale};
}

enum class Edge { Bottom, Top, Left, Right };

inline Edge edgeFor(Origin origin, Edge fallback) {
    switch (origin) {
        case Origin::Bottom: return Edge::Bottom;
        case Origin::Top: return Edge::Top;
        case Origin::Left: return Edge::Left;
        case Origin::Right: return Edge::Right;
        default: return fallback;
    }
}

struct Layer {
    Rect clip;
    Rect content;
    float radius = 0.f;
    float opacity = 1.f;
    float brightness = 1.f;
};

struct Frame {
    Layer background;
    Layer foreground;
    Layer button;
    float shadow = 0.f;
    float shadowBlur = 0.f;
};

// `panel` marks captures of the same scene, where moving the background would show it twice.
inline Frame evaluate(Config const& config, Rect origin, float width, float height,
    float progress, bool backwards, bool panel = false) {
    width = std::max(width, 1.f);
    height = std::max(height, 1.f);
    float t = limit(progress, 0.f, 0.f, 1.f);
    float eased = ease(t, config.curve, config.spring);
    float expansion = std::clamp(backwards ? 1.f - eased : eased, -.3f, 1.3f);
    float coverage = std::clamp(expansion, 0.f, 1.f);
    float shown = backwards ? 1.f - t : t;
    float unit = unitScale(height);
    origin = normalizeOrigin(origin, width, height);
    float ox = origin.x + origin.width / 2.f, oy = origin.y + origin.height / 2.f;
    Rect screen{0.f, 0.f, width, height};
    float depth = 1.f - limit(config.backgroundScale, .96f, .85f, 1.f);
    float radius = config.cornerRadius * unit * (1.f - coverage);

    Frame frame;
    auto& background = frame.background;
    auto& foreground = frame.foreground;
    auto& button = frame.button;
    background.clip = background.content = screen;
    foreground.clip = foreground.content = screen;
    button.opacity = 0.f;
    float backgroundScale = 1.f - depth * coverage;
    float backgroundRadius = 0.f;
    float shiftX = 0.f, shiftY = 0.f;
    float shadowBell = std::pow(std::max(0.f, std::sin(coverage * kPi)), .6f);
    bool staticBackground = panel;

    switch (config.style) {
        case Style::App:
        case Style::Card: {
            Rect start = config.style == Style::Card ? cardRect(origin, width, height) : origin;
            foreground.clip = expandRect(start, screen, expansion);
            foreground.content = aspectFill(foreground.clip, width, height);
            foreground.radius = radius;
            if (config.style == Style::App && config.buttonBlend > 0.f) {
                float blend = smooth01(shown / config.buttonBlend);
                foreground.opacity = blend;
                // The opaque button patch sits under the fading layer, so the crossfade never shows the background.
                button.opacity = blend < 1.f ? 1.f : 0.f;
                button.clip = foreground.clip;
                button.radius = foreground.radius;
                float sx = foreground.clip.width / origin.width;
                float sy = foreground.clip.height / origin.height;
                button.content = {foreground.clip.x - origin.x * sx, foreground.clip.y - origin.y * sy,
                    width * sx, height * sy};
            } else {
                foreground.opacity = smooth01(shown / (config.style == Style::Card ? .12f : .08f));
            }
            break;
        }
        case Style::Slide:
        case Style::Push:
        case Style::Sheet: {
            Edge edge = config.style == Style::Slide ? edgeFor(config.origin, Edge::Bottom) :
                config.style == Style::Push ? edgeFor(config.origin, Edge::Right) : Edge::Bottom;
            float travel = 1.f - expansion;
            float dx = edge == Edge::Left ? -width : edge == Edge::Right ? width : 0.f;
            float dy = edge == Edge::Bottom ? -height : edge == Edge::Top ? height : 0.f;
            if (travel >= 0.f) {
                foreground.clip = foreground.content = offsetRect(screen, dx * travel, dy * travel);
            } else {
                // Overshoot stretches the layer past its leading edge instead of opening a gap behind it.
                float stretch = -travel;
                Rect clip = screen;
                if (dx != 0.f) clip.width += width * stretch;
                if (dy != 0.f) clip.height += height * stretch;
                if (dx > 0.f) clip.x -= width * stretch;
                if (dy > 0.f) clip.y -= height * stretch;
                foreground.clip = clip;
                foreground.content = aspectFill(clip, width, height);
            }
            foreground.radius = config.style == Style::Push ? radius * .5f : radius;
            if (config.style == Style::Push) {
                backgroundScale = 1.f;
                shiftX = -dx * .3f * coverage;
                shiftY = -dy * .3f * coverage;
            } else if (config.style == Style::Sheet) {
                backgroundScale = 1.f - std::max(depth, .04f) * 1.4f * coverage;
                backgroundRadius = config.cornerRadius * unit * .6f * coverage;
            }
            break;
        }
        case Style::Zoom: {
            float scale = .82f + .18f * expansion;
            foreground.clip = foreground.content = scaleRect(screen, scale, width / 2.f, height / 2.f);
            foreground.opacity = smooth01(shown / .35f);
            foreground.radius = radius;
            break;
        }
        case Style::Fade: {
            float zoom = panel ? 0.f : depth * .9f * std::max(0.f, 1.f - expansion);
            foreground.content = scaleRect(screen, 1.f + zoom, width / 2.f, height / 2.f);
            foreground.opacity = smooth01(coverage);
            shadowBell = 0.f;
            break;
        }
        case Style::Reveal: {
            float startRadius = std::max(origin.width, origin.height) * .5f;
            float endRadius = std::sqrt(std::pow(std::max(ox, width - ox), 2.f) +
                std::pow(std::max(oy, height - oy), 2.f));
            float circle = expansion < 0.f ? startRadius * std::max(.2f, 1.f + expansion) :
                startRadius + (endRadius - startRadius) * expansion;
            circle = std::max(.5f, circle);
            foreground.clip = {ox - circle, oy - circle, circle * 2.f, circle * 2.f};
            foreground.radius = circle;
            foreground.opacity = smooth01(shown / .1f);
            if (expansion >= 1.f) {
                foreground.clip = screen;
                foreground.radius = 0.f;
            }
            break;
        }
    }

    if (staticBackground) {
        shiftX = shiftY = 0.f;
        if (config.style == Style::Zoom || config.style == Style::Fade || config.style == Style::Reveal)
            backgroundScale = 1.f;
    }
    background.clip = background.content = offsetRect(
        scaleRect(screen, backgroundScale, width / 2.f, height / 2.f), shiftX, shiftY);
    background.radius = backgroundRadius;
    background.brightness = 1.f - limit(config.dim, .22f, 0.f, .65f) * coverage;
    foreground.radius = std::clamp(foreground.radius, 0.f,
        std::min(foreground.clip.width, foreground.clip.height) / 2.f);
    button.radius = foreground.radius;
    foreground.opacity = std::clamp(foreground.opacity, 0.f, 1.f);
    button.opacity = std::clamp(button.opacity, 0.f, 1.f);
    frame.shadow = limit(config.shadow, .5f, 0.f, 1.f) * .55f * shadowBell *
        std::max(foreground.opacity, button.opacity);
    frame.shadowBlur = (8.f + 10.f * coverage) * unit;
    return frame;
}

inline Config panelConfig(Config config) {
    if (config.panelStyle != PanelStyle::Match)
        config.style = static_cast<Style>(static_cast<int>(config.panelStyle) - 1);
    return config;
}

inline Config reducedMotionConfig(Config config) {
    config.style = Style::Fade;
    config.curve = Curve::Soft;
    config.duration = config.backDuration = .16f;
    config.backgroundScale = 1.f;
    config.dim = 0.f;
    config.shadow = 0.f;
    config.buttonBlend = 0.f;
    return config;
}

// A long first frame usually carries the capture cost, so it advances one nominal frame.
inline float frameStep(float dt, bool first) {
    if (!std::isfinite(dt) || dt <= 0.f) return 0.f;
    return std::min(dt, first ? 1.f / 60.f : .1f);
}

struct Preset {
    Style style;
    Curve curve;
    float duration, backDuration, cornerRadius, backgroundScale, dim, spring, buttonBlend, shadow;
};

inline constexpr std::array<Preset, 9> kPresets{{
    {Style::App, Curve::Smooth, .42f, .32f, 18.f, .96f, .22f, .035f, .24f, .5f},
    {Style::App, Curve::Expo, .26f, .22f, 14.f, .98f, .14f, .035f, .2f, .4f},
    {Style::App, Curve::Soft, .55f, .42f, 22.f, .97f, .2f, .035f, .3f, .55f},
    {Style::App, Curve::Spring, .52f, .4f, 20.f, .95f, .22f, .09f, .24f, .5f},
    {Style::Card, Curve::Emphasized, .65f, .5f, 28.f, .92f, .4f, .035f, .24f, .7f},
    {Style::Sheet, Curve::Spring, .5f, .4f, 22.f, .94f, .28f, .02f, .24f, .55f},
    {Style::Push, Curve::Smooth, .38f, .32f, 0.f, 1.f, .25f, .035f, .24f, .6f},
    {Style::Reveal, Curve::Emphasized, .5f, .38f, 18.f, .98f, .18f, .035f, .24f, .45f},
    {Style::Fade, Curve::Soft, .24f, .2f, 0.f, .98f, .08f, .035f, .24f, 0.f},
}};

inline Config applyPreset(Config config, int index) {
    if (index < 0 || index >= static_cast<int>(kPresets.size())) return sanitize(config);
    auto const& preset = kPresets[static_cast<size_t>(index)];
    config.style = preset.style;
    config.curve = preset.curve;
    config.duration = preset.duration;
    config.backDuration = preset.backDuration;
    config.cornerRadius = preset.cornerRadius;
    config.backgroundScale = preset.backgroundScale;
    config.dim = preset.dim;
    config.spring = preset.spring;
    config.buttonBlend = preset.buttonBlend;
    config.shadow = preset.shadow;
    return sanitize(config);
}

inline int matchPreset(Config const& config) {
    auto near = [](float a, float b, float tolerance) { return std::abs(a - b) <= tolerance; };
    for (size_t i = 0; i < kPresets.size(); ++i) {
        auto const& preset = kPresets[i];
        if (config.style == preset.style && config.curve == preset.curve &&
            near(config.duration, preset.duration, .006f) &&
            near(config.backDuration, preset.backDuration, .006f) &&
            near(config.cornerRadius, preset.cornerRadius, .3f) &&
            near(config.backgroundScale, preset.backgroundScale, .003f) &&
            near(config.dim, preset.dim, .006f) &&
            near(config.spring, preset.spring, .003f) &&
            near(config.buttonBlend, preset.buttonBlend, .006f) &&
            near(config.shadow, preset.shadow, .006f)) return static_cast<int>(i);
    }
    return -1;
}

}
