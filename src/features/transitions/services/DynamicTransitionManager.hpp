#pragma once

#include <Geode/Geode.hpp>
#include "DynamicTransitionMotion.hpp"

namespace paimon::transitions::dynamic {

inline constexpr char const* kEnabledSetting = "dynamic-transition-enabled";

Config getConfig();
Config animationConfig(Config config);
void saveConfig(Config config);
void resetConfig();
bool isEnabled();
void captureButton(cocos2d::CCMenuItem* button);
void discardButton();
void deferDiscardButton();
bool currentButton(cocos2d::CCScene* scene, Rect& rect, bool& back);
void beginBackInput();
void endBackInput();
bool hasBackInput();
void clearHistory();
Rect buttonRect(cocos2d::CCNode* button);
cocos2d::CCScene* createTransition(cocos2d::CCScene* destination,
    bool backwards = false, bool instant = false);

}
