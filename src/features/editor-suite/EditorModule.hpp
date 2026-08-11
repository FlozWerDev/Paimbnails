#pragma once

// Shared gate for the editor features: featureEnabled(key) reads the mod.json
// bool without assuming the setting exists (older configs, stripped builds).

#include <Geode/loader/Mod.hpp>
#include <string_view>

namespace paimon::editor {

inline bool featureEnabled(std::string_view key) {
    auto* mod = geode::Mod::get();
    if (!mod || !mod->hasSetting(key)) return false;
    return mod->getSettingValue<bool>(std::string(key));
}

} // namespace paimon::editor
