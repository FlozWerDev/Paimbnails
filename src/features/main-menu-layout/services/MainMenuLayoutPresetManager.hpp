#pragma once

#include "MainMenuLayoutManager.hpp"

#include <cstdint>
#include <map>
#include <string>

namespace paimon::menu_layout {

struct LayoutPreset {
    int slotIndex = 0;
    int itemCount = 0;
    int buttonCount = 0;
    int shapeCount = 0;
    std::string name;
    std::string sceneTag;
    int64_t updatedAt = 0;
    LayoutSnapshot snapshot;
};

class MainMenuLayoutPresetManager {
public:
    static MainMenuLayoutPresetManager& get();

    void load();
    void save();

    void setPreset(int slotIndex, LayoutSnapshot const& snapshot,
                   std::string const& name = "", std::string const& sceneTag = "");
    bool renamePreset(int slotIndex, std::string const& newName);
    bool deletePreset(int slotIndex);
    bool duplicatePreset(int fromSlot, int toSlot);
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
