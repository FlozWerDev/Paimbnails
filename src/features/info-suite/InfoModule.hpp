#pragma once

// runtime gates for the suite; queried live, so toggles apply the next time a screen opens.

#include "InfoCompat.hpp"
#include <Geode/loader/Mod.hpp>
#include <string>
#include <string_view>

namespace paimon::info {

inline bool suiteEnabled() {
    auto* mod = geode::Mod::get();
    if (!mod || !mod->hasSetting("info-suite-enable")) return true;
    return mod->getSettingValue<bool>("info-suite-enable");
}

inline bool moduleEnabled(std::string_view key) {
    auto* mod = geode::Mod::get();
    if (!mod) return false;
    if (!suiteEnabled()) return false;
    if (!mod->hasSetting(key)) return false;
    if (compat::isCeded(key)) return false;
    return mod->getSettingValue<bool>(std::string(key));
}

template <typename T>
inline T moduleSetting(std::string_view key, T fallback = T{}) {
    auto* mod = geode::Mod::get();
    if (!mod || !mod->hasSetting(key)) return fallback;
    return mod->getSettingValue<T>(std::string(key));
}

// detail toggle: only meaningful while its owning module is on.
inline bool subEnabled(std::string_view moduleKey, std::string_view key,
                       bool fallback = true) {
    if (!moduleEnabled(moduleKey)) return false;
    return moduleSetting<bool>(key, fallback);
}

} // namespace paimon::info
