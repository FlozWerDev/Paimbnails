// Editor entry point for the music panel: the Ctrl+M keybind and the playtest
// bridge that silences the panel while the level runs and brings it back after.

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/utils/Keyboard.hpp>

#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../editor-suite/EditorHelpers.hpp"
#include "../services/EditorMusicPlayer.hpp"
#include "../ui/EditorMusicPanel.hpp"

using namespace geode::prelude;

namespace {

bool editorMusicEnabled() {
    return paimon::modules::isEnabled("paimbnails.editormusic.editor");
}

} // namespace

class $modify(PaimonEditorMusicLayer, LevelEditorLayer) {
    $override
    bool init(GJGameLevel* level, bool noUI) {
        if (!LevelEditorLayer::init(level, noUI)) return false;
        if (noUI || !editorMusicEnabled()) return true;

        if (auto* panel = paimon::editormusic::EditorMusicPanel::create(this)) {
            this->addChild(panel, 9000);
        }
        return true;
    }

    $override
    void onPlaytest() {
        paimon::editormusic::EditorMusicPlayer::get().suspend();
        LevelEditorLayer::onPlaytest();
    }

    $override
    void onStopPlaytest() {
        LevelEditorLayer::onStopPlaytest();
        paimon::editormusic::EditorMusicPlayer::get().resumeFromSuspend();
    }
};

$execute {
    KeybindSettingPressedEventV3(Mod::get(), "editor-music-keybind").listen(
        +[](Keybind const&, bool down, bool repeat, double) {
            if (!down || repeat) return;
            if (!editorMusicEnabled()) return;
            if (!LevelEditorLayer::get()) return;
            if (paimon::editor::focusedTextInput()) return;
            if (auto* panel = paimon::editormusic::EditorMusicPanel::get()) panel->toggleOpen();
        }
    ).leak();
}
