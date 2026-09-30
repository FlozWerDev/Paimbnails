// opens the editor color-picker eyedropper via a keybind (default ctrl+g).
// gated behind the "editor-color-picker-enable" setting. editor only.

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
            if (!LevelEditorLayer::get()) return;
            paimon::editorcp::ColorPickerOverlay::show();
        }
    ).leak();
}
