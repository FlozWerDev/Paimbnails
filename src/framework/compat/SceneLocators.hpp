#pragma once

// centralized scene locators using id -> type -> heuristic fallbacks.

#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace paimon::compat {

struct LevelBrowserLocator {
    // search-menu id, then the uppermost ccmenu.
    static cocos2d::CCMenu* findSearchMenu(cocos2d::CCNode* layer) {
        if (!layer) return nullptr;

        if (auto node = layer->getChildByID("search-menu")) {
            if (auto menu = typeinfo_cast<cocos2d::CCMenu*>(node)) return menu;
        }

        auto winH = cocos2d::CCDirector::get()->getWinSize().height;
        for (auto* child : CCArrayExt<cocos2d::CCNode*>(layer->getChildren())) {
            if (auto menu = typeinfo_cast<cocos2d::CCMenu*>(child)) {
                if (menu->getPosition().y > winH * 0.7f) {
                    return menu;
                }
            }
        }
        return nullptr;
    }
};

struct InfoLayerLocator {
    struct PopupGeometry {
        cocos2d::CCSize  size   = cocos2d::CCSize(440.f, 290.f);
        cocos2d::CCPoint center = cocos2d::CCPointZero;
        bool found = false;
    };

    // background id, then the first ccscale9sprite.
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

        return geo;
    }
};

struct LevelSelectLocator {
    // foreign mod nodes stay visible.
    static bool isForeignModNode(cocos2d::CCNode* node) {
        if (!node) return false;
        std::string id = node->getID();
        if (id.empty()) return false;

        static char const* const kForeignPrefixes[] = {
            "alphalaneous.",
            "geode.texture-loader/",
            "geode.node-ids/",
            "prevter.imageplus",
            "kampwski.",
            "hjfod.",
            "cvolton.",
            "cdc.",
            "eclipsemenu.",
            "globed.",
        };
        for (auto* prefix : kForeignPrefixes) {
            if (id.find(prefix) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    // hide vanilla backdrops; leave other mods alone.
    static void hideVanillaBackground(cocos2d::CCNode* layer) {
        if (!layer) return;

        auto* children = layer->getChildren();
        if (!children) return;

        for (auto* node : CCArrayExt<cocos2d::CCNode*>(children)) {
            if (!node || isForeignModNode(node)) continue;
            if (node->getZOrder() < -1 || typeinfo_cast<GJGroundLayer*>(node)) {
                node->setVisible(false);
            }
        }
    }
};

}
