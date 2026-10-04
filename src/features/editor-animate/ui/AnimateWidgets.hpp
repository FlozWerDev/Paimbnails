#pragma once

#include "../AnimateTypes.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>

#include <functional>
#include <initializer_list>
#include <string>

namespace paimon::animate::widgets {

cocos2d::ccColor3B clipColor(int index);
cocos2d::ccColor3B scaled(cocos2d::ccColor3B color, float factor);

// mod frame first, then gd frames, then an empty sprite so callers never null-check.
cocos2d::CCSprite* icon(char const* paim, std::initializer_list<char const*> fallbacks = {});
cocos2d::CCSprite* fitted(cocos2d::CCSprite* sprite, float size);

geode::EditorButtonSprite* toolSprite(cocos2d::CCSprite* top, geode::EditorBaseColor color, float size);
CCMenuItemSpriteExtra* toolButton(
    cocos2d::CCSprite* top, geode::EditorBaseColor color, float size, std::function<void()> onPress);

CCMenuItemSpriteExtra* iconButton(cocos2d::CCSprite* sprite, float size, std::function<void()> onPress);

std::string seconds(float value);
char const* modeName(PlayMode mode);
char const* modeIcon(PlayMode mode);
char const* endName(EndAction end);

void notifyError(std::string const& text);
void notifyOk(std::string const& text);

} // namespace paimon::animate::widgets
