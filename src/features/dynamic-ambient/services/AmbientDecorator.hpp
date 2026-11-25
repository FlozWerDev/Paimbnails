#pragma once

#include "AmbientNode.hpp"
#include "AmbientRuntime.hpp"

#include <Geode/Geode.hpp>

class ButtonSprite;
class FLAlertLayer;
class GJDropDownLayer;

namespace cocos2d {
class CCLayer;
class CCNode;
class CCScene;
}

namespace paimon::ambient {

class AmbientNode;

// Returns true if `node` belongs to a button customized by Button Skins or
// another excluded UI element (backgrounds, gameplay, Button Skin editor).
bool isNodeIgnoredForAmbient(cocos2d::CCNode* node);

AmbientNode* attachAmbient(cocos2d::CCNode* host, AmbientKind kind, AmbientScope scope);

cocos2d::CCNode* decoratePopup(FLAlertLayer* popup);
void decorateDropDown(GJDropDownLayer* dropdown);
void decorateUiRoot(cocos2d::CCNode* root);
void decorateButtonSprite(ButtonSprite* button, bool immediateReveal = false);
void decorateTree(cocos2d::CCNode* root);
void decorateScene(cocos2d::CCScene* scene);
void decorateEnteredLayer(cocos2d::CCLayer* layer);
void decorateRunningScene();

void watchPopups();

} // namespace paimon::ambient
