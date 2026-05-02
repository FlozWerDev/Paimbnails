#include <Geode/Geode.hpp>
#include <Geode/modify/LevelCell.hpp>
#include <Geode/binding/LevelBrowserLayer.hpp>
#include <Geode/binding/GJSearchObject.hpp>

using namespace geode::prelude;

// Detect if this LevelCell is inside a LevelBrowserLayer for "My Levels"
// (SearchType::MyLevels = 98). Walks up the parent chain.
static bool isInsideMyLevels(CCNode* cell) {
    CCNode* p = cell ? cell->getParent() : nullptr;
    for (int i = 0; i < 16 && p; ++i, p = p->getParent()) {
        if (auto layer = typeinfo_cast<LevelBrowserLayer*>(p)) {
            return layer->m_searchObject &&
                   layer->m_searchObject->m_searchType == SearchType::MyLevels;
        }
    }
    return false;
}

static void applyMyLevelsLayout(LevelCell* cell) {
    if (!cell || !cell->m_level) return;
    if (cell->getUserFlag("paimon-mylevels-laid-out"_spr)) return;
    cell->setUserFlag("paimon-mylevels-laid-out"_spr, true);

    auto* mainLayer = cell->m_mainLayer;
    if (!mainLayer) return;

    // Hide redundant secondary text labels and icons (compact one-line layout).
    // Keep level name, view button, and the difficulty/length icons visible.
    static constexpr const char* idsToHide[] = {
        "song-icon",
        "song-label",
        "info-button",
        "info-label",
        "verified-label",
        "verified-icon",
    };
    for (auto id : idsToHide) {
        if (auto n = mainLayer->getChildByID(id)) {
            n->setVisible(false);
        }
    }

    // Shift the level name down a bit (more centered) and the length info up
    // so they read as one neat row instead of two crowded rows.
    if (auto nameLabel = mainLayer->getChildByID("level-name")) {
        // Move name down slightly so it sits centered vertically.
        nameLabel->setPositionY(nameLabel->getPositionY() - 6.f);
        nameLabel->setScale(nameLabel->getScale() * 0.95f);
    }

    if (auto length = mainLayer->getChildByID("length-icon")) {
        length->setPositionY(length->getPositionY() + 14.f);
    }
    if (auto lengthLabel = mainLayer->getChildByID("length-label")) {
        lengthLabel->setPositionY(lengthLabel->getPositionY() + 14.f);
    }
}

class $modify(PaimonMyLevelsCell, LevelCell) {
    static void onModify(auto& self) {
        // Run after node-ids and after the main LevelCell hook so we can
        // safely query/modify the child layout.
        (void)self.setHookPriorityAfterPost("LevelCell::loadCustomLevelCell", "geode.node-ids");
        (void)self.setHookPriorityAfterPost("LevelCell::loadFromLevel", "geode.node-ids");
    }

    void loadCustomLevelCell() {
        this->setUserFlag("paimon-mylevels-laid-out"_spr, false);
        LevelCell::loadCustomLevelCell();
        if (isInsideMyLevels(this)) {
            applyMyLevelsLayout(this);
        }
    }

    void loadFromLevel(GJGameLevel* level) {
        this->setUserFlag("paimon-mylevels-laid-out"_spr, false);
        LevelCell::loadFromLevel(level);
        if (isInsideMyLevels(this)) {
            applyMyLevelsLayout(this);
        }
    }
};
