#pragma once
#include <Geode/Geode.hpp>

namespace paimon {

inline std::string const& dynamicPopupFlag() {
    static const std::string flag = geode::Mod::get()->getID() + "/dynamic-popup";
    return flag;
}

inline std::string const& managedDynamicPopupFlag() {
    static const std::string flag = geode::Mod::get()->getID() + "/dynamic-popup-managed";
    return flag;
}

inline void markDynamicPopup(cocos2d::CCNode* node) {
    if (node) {
        node->setUserFlag(dynamicPopupFlag(), true);
        node->setUserFlag(managedDynamicPopupFlag(), true);
    }
}

inline bool isManagedDynamicPopup(cocos2d::CCNode* node) {
    return node && node->getUserFlag(managedDynamicPopupFlag());
}

inline bool isDynamicPopup(cocos2d::CCNode* node) {
    return node && node->getUserFlag(dynamicPopupFlag());
}

inline void unmarkDynamicPopup(cocos2d::CCNode* node) {
    if (node) {
        node->setUserFlag(dynamicPopupFlag(), false);
    }
}

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
