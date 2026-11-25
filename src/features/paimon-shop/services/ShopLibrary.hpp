#pragma once
#include "../data/ShopPreset.hpp"
#include <filesystem>

namespace paimon::shop {

// local repository boundary. Presets use the same versioned envelope on export
// and import; a future server repository can use it without depending on the UI.
class ShopLibrary {
public:
    explicit ShopLibrary(std::filesystem::path directory) : m_directory(std::move(directory)) {}
    geode::Result<std::vector<Listing>> list() const;
    geode::Result<Preset> read(std::string const& id) const;
    geode::Result<std::string> add(Preset const& preset);
    geode::Result<> remove(std::string const& id);

private:
    std::filesystem::path m_directory;
    geode::Result<matjson::Value> readIndex() const;
};
} // namespace paimon::shop
