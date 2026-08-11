// Visible IDs on browser cells: levels, level lists and map packs.
//
// Runs after every other Paimbnails hook so the badge lands on top of the
// finished cell, and re-anchors itself to whatever the name label ended up
// being instead of assuming vanilla coordinates.

#include "../InfoModule.hpp"
#include "../services/IdBadge.hpp"
#include "../../../framework/HookConventions.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GJLevelList.hpp>
#include <Geode/binding/GJMapPack.hpp>
#include <Geode/modify/LevelCell.hpp>
#include <Geode/modify/LevelListCell.hpp>
#include <Geode/modify/MapPackCell.hpp>

#include <string>

using namespace geode::prelude;

namespace {

char const* const kBadgeID = "info-suite-id-badge";

bool levelIdsEnabled() {
    return paimon::info::subEnabled("info-mod-ids", "info-ids-levels", true);
}

bool inlinePlacement() {
    return paimon::info::moduleSetting<std::string>("info-ids-position", "inline") != "corner";
}

// The name label has no member in the bindings. node-ids names it, and if that
// mod is missing we fall back to the label whose text is the title.
CCLabelBMFont* findTitleLabel(CCNode* cell, std::string const& title) {
    for (auto const* id : {"level-name", "list-name", "pack-name"}) {
        if (auto* label = typeinfo_cast<CCLabelBMFont*>(cell->getChildByIDRecursive(id))) {
            return label;
        }
    }
    if (title.empty()) return nullptr;

    auto* children = cell->getChildren();
    if (!children) return nullptr;
    for (auto* child : CCArrayExt<CCNode*>(children)) {
        auto* label = typeinfo_cast<CCLabelBMFont*>(child);
        if (label && label->getString() && title == label->getString()) return label;
    }
    return nullptr;
}

// Places the badge either after the title or in the bottom right corner. The
// badge is parented to the cell in both cases so recycling removes it with the
// rest of the cell contents.
void attachBadge(CCNode* cell, std::string const& title, int id) {
    if (!cell || id <= 0) return;
    if (cell->getChildByID(kBadgeID)) return;

    auto* badge = paimon::info::makeIdBadge(fmt::format("#{}", id));
    if (!badge) return;

    badge->setID(kBadgeID);
    paimon::info::applyAdaptiveIdBadgeContrast(badge);

    auto* titleLabel = inlinePlacement() ? findTitleLabel(cell, title) : nullptr;
    if (titleLabel) {
        badge->setAnchorPoint({1.f, 0.5f});
        badge->setPosition({350.f, 44.f});
    } else {
        auto cellSize = cell->getContentSize();
        badge->setAnchorPoint({1.f, 0.f});
        badge->setPosition({cellSize.width - 6.f, 4.f});
    }

    cell->addChild(badge, 100);
}

} // namespace

class $modify(PaimonInfoSuiteLevelCell, LevelCell) {
    static void onModify(auto& self) {
        paimon::hooks::afterAllPaimonUiOrVeryLate(self, "LevelCell::loadFromLevel");
    }

    void loadFromLevel(GJGameLevel* level) {
        LevelCell::loadFromLevel(level);
        if (!levelIdsEnabled() || !level) return;
        attachBadge(this, std::string(level->m_levelName), level->m_levelID.value());
    }
};

class $modify(PaimonInfoSuiteLevelListCell, LevelListCell) {
    static void onModify(auto& self) {
        paimon::hooks::afterAllPaimonUiOrVeryLate(self, "LevelListCell::loadFromList");
    }

    void loadFromList(GJLevelList* list) {
        LevelListCell::loadFromList(list);
        if (!levelIdsEnabled() || !list) return;
        attachBadge(this, std::string(list->m_listName), list->m_listID);
    }
};

class $modify(PaimonInfoSuiteMapPackCell, MapPackCell) {
    static void onModify(auto& self) {
        paimon::hooks::afterAllPaimonUiOrVeryLate(self, "MapPackCell::loadFromMapPack");
    }

    void loadFromMapPack(GJMapPack* pack) {
        MapPackCell::loadFromMapPack(pack);
        if (!levelIdsEnabled() || !pack) return;
        attachBadge(this, std::string(pack->m_packName), pack->m_packID);
    }
};
