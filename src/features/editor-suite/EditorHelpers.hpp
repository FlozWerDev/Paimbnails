#pragma once

#include <Geode/Geode.hpp>
#include <Geode/binding/CCTextInputNode.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>

namespace paimon::editor {

void focusCameraOnPoint(LevelEditorLayer* lel, cocos2d::CCPoint objectSpace);

// Tracks focused input for keybind routing without extending any popup's lifetime.
// Keybinds check this so they don't fire while typing.
void setFocusedTextInput(CCTextInputNode* node);
geode::Ref<CCTextInputNode> focusedTextInput();

} // namespace paimon::editor
