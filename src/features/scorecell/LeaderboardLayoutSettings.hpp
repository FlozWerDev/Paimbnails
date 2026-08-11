#pragma once

#include <Geode/loader/Mod.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace paimon::scorecell {

enum class LeaderboardModule : uint8_t {
    Rank,
    PlayerIcon,
    PlayerName,
    Stars,
    Moons,
    Diamonds,
    SecretCoins,
    UserCoins,
    Demons,
    CreatorPoints,
};

struct LeaderboardModuleInfo {
    LeaderboardModule module;
    std::string_view key;
    std::string_view name;
    bool enabledByDefault;
};

inline constexpr std::array kLeaderboardModules = {
    LeaderboardModuleInfo{LeaderboardModule::Rank, "rank", "Rank", true},
    LeaderboardModuleInfo{LeaderboardModule::PlayerIcon, "player-icon", "Player Icon", true},
    LeaderboardModuleInfo{LeaderboardModule::PlayerName, "player-name", "Player Name", true},
    LeaderboardModuleInfo{LeaderboardModule::Stars, "stars", "Stars", true},
    LeaderboardModuleInfo{LeaderboardModule::Moons, "moons", "Moons", true},
    LeaderboardModuleInfo{LeaderboardModule::Diamonds, "diamonds", "Diamonds", true},
    LeaderboardModuleInfo{LeaderboardModule::SecretCoins, "secret-coins", "Secret Coins", true},
    LeaderboardModuleInfo{LeaderboardModule::UserCoins, "user-coins", "User Coins", true},
    LeaderboardModuleInfo{LeaderboardModule::Demons, "demons", "Demons", true},
    LeaderboardModuleInfo{LeaderboardModule::CreatorPoints, "creator-points", "Creator Points", true},
};

using LeaderboardModuleMask = uint16_t;

constexpr LeaderboardModuleMask moduleBit(LeaderboardModule module) {
    return LeaderboardModuleMask{1} << static_cast<uint8_t>(module);
}

struct LeaderboardPresetInfo {
    std::string_view key;
    std::string_view name;
    LeaderboardModuleMask modules;
};

inline constexpr LeaderboardModuleMask kAllLeaderboardModules =
    (LeaderboardModuleMask{1} << kLeaderboardModules.size()) - 1;

inline constexpr std::array kLeaderboardPresets = {
    LeaderboardPresetInfo{"balanced", "Balanced", kAllLeaderboardModules},
    LeaderboardPresetInfo{
        "compact", "Compact",
        moduleBit(LeaderboardModule::Rank) |
        moduleBit(LeaderboardModule::PlayerName) |
        moduleBit(LeaderboardModule::Stars) |
        moduleBit(LeaderboardModule::Demons)
    },
    LeaderboardPresetInfo{
        "clean", "Clean",
        moduleBit(LeaderboardModule::Rank) |
        moduleBit(LeaderboardModule::PlayerName)
    },
    LeaderboardPresetInfo{
        "creator", "Creator",
        moduleBit(LeaderboardModule::Rank) |
        moduleBit(LeaderboardModule::PlayerIcon) |
        moduleBit(LeaderboardModule::PlayerName) |
        moduleBit(LeaderboardModule::Stars) |
        moduleBit(LeaderboardModule::Demons) |
        moduleBit(LeaderboardModule::CreatorPoints)
    },
};

inline LeaderboardModuleInfo const& moduleInfo(LeaderboardModule module) {
    return kLeaderboardModules[static_cast<size_t>(module)];
}

inline std::string moduleSettingKey(LeaderboardModule module) {
    return "scorecell-layout-" + std::string(moduleInfo(module).key);
}

inline bool moduleEnabled(LeaderboardModule module) {
    auto const& info = moduleInfo(module);
    return geode::Mod::get()->getSavedValue<bool>(moduleSettingKey(module), info.enabledByDefault);
}

inline std::string leaderboardPreset() {
    return geode::Mod::get()->getSavedValue<std::string>("scorecell-layout-preset", "balanced");
}

inline LeaderboardPresetInfo const* presetInfo(std::string_view key) {
    for (auto const& preset : kLeaderboardPresets) {
        if (preset.key == key) return &preset;
    }
    return nullptr;
}

inline void applyLeaderboardPreset(std::string_view key) {
    auto preset = presetInfo(key);
    if (!preset) preset = &kLeaderboardPresets.front();

    for (auto const& info : kLeaderboardModules) {
        geode::Mod::get()->setSavedValue<bool>(
            moduleSettingKey(info.module),
            (preset->modules & moduleBit(info.module)) != 0
        );
    }
    geode::Mod::get()->setSavedValue<std::string>("scorecell-layout-preset", std::string(preset->key));
}

inline void setModuleEnabled(LeaderboardModule module, bool enabled) {
    geode::Mod::get()->setSavedValue<bool>(moduleSettingKey(module), enabled);
    geode::Mod::get()->setSavedValue<std::string>("scorecell-layout-preset", "custom");
}

} // namespace paimon::scorecell
