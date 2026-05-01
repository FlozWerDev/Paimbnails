#include <Geode/Geode.hpp>
#include <Geode/modify/GJListLayer.hpp>
#include <Geode/modify/LevelCell.hpp>
#include <Geode/modify/LevelListCell.hpp>
#include <Geode/modify/GJScoreCell.hpp>
#include <Geode/modify/MapPackCell.hpp>

using namespace geode::prelude;

static bool isTransparentMode() {
    return Mod::get()->getSettingValue<bool>("transparent-list-mode");
}

// Fondo de lista transparente
class $modify(PaimonGJListLayer, GJListLayer) {
    void visit() {
        if (isTransparentMode()) {
            this->setOpacity(0);
        }
        GJListLayer::visit();
    }
};

// Aplica fondo transparente a celdas
static void applyTransparentCellBg(CCNode* self) {
    if (!isTransparentMode()) return;
    auto bg = static_cast<TableViewCell*>(self)->m_backgroundLayer;
    if (!bg) return;

    auto color = bg->getColor();
    if (color == ccColor3B{161, 88, 44}) {
        bg->setColor({0, 0, 0});
    } else if (color == ccColor3B{194, 114, 62}) {
        bg->setColor({80, 80, 80});
    }
    bg->setOpacity(0);
}

class $modify(PaimonTransparentLevelCell, LevelCell) {
    void loadCustomLevelCell() {
        LevelCell::loadCustomLevelCell();
        applyTransparentCellBg(this);
    }

    void loadFromLevel(GJGameLevel* level) {
        LevelCell::loadFromLevel(level);
        applyTransparentCellBg(this);
    }
};

class $modify(PaimonLevelListCell, LevelListCell) {
    void loadFromList(GJLevelList* list) {
        LevelListCell::loadFromList(list);
        applyTransparentCellBg(this);
    }
};

class $modify(PaimonGJScoreCell, GJScoreCell) {
    void loadFromScore(GJUserScore* score) {
        GJScoreCell::loadFromScore(score);
        applyTransparentCellBg(this);
    }
};

class $modify(PaimonMapPackCell, MapPackCell) {
    void loadFromMapPack(GJMapPack* pack) {
        MapPackCell::loadFromMapPack(pack);
        applyTransparentCellBg(this);
    }
};
