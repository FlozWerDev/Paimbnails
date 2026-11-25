#pragma once

#include <Geode/Geode.hpp>

namespace paimon::buttonskin {

// chooser or skin editor on screen: right-clicks stay with them.
bool isButtonSkinUiOpen();

// small chooser shown on right-click: add the button to the quick hub or customize it.
void openButtonActions(cocos2d::CCMenuItem* button, cocos2d::CCScene* scene);

} // namespace paimon::buttonskin
