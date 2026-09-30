#pragma once

#include <Geode/Geode.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace paimon::icon_maker {

// one output frame; suffix matches the vanilla/moreicons frame-name tail.
struct SlotDef {
    std::string_view key;      // main/secondary/tertiary/extra/glow.
    std::string_view suffix;   // vanilla frame suffix.
    std::string_view label;    // short ui name.
    std::string_view hint;     // one-line help text.
    cocos2d::ccColor3B accent; // chip/section color.
    bool optional = false;
};

struct AnatomyDef {
    IconType type;
    std::string_view vanillaPrefix;
    std::string_view folderName;     // moreicons folder.
    std::string_view displayName;
    int partCount = 1;               // 4 for robot/spider.
    std::vector<SlotDef> slots;      // per-part slots; extra is part 1 only.
    int canvasUhd = 240;             // square authoring canvas.
    int guideUhd = 120;              // recommended icon extent.
};

// returns nullptr for unsupported types.
AnatomyDef const* anatomyFor(IconType type);

std::vector<IconType> const& supportedTypes();

// display name for a robot/spider part.
std::string_view partLabel(IconType type, int part);

// project key: "main" or a part-qualified key such as "p2.glow".
std::string slotStorageKey(int part, std::string_view slotKey);

// build the gd/moreicons frame name.
std::string frameName(std::string_view exportName, IconType type, int part,
                      std::string_view slotKey);

// parse a known frame suffix into part and slot.
bool slotForSuffix(IconType type, std::string_view fullSuffix,
                   int& outPart, std::string& outSlotKey);

}
