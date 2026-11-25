#pragma once

#include "AmbientMath.hpp"

#include <Geode/loader/Mod.hpp>

namespace paimon::ambient {

inline AmbientSettings loadSettings() {
    auto* mod = geode::Mod::get();
    AmbientSettings s;
    if (!mod) return s;
    AmbientSettings const d;
    s.enabled = mod->getSavedValue<bool>("hub-ambient-enabled", d.enabled);
    s.intensity = static_cast<float>(mod->getSavedValue<double>("hub-ambient-intensity", d.intensity));
    s.rimStrength = static_cast<float>(mod->getSavedValue<double>("hub-ambient-rim", d.rimStrength));
    s.shadowStrength = static_cast<float>(mod->getSavedValue<double>("hub-ambient-shadow", d.shadowStrength));
    s.tintStrength = static_cast<float>(mod->getSavedValue<double>("hub-ambient-tint", d.tintStrength));
    s.paletteStrength = static_cast<float>(mod->getSavedValue<double>("hub-ambient-palette", d.paletteStrength));
    s.affectButtons = mod->getSavedValue<bool>("ambient-scope-buttons", d.affectButtons);
    s.affectPanels = mod->getSavedValue<bool>("ambient-scope-panels", d.affectPanels);
    s.affectAssets = mod->getSavedValue<bool>("ambient-scope-assets", d.affectAssets);
    return sanitizeAmbientSettings(s);
}

inline void saveSettings(AmbientSettings const& settings) {
    auto* mod = geode::Mod::get();
    if (!mod) return;
    auto s = sanitizeAmbientSettings(settings);
    mod->setSavedValue<bool>("hub-ambient-enabled", s.enabled);
    mod->setSavedValue<double>("hub-ambient-intensity", s.intensity);
    mod->setSavedValue<double>("hub-ambient-rim", s.rimStrength);
    mod->setSavedValue<double>("hub-ambient-shadow", s.shadowStrength);
    mod->setSavedValue<double>("hub-ambient-tint", s.tintStrength);
    mod->setSavedValue<double>("hub-ambient-palette", s.paletteStrength);
    mod->setSavedValue<bool>("ambient-scope-buttons", s.affectButtons);
    mod->setSavedValue<bool>("ambient-scope-panels", s.affectPanels);
    mod->setSavedValue<bool>("ambient-scope-assets", s.affectAssets);
}

} // namespace paimon::ambient
