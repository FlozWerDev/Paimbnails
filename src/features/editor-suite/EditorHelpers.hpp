#pragma once

#include <Geode/Geode.hpp>
#include <Geode/binding/CCTextInputNode.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>

namespace paimon::editor {

// Move object layer so world point stays under screen center.
void focusCameraOnPoint(LevelEditorLayer* lel, cocos2d::CCPoint objectSpace);

// Track focused CCTextInputNode for keybind routing without extending the
// lifetime of a popup or retaining a stale pointer after it closes. Editor
// keybinds check this so they don't fire while you are typing.
void setFocusedTextInput(CCTextInputNode* node);
geode::Ref<CCTextInputNode> focusedTextInput();

} // namespace paimon::editor
