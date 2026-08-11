// Opens the editor color-picker eyedropper via a keybind (default Ctrl+G).
// Gated behind the "editor-color-picker-enable" setting. Editor only.

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/utils/Keyboard.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>

#include "../ui/ColorPickerOverlay.hpp"
#include "../../editor-suite/EditorModule.hpp"

using namespace geode::prelude;

$execute {
    KeybindSettingPressedEventV3(Mod::get(), "editor-color-picker-keybind").listen(
        +[](Keybind const&, bool down, bool repeat, double) {
            if (!down || repeat) return;
            if (!paimon::editor::featureEnabled("editor-color-picker-enable")) return;
            if (!LevelEditorLayer::get()) return; // editor only
            paimon::editorcp::ColorPickerOverlay::show();
        }
    ).leak();
}
