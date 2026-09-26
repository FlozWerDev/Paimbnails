#pragma once
// Hidden rail on the icon kit holds hub buttons instead of the garage column.

#include <Geode/Geode.hpp>
#include <Geode/binding/GJGarageLayer.hpp>

#include <string>
#include <vector>

namespace paimon::garage_hub {

// Stores the button on the layer rail; label shows under the icon, lower order first.
void addButton(GJGarageLayer* layer, cocos2d::CCMenuItem* btn, std::string const& label, int order);

// Hidden layer rail, or null when nothing registered yet.
cocos2d::CCMenu* rail(GJGarageLayer* layer);

// Registered buttons, already sorted as the popup draws them.
std::vector<cocos2d::CCMenuItem*> entries(GJGarageLayer* layer);

std::string labelOf(cocos2d::CCNode* btn);

// Single visible button opening the hub popup.
void installHubButton(GJGarageLayer* layer);

}  // namespace paimon::garage_hub
