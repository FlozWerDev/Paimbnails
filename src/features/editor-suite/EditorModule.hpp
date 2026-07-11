#pragma once

// Shared gates for the Paimon Editor Suite.
// - suiteEnabled()           → master kill-switch for new editor-mod-* features
// - moduleEnabled(key)       → suite master && module bool (editor-mod-*)
// - legacyModuleEnabled(key) → standalone feature (rotate, collab, …); no suite gate

#include <Geode/loader/Mod.hpp>
#include <string_view>

namespace paimon::editor {

inline bool suiteEnabled() {
    auto* mod = geode::Mod::get();
    if (!mod || !mod->hasSetting("editor-suite-enable")) return true;
    return mod->getSettingValue<bool>("editor-suite-enable");
}

// For keys under editor-mod-*. Requires suite master ON.
inline bool moduleEnabled(std::string_view key) {
    auto* mod = geode::Mod::get();
    if (!mod) return false;
    if (!suiteEnabled()) return false;
    if (!mod->hasSetting(key)) return false;
    return mod->getSettingValue<bool>(std::string(key));
}

// Pre-existing features keep independent masters (not gated by suite).
inline bool legacyModuleEnabled(std::string_view key) {
    auto* mod = geode::Mod::get();
    if (!mod || !mod->hasSetting(key)) return false;
    return mod->getSettingValue<bool>(std::string(key));
}

template <typename T>
inline T moduleSetting(std::string_view key, T fallback = T{}) {
    auto* mod = geode::Mod::get();
    if (!mod || !mod->hasSetting(key)) return fallback;
    return mod->getSettingValue<T>(std::string(key));
}

} // namespace paimon::editor
