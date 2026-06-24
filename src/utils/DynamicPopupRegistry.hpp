#pragma once
#include <Geode/Geode.hpp>

namespace paimon {

// Dynamic Popup marker (via Geode user flag)

inline std::string const& dynamicPopupFlag() {
    static const std::string flag = geode::Mod::get()->getID() + "/dynamic-popup";
    return flag;
}

inline void markDynamicPopup(cocos2d::CCNode* node) {
    if (node) {
        node->setUserFlag(dynamicPopupFlag(), true);
    }
}

inline bool isDynamicPopup(cocos2d::CCNode* node) {
    return node && node->getUserFlag(dynamicPopupFlag());
}

inline void unmarkDynamicPopup(cocos2d::CCNode* node) {
    if (node) {
        node->setUserFlag(dynamicPopupFlag(), false);
    }
}

// origin of the last pressed button (world coords)

inline cocos2d::CCPoint& lastButtonOrigin() {
    static cocos2d::CCPoint s(-1.f, -1.f);
    return s;
}

inline void storeButtonOrigin(cocos2d::CCPoint const& worldPos) {
    lastButtonOrigin() = worldPos;
}

inline cocos2d::CCPoint consumeButtonOrigin() {
    auto pt = lastButtonOrigin();
    lastButtonOrigin() = cocos2d::CCPoint(-1.f, -1.f);
    return pt;
}

inline bool hasButtonOrigin() {
    auto& pt = lastButtonOrigin();
    return pt.x >= 0.f && pt.y >= 0.f;
}

} // namespace paimon
