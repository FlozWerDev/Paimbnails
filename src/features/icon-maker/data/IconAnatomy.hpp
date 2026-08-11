#pragma once

#include <Geode/Geode.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace paimon::icon_maker {

// One output frame; suffix matches the vanilla/MoreIcons frame-name tail.
struct SlotDef {
    std::string_view key;      // main/secondary/tertiary/extra/glow.
    std::string_view suffix;   // Vanilla frame suffix.
    std::string_view label;    // Short UI name.
    std::string_view hint;     // One-line help text.
    cocos2d::ccColor3B accent; // Chip/section color.
    bool optional = false;
};

struct AnatomyDef {
    IconType type;
    std::string_view vanillaPrefix;
    std::string_view folderName;     // MoreIcons folder.
    std::string_view displayName;
    int partCount = 1;               // 4 for robot/spider.
    std::vector<SlotDef> slots;      // Per-part slots; extra is part 1 only.
    int canvasUhd = 240;             // Square authoring canvas.
    int guideUhd = 120;              // Recommended icon extent.
};

// Returns nullptr for unsupported types.
AnatomyDef const* anatomyFor(IconType type);

std::vector<IconType> const& supportedTypes();

// Display name for a robot/spider part.
std::string_view partLabel(IconType type, int part);

// Project key: "main" or a part-qualified key such as "p2.glow".
std::string slotStorageKey(int part, std::string_view slotKey);

// Build the GD/MoreIcons frame name.
std::string frameName(std::string_view exportName, IconType type, int part,
                      std::string_view slotKey);

// Parse a known frame suffix into part and slot.
bool slotForSuffix(IconType type, std::string_view fullSuffix,
                   int& outPart, std::string& outSlotKey);

}
