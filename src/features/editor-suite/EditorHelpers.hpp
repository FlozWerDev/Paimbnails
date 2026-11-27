#pragma once

#include <Geode/Geode.hpp>
#include <Geode/binding/CCTextInputNode.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <string>

namespace paimon::editor {

cocos2d::CCMenu* hostToolbarMenu(EditorUI* ui);
// unscaled height of the vanilla swipe/rotate toggles, 0 if unknown.
float toolbarToggleSize(EditorUI* ui);
// registers an editor ui node so playtest / EditorUI::showUI hides and restores it.
void registerHideableUI(EditorUI* ui, cocos2d::CCNode* node);
// appends a button to hostToolbarMenu (or a fallback menu) and registers it with hideable UI.
void addToolbarButton(
    EditorUI* ui,
    cocos2d::CCMenuItem* button,
    std::string const& fallbackMenuID,
    cocos2d::CCPoint fallbackPosition
);
void focusCameraOnPoint(LevelEditorLayer* lel, cocos2d::CCPoint objectSpace);

// true when a modal popup/pause overlay is visibly open on top of the editor.
bool hasBlockingModal(LevelEditorLayer* lel, EditorUI* ui = nullptr);

// keybinds check this so they don't fire while typing.
void setFocusedTextInput(CCTextInputNode* node);
geode::Ref<CCTextInputNode> focusedTextInput();

} // namespace paimon::editor
