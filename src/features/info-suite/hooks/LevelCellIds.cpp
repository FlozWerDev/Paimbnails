// runs after every other paimbnails hook so the badge lands on top of the finished cell,
// re-anchored to whatever the name label ended up being instead of vanilla coordinates.

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

char const* const kBadgeID = "info-suite-id-badge"_spr;

bool levelIdsEnabled() {
    return paimon::info::subEnabled("info-mod-ids", "info-ids-levels", true);
}

bool inlinePlacement() {
    return paimon::info::moduleSetting<std::string>("info-ids-position", "inline") != "corner";
}

// the name label has no member in the bindings. node-ids names it, and if that
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

// keep the badge under the cell so recycling removes it with the other content.
void attachBadge(CCNode* cell, std::string const& title, int id) {
    if (!cell || id <= 0) return;

    auto* titleLabel = inlinePlacement() ? findTitleLabel(cell, title) : nullptr;
    CCPoint anchor(1.f, 0.f);
    CCPoint position(cell->getContentSize().width - 6.f, 4.f);
    if (titleLabel) {
        anchor = CCPoint(1.f, 0.5f);
        position = CCPoint(350.f, 44.f);
    }

    auto* badge = paimon::info::attachIdBadge(cell, kBadgeID, fmt::format("#{}", id), 0.4f, anchor, position);
    if (!badge) return;
    paimon::info::applyAdaptiveIdBadgeContrast(badge);
}

} // namespace

class $modify(PaimonInfoSuiteLevelCell, LevelCell) {
    static void onModify(auto& self) {
        paimon::hooks::veryLatePost(self, "LevelCell::loadFromLevel");
    }

    void loadFromLevel(GJGameLevel* level) {
        LevelCell::loadFromLevel(level);
        if (!levelIdsEnabled() || !level) return;
        attachBadge(this, std::string(level->m_levelName), level->m_levelID.value());
    }
};

class $modify(PaimonInfoSuiteLevelListCell, LevelListCell) {
    static void onModify(auto& self) {
        paimon::hooks::veryLatePost(self, "LevelListCell::loadFromList");
    }

    void loadFromList(GJLevelList* list) {
        LevelListCell::loadFromList(list);
        if (!levelIdsEnabled() || !list) return;
        attachBadge(this, std::string(list->m_listName), list->m_listID);
    }
};

class $modify(PaimonInfoSuiteMapPackCell, MapPackCell) {
    static void onModify(auto& self) {
        paimon::hooks::veryLatePost(self, "MapPackCell::loadFromMapPack");
    }

    void loadFromMapPack(GJMapPack* pack) {
        MapPackCell::loadFromMapPack(pack);
        if (!levelIdsEnabled() || !pack) return;
        attachBadge(this, std::string(pack->m_packName), pack->m_packID);
    }
};
