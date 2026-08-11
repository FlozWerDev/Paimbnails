#pragma once

// Centralized scene locators using ID -> type -> heuristic fallbacks.

#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace paimon::compat {

struct LevelBrowserLocator {
    // ID search-menu, then the uppermost CCMenu.
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

    // ID background, then the first CCScale9Sprite.
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

struct GauntletLocator {
    // ID background, then the first direct child.
    static cocos2d::CCNode* findBackground(cocos2d::CCNode* layer) {
        if (!layer) return nullptr;

        if (auto bg = layer->getChildByID("background")) return bg;

        if (auto first = layer->getChildByType<cocos2d::CCNode>(0)) return first;

        return nullptr;
    }
};

struct InfoLayerLocator {
    struct PopupGeometry {
        cocos2d::CCSize  size   = cocos2d::CCSize(440.f, 290.f);
        cocos2d::CCPoint center = cocos2d::CCPointZero;
        bool found = false;
    };

    // ID background, then the first CCScale9Sprite.
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

struct LevelSelectLocator {
    // Recognize foreign mod nodes by their _spr-prefixed IDs so they stay visible.
    static bool isForeignModNode(cocos2d::CCNode* node) {
        if (!node) return false;
        std::string id = node->getID();
        if (id.empty()) return false;

        // Foreign prefixes whose backgrounds must not be hidden.
        static char const* const kForeignPrefixes[] = {
            "alphalaneous.",       // happy_textures, etc.
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

    // Hide vanilla backgrounds and GJGroundLayer, but leave other mods alone.
    static void hideVanillaBackground(cocos2d::CCNode* layer) {
        if (!layer) return;

        auto* children = layer->getChildren();
        if (!children) return;

        for (auto* node : CCArrayExt<cocos2d::CCNode*>(children)) {
            if (!node) continue;
            // Do not touch other mods' nodes.
            if (isForeignModNode(node)) continue;
            if (node->getZOrder() < -1) {
                node->setVisible(false);
            }
            if (typeinfo_cast<GJGroundLayer*>(node)) {
                node->setVisible(false);
            }
        }
    }
};

}
