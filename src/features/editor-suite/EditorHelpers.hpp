#pragma once

#include <Geode/Geode.hpp>
#include <Geode/binding/CCTextInputNode.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>

namespace paimon::editor {

cocos2d::CCMenu* hostToolbarMenu(EditorUI* ui);
void focusCameraOnPoint(LevelEditorLayer* lel, cocos2d::CCPoint objectSpace);

// keybinds check this so they don't fire while typing.
void setFocusedTextInput(CCTextInputNode* node);
geode::Ref<CCTextInputNode> focusedTextInput();

} // namespace paimon::editor
