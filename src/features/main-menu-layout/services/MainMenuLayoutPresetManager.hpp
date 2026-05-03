#pragma once

#include "MainMenuLayoutManager.hpp"

#include <map>

namespace paimon::menu_layout {

struct LayoutPreset {
    int slotIndex = 0;
    int itemCount = 0;
    LayoutSnapshot snapshot;
};

class MainMenuLayoutPresetManager {
public:
    static MainMenuLayoutPresetManager& get();

    void load();
    void save();

    void setPreset(int slotIndex, LayoutSnapshot const& snapshot);
    std::optional<LayoutPreset> getPreset(int slotIndex) const;
    bool hasPreset(int slotIndex) const;
    int maxUsedSlot() const;

private:
    MainMenuLayoutPresetManager() = default;

    std::filesystem::path configPath() const;
    void ensureLoaded();

    bool m_loaded = false;
    std::map<int, LayoutPreset> m_presets;
};

} // namespace paimon::menu_layout
