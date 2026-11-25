#pragma once

#include "GlassQuality.hpp"

#include <Geode/loader/Mod.hpp>

namespace paimon::glass {

// saved-value storage for the Liquid Glass settings. The pure value type
// and sanitizer live in GlassQuality.hpp. The "hub-glass-*" keys predate
// the global material and stay so existing configs keep working.
inline GlassSettings loadSettings() {
    auto* mod = geode::Mod::get();
    GlassSettings s;
    if (!mod) return s;
    GlassSettings const d;
    s.enabled = mod->getSavedValue<bool>("hub-glass-enabled", d.enabled);
    s.tier = static_cast<int>(mod->getSavedValue<int64_t>("hub-glass-tier", d.tier));
    s.blur = static_cast<float>(mod->getSavedValue<double>("hub-glass-blur", d.blur));
    s.refraction = static_cast<float>(mod->getSavedValue<double>("hub-glass-refraction", d.refraction));
    s.tintOpacity = static_cast<float>(mod->getSavedValue<double>("hub-glass-tint-opacity", d.tintOpacity));
    s.rim = static_cast<float>(mod->getSavedValue<double>("hub-glass-rim", d.rim));
    s.cornerRadius = static_cast<float>(mod->getSavedValue<double>("hub-glass-radius", d.cornerRadius));
    s.tintR = static_cast<int>(mod->getSavedValue<int64_t>("hub-glass-tint-r", d.tintR));
    s.tintG = static_cast<int>(mod->getSavedValue<int64_t>("hub-glass-tint-g", d.tintG));
    s.tintB = static_cast<int>(mod->getSavedValue<int64_t>("hub-glass-tint-b", d.tintB));
    s.popups = mod->getSavedValue<bool>("glass-scope-popups", d.popups);
    s.buttons = mod->getSavedValue<bool>("glass-scope-buttons", d.buttons);
    s.coloredButtons = mod->getSavedValue<bool>("glass-colored-buttons", d.coloredButtons);
    return sanitizeGlassSettings(s);
}

inline void saveSettings(GlassSettings const& settings) {
    auto* mod = geode::Mod::get();
    if (!mod) return;
    auto s = sanitizeGlassSettings(settings);
    mod->setSavedValue<bool>("hub-glass-enabled", s.enabled);
    mod->setSavedValue<int64_t>("hub-glass-tier", s.tier);
    mod->setSavedValue<double>("hub-glass-blur", s.blur);
    mod->setSavedValue<double>("hub-glass-refraction", s.refraction);
    mod->setSavedValue<double>("hub-glass-tint-opacity", s.tintOpacity);
    mod->setSavedValue<double>("hub-glass-rim", s.rim);
    mod->setSavedValue<double>("hub-glass-radius", s.cornerRadius);
    mod->setSavedValue<int64_t>("hub-glass-tint-r", s.tintR);
    mod->setSavedValue<int64_t>("hub-glass-tint-g", s.tintG);
    mod->setSavedValue<int64_t>("hub-glass-tint-b", s.tintB);
    mod->setSavedValue<bool>("glass-scope-popups", s.popups);
    mod->setSavedValue<bool>("glass-scope-buttons", s.buttons);
    mod->setSavedValue<bool>("glass-colored-buttons", s.coloredButtons);
}

inline bool isMobilePlatform() {
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    return true;
#else
    return false;
#endif
}

} // namespace paimon::glass
