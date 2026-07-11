#pragma once

#include <Geode/Geode.hpp>
#include <Geode/binding/CCTextInputNode.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <vector>

namespace paimon::editor {

// Selected objects as a stable vector (empty if none / no UI).
std::vector<GameObject*> getSelectedObjects(EditorUI* ui);

// Average center of selected objects in object-layer space.
cocos2d::CCPoint selectionCenter(EditorUI* ui);

// Move object layer so world point stays under screen center.
void focusCameraOnPoint(LevelEditorLayer* lel, cocos2d::CCPoint objectSpace);

// Move selected objects so their center lands on camera center.
void moveSelectionToCamera(EditorUI* ui);

// Focus camera on current selection center.
void focusCameraOnSelection(EditorUI* ui);

// Safe object-layer scale clamp used by scroll/zoom.
float clampZoom(float zoom, float minZ, float maxZ);

// Track focused CCTextInputNode for negate / keybind routing without extending
// the lifetime of a popup or retaining a stale pointer after it closes.
void setFocusedTextInput(CCTextInputNode* node);
geode::Ref<CCTextInputNode> focusedTextInput();

} // namespace paimon::editor
