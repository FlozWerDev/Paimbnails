#pragma once

// SceneLocators.hpp — Localizadores centralizados de nodos por escena.
// Cada funcion encapsula la cadena de fallback (ID → tipo → heuristica)
// para que las correcciones de compatibilidad se hagan en un solo lugar.

#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace paimon::compat {

// ── LevelBrowserLocator ─────────────────────────────────────────────
// Para LevelListLayer, LevelSearchLayer, y capas similares de busqueda.

struct LevelBrowserLocator {
    // Busca el menu de busqueda en la parte superior de la capa.
    // Cadena: getChildByID("search-menu") → primer CCMenu con Y > 70% pantalla.
    static cocos2d::CCMenu* findSearchMenu(cocos2d::CCNode* layer) {
        if (!layer) return nullptr;

        if (auto node = layer->getChildByID("search-menu")) {
            if (auto menu = typeinfo_cast<cocos2d::CCMenu*>(node)) return menu;
        }

        auto winH = cocos2d::CCDirector::sharedDirector()->getWinSize().height;
        for (auto* child : CCArrayExt<cocos2d::CCNode*>(layer->getChildren())) {
            if (auto menu = typeinfo_cast<cocos2d::CCMenu*>(child)) {
                if (menu->getPosition().y > winH * 0.7f) {
                    return menu;
                }
            }
        }
        return nullptr;
    }

    // Busca un nodo de fondo generico.
    // Cadena: getChildByID("background") → primer CCScale9Sprite hijo.
    static cocos2d::CCNode* findBackground(cocos2d::CCNode* layer) {
        if (!layer) return nullptr;

        if (auto bg = layer->getChildByID("background")) return bg;

        for (auto* child : CCArrayExt<cocos2d::CCNode*>(layer->getChildren())) {
            if (typeinfo_cast<cocos2d::extension::CCScale9Sprite*>(child)) {
                return child;
            }
        }
        return nullptr;
    }
};

// ── GauntletLocator ─────────────────────────────────────────────────
// Para GauntletLayer.

struct GauntletLocator {
    // Busca el fondo de gauntlet.
    // Cadena: getChildByID("background") → primer hijo directo.
    static cocos2d::CCNode* findBackground(cocos2d::CCNode* layer) {
        if (!layer) return nullptr;

        if (auto bg = layer->getChildByID("background")) return bg;

        if (auto first = layer->getChildByType<cocos2d::CCNode>(0)) return first;

        return nullptr;
    }
};

// ── InfoLayerLocator ────────────────────────────────────────────────
// Para InfoLayer (popup de info de nivel).

struct InfoLayerLocator {
    struct PopupGeometry {
        cocos2d::CCSize  size   = cocos2d::CCSize(440.f, 290.f);
        cocos2d::CCPoint center = cocos2d::CCPointZero;
        bool found = false;
    };

    // Localiza el background del popup y devuelve su geometria.
    // Cadena: getChildByID("background") → primer CCScale9Sprite hijo.
    static PopupGeometry findPopupGeometry(cocos2d::CCNode* mainLayer) {
        if (!mainLayer) return {};

        auto layerSize = mainLayer->getContentSize();
        PopupGeometry geo;
        geo.center = ccp(layerSize.width * 0.5f, layerSize.height * 0.5f);

        if (auto bg = mainLayer->getChildByID("background")) {
            geo.size   = bg->getScaledContentSize();
            geo.center = bg->getPosition();
            geo.found  = true;
            return geo;
        }

        for (auto* child : CCArrayExt<cocos2d::CCNode*>(mainLayer->getChildren())) {
            if (typeinfo_cast<cocos2d::extension::CCScale9Sprite*>(child)) {
                geo.size   = child->getScaledContentSize();
                geo.center = child->getPosition();
                geo.found  = true;
                return geo;
            }
        }

        geo.found = false;
        return geo;
    }
};

// ── LevelSelectLocator ──────────────────────────────────────────────
// Para LevelSelectLayer (los main levels).

struct LevelSelectLocator {
    // Oculta nodos de fondo vanilla (zOrder < -1) y GJGroundLayer.
    static void hideVanillaBackground(cocos2d::CCNode* layer) {
        if (!layer) return;

        auto* children = layer->getChildren();
        if (!children) return;

        for (auto* node : CCArrayExt<cocos2d::CCNode*>(children)) {
            if (!node) continue;
            if (node->getZOrder() < -1) {
                node->setVisible(false);
            }
            if (typeinfo_cast<GJGroundLayer*>(node)) {
                node->setVisible(false);
            }
        }
    }
};

} // namespace paimon::compat
