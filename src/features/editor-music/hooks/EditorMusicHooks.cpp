// Editor entry points for Editor Music: Ctrl+M keybind, EditorUI toolbar button,
// EditorPauseLayer button, playtest suspend/resume bridge, and auto-play on enter.

#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/EditorPauseLayer.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>

#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../framework/HookConventions.hpp"
#include "../../editor-suite/EditorAssets.hpp"
#include "../../editor-suite/EditorHelpers.hpp"
#include "../../menu-music/services/MenuMusicLibrary.hpp"
#include "../services/EditorMusicPlayer.hpp"
#include "../ui/EditorMusicPanel.hpp"
#include "../ui/EditorMusicPickerPopup.hpp"

using namespace geode::prelude;

namespace {

bool editorMusicEnabled() {
    return paimon::modules::isEnabled("paimbnails.editormusic.editor");
}

void ensureAndTogglePanel() {
    if (!editorMusicEnabled()) return;
    auto* editor = LevelEditorLayer::get();
    if (!editor) return;

    auto* panel = paimon::editormusic::EditorMusicPanel::get();
    if (!panel) {
        panel = paimon::editormusic::EditorMusicPanel::create(editor);
        if (panel) editor->addChild(panel, 9000);
    }
    if (panel) panel->toggleOpen();
}

void openMusicMenu() {
    if (!editorMusicEnabled()) return;
    auto* scene = CCDirector::get()->getRunningScene();
    if (scene && scene->getChildByID("editor-music-picker"_spr)) return;
    if (auto* popup = paimon::editormusic::EditorMusicPickerPopup::create(0)) {
        popup->show();
    }
}

} // namespace

class $modify(PaimonEditorMusicLayer, LevelEditorLayer) {
    $override
    bool init(GJGameLevel* level, bool noUI) {
        if (!LevelEditorLayer::init(level, noUI)) return false;
        if (noUI || !editorMusicEnabled()) return true;

        paimon::menumusic::MenuMusicLibrary::get().load();
        paimon::menumusic::MenuMusicLibrary::get().syncDownloadedSongsAsync();

        auto& player = paimon::editormusic::EditorMusicPlayer::get();
        player.refreshQueue();

        if (auto* panel = paimon::editormusic::EditorMusicPanel::create(this)) {
            this->addChild(panel, 9000);
        }

        if (player.autoPlayOnEnter()) {
            if (!player.trackId().empty()) player.play(player.trackId());
            else player.playNext();
        }
        return true;
    }

    // Asymmetric on purpose: suspend before the engine takes the channel, resume after it gives it back.
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

class $modify(PaimonEditorMusicUI, EditorUI) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EditorUI::init");
    }

    $override
    bool init(LevelEditorLayer* editorLayer) {
        if (!EditorUI::init(editorLayer)) return false;
        if (!editorMusicEnabled()) return true;

        auto* button = paimon::editor::assets::squareButton(
            "",
            {"GJ_musicOnBtn_001.png", "GJ_playMusicBtn_001.png", "GJ_optionsBtn_001.png"},
            0.85f,
            EditorBaseColor::Aqua,
            [] { ensureAndTogglePanel(); },
            paimon::editor::toolbarToggleSize(this)
        );
        if (!button) return true;
        button->setID("editor-music-button"_spr);

        if (auto* menu = paimon::editor::hostToolbarMenu(this)) {
            menu->addChild(button);
            if (menu->getLayout()) menu->updateLayout();
        } else {
            auto* fallback = CCMenu::create();
            fallback->setID("editor-music-toolbar-menu"_spr);
            fallback->setPosition({28.f, CCDirector::get()->getWinSize().height - 200.f});
            fallback->addChild(button);
            addChild(fallback, 100);
        }
        return true;
    }

    $override
    void showUI(bool show) {
        EditorUI::showUI(show);
        if (auto* panel = paimon::editormusic::EditorMusicPanel::get()) {
            panel->applyVisibilityImmediate();
        }
    }
};

class $modify(PaimonEditorMusicPauseLayer, EditorPauseLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "EditorPauseLayer::init");
    }

    $override
    bool init(LevelEditorLayer* editor) {
        if (!EditorPauseLayer::init(editor)) return false;
        if (!editorMusicEnabled()) return true;

        auto* menu = typeinfo_cast<CCMenu*>(this->getChildByID("info-menu"));
        if (!menu) menu = typeinfo_cast<CCMenu*>(this->getChildByID("settings-menu"));
        if (!menu) return true;

        auto* sprite = ButtonSprite::create("Music", "goldFont.fnt", "GJ_button_05.png", 0.45f);
        if (!sprite) return true;
        auto* button = CCMenuItemSpriteExtra::create(
            sprite, this, menu_selector(PaimonEditorMusicPauseLayer::onOpenEditorMusic)
        );
        if (!button) return true;
        button->setID("editor-music-pause-btn"_spr);
        button->setScale(0.65f);
        menu->addChild(button);
        menu->updateLayout();
        return true;
    }

    void onOpenEditorMusic(CCObject*) {
        openMusicMenu();
    }
};

$execute {
    KeybindSettingPressedEventV3(Mod::get(), "editor-music-keybind").listen(
        +[](Keybind const&, bool down, bool repeat, double) {
            if (!down || repeat) return;
            if (!editorMusicEnabled()) return;
            if (!LevelEditorLayer::get()) return;
            if (paimon::editor::focusedTextInput()) return;
            ensureAndTogglePanel();
        }
    ).leak();
}
