#include <Geode/Geode.hpp>
#include <Geode/modify/GJListLayer.hpp>
#include <Geode/modify/LevelListCell.hpp>
#include <Geode/modify/GJScoreCell.hpp>
#include <Geode/modify/MapPackCell.hpp>

using namespace geode::prelude;

static bool isTransparentMode() {
    return Mod::get()->getSavedValue<bool>("transparent-list-mode", false);
}

// Fondo de lista invisible (solo el color, mantiene marcos/bordes)
class $modify(PaimonGJListLayer, GJListLayer) {
    bool init(BoomListView* listView, char const* title, cocos2d::ccColor4B color, float width, float height, int type) {
        if (!GJListLayer::init(listView, title, color, width, height, type)) {
            return false;
        }

        if (isTransparentMode()) {
            // Solo hacer invisible el color de fondo del CCLayerColor
            this->setOpacity(0);
        }

        return true;
    }
};

// Aplica fondo invisible a celdas (solo el CCLayerColor, no sus hijos)
static void applyTransparentCellBg(CCNode* self) {
    if (!isTransparentMode()) return;
    auto cell = static_cast<TableViewCell*>(self);
    if (!cell) return;

    // Ocultar el quad de color del CCLayerColor pero mantener contentSize para hijos.
    // typeinfo_cast: otros mods (HappyTextures, TextureLdr) pueden
    // reemplazar m_backgroundLayer por un sprite custom que no es
    // CCLayerColor. Llamar changeWidthAndHeight() sobre un tipo
    // distinto crashea.
    if (auto* bg = cell->m_backgroundLayer) {
        if (auto* bgColor = typeinfo_cast<cocos2d::CCLayerColor*>(bg)) {
            auto size = bgColor->getContentSize();
            bgColor->changeWidthAndHeight(0.f, 0.f);
            bgColor->setContentSize(size);
        } else {
            // Fallback robusto: ocultar el background entero. No
            // mantenemos contentSize porque otros mods que rebajan a
            // sprites suelen calcular su propio layout.
            bg->setVisible(false);
        }
    }
}

// LevelCell se maneja en LevelCell.cpp (PaimonLevelCell::applyTransparentMode)

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
