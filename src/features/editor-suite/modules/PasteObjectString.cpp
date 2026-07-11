// Paste object string from the system clipboard into the editor (BE idea).
// Keybind: Ctrl+Shift+P — only when module enabled and not typing in an input.

#include "../EditorModule.hpp"
#include "../EditorHelpers.hpp"

#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/utils/cocos.hpp>

#include "../../../utils/EditorContext.hpp"

using namespace geode::prelude;
using namespace paimon::editor;

namespace {
bool on() { return moduleEnabled("editor-mod-paste-object-string"); }
}

class $modify(PaimonPasteStringUI, EditorUI) {
    $override
    void keyDown(enumKeyCodes key, double timestamp) {
        if (on() && paimon::isEditorScene() && !focusedTextInput() && key == KEY_P) {
            auto* kd = CCKeyboardDispatcher::get();
            if (kd && kd->getControlKeyPressed() && kd->getShiftKeyPressed()) {
                auto clip = clipboard::read();
                if (clip.empty()) {
                    Notification::create("Clipboard empty", NotificationIcon::Info)->show();
                    return;
                }
                // Object strings typically contain many commas and end with ';'
                if (clip.find(';') == std::string::npos && clip.find(',') == std::string::npos) {
                    Notification::create("Clipboard is not an object string", NotificationIcon::Warning)->show();
                    return;
                }
                // pasteObjects handles undo when noUndo=false.
                auto* arr = this->pasteObjects(clip, true, false);
                if (arr && arr->count() > 0) {
                    Notification::create(
                        fmt::format("Pasted {} object(s)", arr->count()),
                        NotificationIcon::Success
                    )->show();
                } else {
                    Notification::create("Paste failed", NotificationIcon::Error)->show();
                }
                return;
            }
        }
        EditorUI::keyDown(key, timestamp);
    }
};
