#pragma once

#include "GlassRuntime.hpp"

#include <Geode/Geode.hpp>

class FLAlertLayer;
class GJDropDownLayer;
class ButtonSprite;

namespace cocos2d {
class CCLayer;
class CCScene;
}

namespace paimon::glass {

class GlassNode;

// puts a plate on `host` that replaces its GD art while the scope is
// active: a NineSlice / CCScale9Sprite (panels, buttons, insets, inputs), a
// CCSprite (UI buttons, slider grooves), a CCLayerColor (list frames) or a
// geode::ListBorders. Idempotent: returns the existing plate.
GlassNode* attachGlass(cocos2d::CCNode* host, GlassKind kind, GlassScope scope);

// the largest visible solid slice background directly inside m_mainLayer gets a
// panel. When it does, the popup's corner art and list borders make way,
// its lists, ListBorders, TextInputs, SongWidgets and nested UI boxes become
// wells, and UI buttons (ButtonSprite, BasedButtonSprite, checkboxes, utility
// glyphs and GD UI icon buttons) become glass buttons.
// returns the background that carries the plate, or nullptr.
cocos2d::CCNode* decoratePopup(FLAlertLayer* popup);
void decorateDropDown(GJDropDownLayer* dropdown);
void decorateUiRoot(cocos2d::CCNode* root);
void decorateButtonSprite(ButtonSprite* button);
// popups, UI layers, controls and buttons anywhere below `root` (strictly
// skipping gameplay layers), for enabling at runtime.
void decorateTree(cocos2d::CCNode* root);
void decorateScene(cocos2d::CCScene* scene);
void decorateEnteredLayer(cocos2d::CCLayer* layer);
void decorateRunningScene();

// re-evaluates a sprite button's glyph/shape/skin tint when its display frame
// changes dynamically (e.g. play <-> stop song toggle).
void refreshSpritePlate(GlassNode* plate, cocos2d::CCSprite* sprite);

// once per frame, before the scene draws: UI layers, popups, dropdowns,
// dialogs and overlay notifications get their glass plates (strictly skipping
// gameplay layers) and recover them after rebuilding content.
void watchPopups();

// sprite frames and textures are recreated when textures reload.
void invalidateSkinCache();

} // namespace paimon::glass
