#pragma once

#include <algorithm>
#include <cmath>

namespace paimon::transitions::dynamic {

enum class Style { App, Card, Slide, Zoom };
enum class Curve { Smooth, Spring, Soft, Linear };
enum class Origin { Button, Center, Bottom, Left };
enum class ReducedMotion { Fade, Instant };

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
    float duration = .42f;
    float backDuration = .32f;
    float cornerRadius = 18.f;
    float backgroundScale = .96f;
    float dim = .22f;
    float spring = .035f;
    float buttonBlend = .24f;
    float quality = 1.f;
};

inline float limit(float value, float fallback, float low, float high) {
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}

inline Config sanitize(Config config) {
    if (config.style < Style::App || config.style > Style::Zoom) config.style = Style::App;
    if (config.curve < Curve::Smooth || config.curve > Curve::Linear) config.curve = Curve::Smooth;
    if (config.origin < Origin::Button || config.origin > Origin::Left) config.origin = Origin::Button;
    if (config.reducedMotion < ReducedMotion::Fade || config.reducedMotion > ReducedMotion::Instant)
        config.reducedMotion = ReducedMotion::Fade;
    config.duration = limit(config.duration, .42f, .12f, 1.5f);
    config.backDuration = limit(config.backDuration, .32f, .12f, 1.5f);
    config.cornerRadius = limit(config.cornerRadius, 18.f, 0.f, 48.f);
    config.backgroundScale = limit(config.backgroundScale, .96f, .85f, 1.f);
    config.dim = limit(config.dim, .22f, 0.f, .65f);
    config.spring = limit(config.spring, .035f, 0.f, .12f);
    config.buttonBlend = limit(config.buttonBlend, .24f, 0.f, .6f);
    config.quality = limit(config.quality, 1.f, .5f, 1.f);
    return config;
}

inline float ease(float progress, Curve curve, float spring) {
    float t = limit(progress, 0.f, 0.f, 1.f);
    if (t == 0.f || t == 1.f) return t;
    switch (curve) {
        case Curve::Smooth: return 1.f - std::pow(1.f - t, 4.f);
        case Curve::Soft: return t * t * t * (t * (t * 6.f - 15.f) + 10.f);
        case Curve::Spring: {
            float base = 1.f - std::pow(1.f - t, 4.f);
            return base + limit(spring, .035f, 0.f, .12f) *
                std::sin(t * 3.14159265f) * std::sin(t * 3.14159265f);
        }
        case Curve::Linear: return t;
    }
    return t;
}

inline Rect normalizeOrigin(Rect rect, float width, float height) {
    rect.width = limit(rect.width, 40.f, 8.f, width);
    rect.height = limit(rect.height, 40.f, 8.f, height);
    rect.x = limit(rect.x, (width - rect.width) / 2.f, 0.f, width - rect.width);
    rect.y = limit(rect.y, (height - rect.height) / 2.f, 0.f, height - rect.height);
    return rect;
}

inline Rect fallbackOrigin(float width, float height, Origin origin) {
    float size = std::min({48.f, width, height});
    Rect rect{(width - size) / 2.f, (height - size) / 2.f, size, size};
    if (origin == Origin::Bottom) rect.y = 12.f;
    if (origin == Origin::Left) rect.x = 12.f;
    return normalizeOrigin(rect, width, height);
}

inline Rect initialRect(Config const& config, Rect origin, float width, float height) {
    origin = normalizeOrigin(origin, width, height);
    if (config.style == Style::Slide) return {0.f, -height, width, height};
    if (config.style == Style::Zoom) return {width * .1f, height * .1f, width * .8f, height * .8f};
    if (config.style == Style::Card) {
        float w = std::max(origin.width, width * .28f);
        float h = std::max(origin.height, height * .28f);
        return normalizeOrigin({origin.x + (origin.width - w) / 2.f,
            origin.y + (origin.height - h) / 2.f, w, h}, width, height);
    }
    return origin;
}

struct Frame {
    Rect rect;
    float radius = 0.f;
    float backgroundScale = 1.f;
    float backgroundBrightness = 1.f;
    float opacity = 1.f;
    float buttonOpacity = 0.f;
    float shadow = 0.f;
};

inline Frame evaluate(Config const& config, Rect origin, float width, float height,
    float progress, bool backwards) {
    float t = limit(progress, 0.f, 0.f, 1.f);
    float eased = ease(t, config.curve, config.spring);
    float expansion = backwards ? 1.f - eased : eased;
    float coverage = std::clamp(expansion, 0.f, 1.f);
    Rect start = initialRect(config, origin, width, height);
    auto mix = [expansion](float a, float b) { return a + (b - a) * expansion; };
    Frame frame;
    frame.rect = {mix(start.x, 0.f), mix(start.y, 0.f),
        std::max(1.f, mix(start.width, width)), std::max(1.f, mix(start.height, height))};
    frame.radius = std::min({config.cornerRadius * (1.f - coverage),
        frame.rect.width / 2.f, frame.rect.height / 2.f});
    frame.backgroundScale = 1.f + (config.backgroundScale - 1.f) * coverage;
    frame.backgroundBrightness = 1.f - config.dim * coverage;
    float blendProgress = backwards ? 1.f - t : t;
    float blend = config.buttonBlend > 0.f ?
        std::clamp(blendProgress / config.buttonBlend, 0.f, 1.f) : 1.f;
    bool morphButton = config.style == Style::App && config.buttonBlend > 0.f;
    frame.buttonOpacity = morphButton ? 1.f - blend : 0.f;
    frame.opacity = morphButton ? blend :
        (config.style == Style::Zoom ? std::clamp(blendProgress * 5.f, 0.f, 1.f) : 1.f);
    if (backwards) frame.opacity *= std::clamp((1.f - t) * 8.f, 0.f, 1.f);
    frame.shadow = std::max(0.f, std::sin(coverage * 3.14159265f)) * .25f;
    return frame;
}

}
