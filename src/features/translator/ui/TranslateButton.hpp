#pragma once

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <functional>

namespace paimon::translate::ui {

enum class TranslateButtonState {
    Idle,     // blue body
    Busy,     // gray body while a request is in flight; the button is disabled
    Showing,  // green body while the translation is on screen
};

// GD account-style square: Geode's blue account blank with the translate bubbles on
// top. scale is applied to the sprite so the press animation keeps it.
CCMenuItemSpriteExtra* makeTranslateButton(std::function<void()> onPress, float scale = .5f);
void setTranslateButtonState(CCMenuItemSpriteExtra* button, TranslateButtonState state);

}
