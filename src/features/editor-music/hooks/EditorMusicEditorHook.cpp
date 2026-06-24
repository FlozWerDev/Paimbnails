// Wires EditorMusicPlayer into the editor: Ctrl+M toggles the mini-player cell.

#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/utils/Keyboard.hpp>
#include <Geode/binding/EditorUI.hpp>

#include "../services/EditorMusicPlayer.hpp"
#include "../ui/EditorMusicCell.hpp"
#include "../../../hooks/CustomSongWidgetLifecycle.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

using namespace geode::prelude;
using namespace paimon::editormusic;

namespace {
    constexpr char const* kCellID = "editor-music-cell";
}

class $modify(PaimonEditorMusicHook, LevelEditorLayer) {
    struct Fields {
        // Free our music channel when leaving the editor.
        ~Fields() { EditorMusicPlayer::get().stop(); }
    };

    $override
    bool init(GJGameLevel* level, bool p1) {
        if (!LevelEditorLayer::init(level, p1)) return false;
        // Force Fields construction so its destructor runs.
        m_fields.self();
        return true;
    }

    // FMOD fires delegates (CustomSongWidget::updateSongInfo) while playtest stops; mark teardown before the original.
    $override
    void onStopPlaytest() {
        paimon::csw::Lifecycle::beginEditorTeardown();
        LevelEditorLayer::onStopPlaytest();
        Loader::get()->queueInMainThread([] {
            if (paimon::isRuntimeShuttingDown()) {
                paimon::csw::Lifecycle::endEditorTeardown();
                return;
            }
            paimon::csw::Lifecycle::endEditorTeardown();
        });
    }

    $override
    void onPlaytest() {
        paimon::csw::Lifecycle::endEditorTeardown();
        LevelEditorLayer::onPlaytest();
    }
};

$execute {
    KeybindSettingPressedEventV3(Mod::get(), "editorMusicToggleKeybind").listen(
        +[](Keybind const&, bool down, bool repeat, double) {
            if (!down || repeat) return;
            if (!Mod::get()->getSettingValue<bool>("editorMusicEnable")) return;

            auto* editor = LevelEditorLayer::get();
            if (!editor) return;

            CCNode* parent = editor->m_editorUI
                ? static_cast<CCNode*>(editor->m_editorUI)
                : static_cast<CCNode*>(editor);

            // Toggle: if it exists, animate out (stops and removes on finish).
            if (auto* existing = parent->getChildByID(kCellID)) {
                if (auto* cell = typeinfo_cast<EditorMusicCell*>(existing)) {
                    cell->animateOut();
                } else {
                    existing->removeFromParent();
                    EditorMusicPlayer::get().stop();
                }
                return;
            }

            auto cell = EditorMusicCell::create();
            if (!cell) return;
            cell->setID(kCellID);
            auto win = CCDirector::get()->getWinSize();
            cell->setPosition({8.f, win.height - 8.f});
            parent->addChild(cell, 1000);

            EditorMusicPlayer::get().playNext();
            cell->refresh();
            cell->animateIn();
        }
    ).leak();
}
