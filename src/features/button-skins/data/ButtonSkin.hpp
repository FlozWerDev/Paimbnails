#pragma once

#include "../../quick-hub/data/QuickHubCategories.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <cmath>
#include <string>

namespace paimon::buttonskin {

// ints on disk, like the quick hub enums.
enum class SkinScope : int { Exact = 0, SameScreen = 1, Everywhere = 2 };
enum class SkinFill : int { None = 0, Image = 1, Color = 2, Gradient = 3 };
enum class SkinFit : int { Cover = 0, Contain = 1, Stretch = 2 };
enum class SkinBlend : int { Normal = 0, Multiply = 1, Add = 2, Screen = 3 };

struct ButtonSkin {
    std::string id;
    // address of the captured button; its sfx* fields hold the click sound.
    quickhub::CustomQuickButton target;
    // frames the button draws: look-alike buttons share it (wider scopes).
    std::string signature;
    SkinScope scope = SkinScope::Exact;

    // paint layered over the asset
    SkinFill fill = SkinFill::None;
    std::string imagePath;
    SkinFit fit = SkinFit::Cover;
    SkinBlend blend = SkinBlend::Normal;
    bool maskToShape = true;     // paint only where the asset has pixels
    float fillOpacity = 1.f;     // 0..1
    float fillScale = 1.f;
    float fillRotation = 0.f;    // image turn, gradient angle
    float fillOffsetX = 0.f;     // fraction of the asset size
    float fillOffsetY = 0.f;
    bool fillFlipX = false;
    bool fillFlipY = false;
    cocos2d::ccColor3B fillColor{255, 140, 200};
    cocos2d::ccColor3B fillColorB{110, 200, 255};

    // original asset
    bool tintEnabled = false;
    cocos2d::ccColor3B tint{255, 255, 255};
    float baseOpacity = 1.f;     // 0..1
    bool hideOriginal = false;   // only the paint stays visible

    // whole button
    float scale = 1.f;
    float rotation = 0.f;
    float offsetX = 0.f;         // points in the button's parent
    float offsetY = 0.f;

    bool muteOriginal = false;   // silence the game's click sound
};

namespace limits {
inline constexpr float kFillScaleMin = 0.2f;
inline constexpr float kFillScaleMax = 4.f;
inline constexpr float kFillOffsetMax = 1.f;
inline constexpr float kScaleMin = 0.3f;
inline constexpr float kScaleMax = 3.f;
inline constexpr float kOffsetMax = 200.f;
} // namespace limits

inline float wrapDegrees(float v) {
    if (!std::isfinite(v)) return 0.f;
    while (v > 180.f) v -= 360.f;
    while (v < -180.f) v += 360.f;
    return v;
}

inline float clampFinite(float v, float lo, float hi, float fallback) {
    return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
}

inline bool sameColor(cocos2d::ccColor3B a, cocos2d::ccColor3B b) {
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

inline bool hasFill(ButtonSkin const& s) {
    if (s.fill == SkinFill::Image) return !s.imagePath.empty();
    return s.fill == SkinFill::Color || s.fill == SkinFill::Gradient;
}

inline bool hasCustomSound(ButtonSkin const& s) {
    return static_cast<quickhub::QuickButtonSfxKind>(s.target.sfxKind) !=
           quickhub::QuickButtonSfxKind::None;
}

inline bool changesSound(ButtonSkin const& s) {
    return hasCustomSound(s) || s.muteOriginal;
}

inline bool changesTransform(ButtonSkin const& s) {
    return std::abs(s.scale - 1.f) > 0.001f || std::abs(s.rotation) > 0.01f ||
           std::abs(s.offsetX) > 0.01f || std::abs(s.offsetY) > 0.01f;
}

inline bool changesLook(ButtonSkin const& s) {
    return hasFill(s) || (s.tintEnabled && !sameColor(s.tint, {255, 255, 255})) ||
           s.baseOpacity < 0.999f || s.hideOriginal || changesTransform(s);
}

inline bool changesAnything(ButtonSkin const& s) {
    return changesLook(s) || changesSound(s);
}

// copies where a button lives (not its quick hub look or sound) from src into dst.
inline void copyButtonAddress(quickhub::CustomQuickButton& dst, quickhub::CustomQuickButton const& src) {
    dst.name = src.name;
    dst.icon = src.icon;
    dst.labelText = src.labelText;
    dst.targetNodeId = src.targetNodeId;
    dst.parentId = src.parentId;
    dst.nodePath = src.nodePath;
    dst.idPath = src.idPath;
    dst.ownerClass = src.ownerClass;
    dst.sceneClass = src.sceneClass;
    dst.itemClass = src.itemClass;
    dst.listenerClass = src.listenerClass;
    dst.relX = src.relX;
    dst.relY = src.relY;
    dst.tag = src.tag;
}

// loader and editor share these limits; bad saved data falls back to defaults.
inline void sanitize(ButtonSkin& s) {
    auto inRange = [](int v, int hi) { return v >= 0 && v <= hi; };
    if (!inRange(static_cast<int>(s.scope), 2)) s.scope = SkinScope::Exact;
    if (!inRange(static_cast<int>(s.fill), 3)) s.fill = SkinFill::None;
    if (!inRange(static_cast<int>(s.fit), 2)) s.fit = SkinFit::Cover;
    if (!inRange(static_cast<int>(s.blend), 3)) s.blend = SkinBlend::Normal;

    s.fillOpacity = clampFinite(s.fillOpacity, 0.f, 1.f, 1.f);
    s.fillScale = clampFinite(s.fillScale, limits::kFillScaleMin, limits::kFillScaleMax, 1.f);
    s.fillRotation = wrapDegrees(s.fillRotation);
    s.fillOffsetX = clampFinite(s.fillOffsetX, -limits::kFillOffsetMax, limits::kFillOffsetMax, 0.f);
    s.fillOffsetY = clampFinite(s.fillOffsetY, -limits::kFillOffsetMax, limits::kFillOffsetMax, 0.f);

    s.baseOpacity = clampFinite(s.baseOpacity, 0.f, 1.f, 1.f);
    s.scale = clampFinite(s.scale, limits::kScaleMin, limits::kScaleMax, 1.f);
    s.rotation = wrapDegrees(s.rotation);
    s.offsetX = clampFinite(s.offsetX, -limits::kOffsetMax, limits::kOffsetMax, 0.f);
    s.offsetY = clampFinite(s.offsetY, -limits::kOffsetMax, limits::kOffsetMax, 0.f);

    auto& t = s.target;
    if (t.sfxKind < 0 || t.sfxKind > 3) t.sfxKind = 0;
    t.sfxVolume = clampFinite(t.sfxVolume, 0.f, 1.f, 1.f);
    t.sfxSpeed = clampFinite(t.sfxSpeed, 0.4f, 2.5f, 1.f);
    t.sfxStartMs = std::max(0, t.sfxStartMs);
    t.sfxEndMs = std::max(0, t.sfxEndMs);
    t.sfxFadeInMs = std::max(0, t.sfxFadeInMs);
    t.sfxFadeOutMs = std::max(0, t.sfxFadeOutMs);
    if (t.sfxEndMs != 0 && t.sfxEndMs <= t.sfxStartMs) t.sfxEndMs = 0;
}

} // namespace paimon::buttonskin
