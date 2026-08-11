#pragma once

#include <Geode/Geode.hpp>

namespace paimon::cursorfx {

enum class TransitionEffect : int {
    Instant = 0,
    Dissolve,
    Fade,
    CrossFade,
    ZoomIn,
    ZoomOut,
    Pop,
    Pulse,
    SpinClockwise,
    SpinCounterClockwise,
    FlipHorizontal,
    FlipVertical,
    SlideLeft,
    SlideRight,
    SlideUp,
    SlideDown,
    Diagonal,
    Swing,
    Pendulum,
    Squash,
    Stretch,
    OrbitClockwise,
    OrbitCounterClockwise,
    Snap,
    Glitch,
    Burst,
    Count
};

enum class TransitionEasing : int {
    Linear = 0,
    Smooth,
    EaseIn,
    EaseOut,
    EaseInOut,
    Back,
    Elastic,
    Bounce,
    Count
};

inline constexpr int kTransitionEffectCount = static_cast<int>(TransitionEffect::Count);
inline constexpr int kTransitionEasingCount = static_cast<int>(TransitionEasing::Count);
inline constexpr float kTransitionDurationMin = 0.04f;
inline constexpr float kTransitionDurationMax = 0.80f;
inline constexpr float kTransitionIntensityMin = 0.25f;
inline constexpr float kTransitionIntensityMax = 2.00f;

struct TransitionSettings {
    TransitionEffect effect = TransitionEffect::CrossFade;
    TransitionEasing easing = TransitionEasing::Smooth;
    float duration = 0.16f;
    float intensity = 0.80f;
};

struct TransitionFrame {
    cocos2d::CCPoint offset{};
    float scaleX = 1.f;
    float scaleY = 1.f;
    float rotation = 0.f;
    float skewX = 0.f;
    float skewY = 0.f;
    float opacity = 1.f;
};

struct TransitionPreset {
    char const* name;
    TransitionSettings settings;
};

char const* transitionEffectName(TransitionEffect effect);
char const* transitionEffectDesc(TransitionEffect effect);
char const* transitionEasingName(TransitionEasing easing);
char const* transitionEasingDesc(TransitionEasing easing);

int transitionPresetCount();
TransitionPreset const& transitionPresetAt(int index);
int findTransitionPreset(TransitionSettings const& settings);

TransitionFrame sampleTransition(
    TransitionSettings const& settings, float progress, bool incoming);

void applyTransitionFrame(
    cocos2d::CCSprite* sprite,
    cocos2d::CCPoint const& position,
    cocos2d::CCPoint const& baseScale,
    int baseOpacity,
    TransitionFrame const& frame);

} // namespace paimon::cursorfx
