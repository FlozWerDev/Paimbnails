// Select all left / right of camera (BetterEdit / Tinker keybind idea).
// Uses vanilla EditorUI::selectAllWithDirection — no reimplementation.

#include "../EditorModule.hpp"
#include "../EditorHelpers.hpp"

#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/ui/Notification.hpp>

#include "../../../utils/EditorContext.hpp"

using namespace geode::prelude;
using namespace paimon::editor;

namespace {
bool on() { return moduleEnabled("editor-mod-select-direction"); }
}

class $modify(PaimonSelectDirUI, EditorUI) {
    $override
    void keyDown(enumKeyCodes key, double timestamp) {
        if (on() && paimon::isEditorScene() && !focusedTextInput()) {
            auto* kd = CCKeyboardDispatcher::get();
            bool ctrl = kd && kd->getControlKeyPressed();
            bool shift = kd && kd->getShiftKeyPressed();
            // Ctrl+Shift+Left / Right = select all in that direction
            if (ctrl && shift) {
                if (key == KEY_Left) {
                    this->selectAllWithDirection(true);
                    return;
                }
                if (key == KEY_Right) {
                    this->selectAllWithDirection(false);
                    return;
                }
                // Ctrl+Shift+A = select all (vanilla has Ctrl+A often)
                if (key == KEY_A) {
                    this->selectAll();
                    return;
                }
            }
        }
        EditorUI::keyDown(key, timestamp);
    }
};
