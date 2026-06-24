#pragma once

// Downloads a synced icon's PNG/plist, caches them on disk, and registers them
// in More Icons. Header uses only IconType + std so it can be included anywhere.

#include <Geode/Geode.hpp>
#include "../GlobalIconTypes.hpp"
#include <string>
#include <filesystem>
#include <set>

namespace paimon::globalicon {

class GlobalIconStorage {
public:
    static GlobalIconStorage& get();

    // Is More Icons available? (degradation gate)
    static bool available();

    // Namespaced name used to register the icon in More Icons.
    static std::string registeredName(int accountID, GlobalIconSlot const& slot);

    // Ensure the icon is on disk and registered in More Icons.
    //   cb(success, registeredIconName) — iconName empty on failure.
    // Caller gets the IconInfo* via more_icons::getIcon(name, type).
    using EnsureCallback = geode::CopyableFunction<void(bool success, std::string const& iconName)>;
    void ensureIcon(int accountID, GlobalIconSlot const& slot, EnsureCallback cb);

private:
    GlobalIconStorage() = default;

    std::filesystem::path cacheDir(int accountID, std::string const& type) const;

    // Registers in More Icons (defined in the .cpp, which includes the API).
    void registerWithMoreIcons(std::string const& regName, GlobalIconSlot const& slot,
                               IconType type, std::filesystem::path const& png,
                               std::filesystem::path const& plist, EnsureCallback const& cb);

    // icons already registered this session (avoids duplicate addIcon).
    std::set<std::string> m_registered;
};

} // namespace paimon::globalicon
