#pragma once

#include <Geode/Geode.hpp>
#include <string_view>

namespace paimon {

// GD 2.209 puts the controls inside main-layer. Older layouts used the root.
inline cocos2d::CCMenu* findPauseButtonMenu(cocos2d::CCNode* root, std::string_view id) {
    if (!root) return nullptr;
    if (auto* main = root->getChildByID("main-layer")) {
        if (auto* menu = geode::cast::typeinfo_cast<cocos2d::CCMenu*>(main->getChildByID(id))) {
            return menu;
        }
    }
    return geode::cast::typeinfo_cast<cocos2d::CCMenu*>(root->getChildByID(id));
}

} // namespace paimon
