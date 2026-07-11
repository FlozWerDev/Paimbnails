// Optional confirmation before playtest.

#include "../EditorModule.hpp"

#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/ui/PopupManager.hpp>

using namespace geode::prelude;
using namespace paimon::editor;

class $modify(PaimonPlaytestConfirmUI, EditorUI) {
    $override
    void onPlaytest(CCObject* sender) {
        if (!moduleEnabled("editor-mod-playtest-confirm")) {
            return EditorUI::onPlaytest(sender);
        }
        // Only confirm when starting (not already playing)
        if (m_editorLayer && m_editorLayer->m_playbackMode != PlaybackMode::Not) {
            return EditorUI::onPlaytest(sender);
        }
        PopupManager::get().quickPopup(
            "Playtest",
            "Start playtesting this level?",
            "Cancel", "Play",
            [this, sender](FLAlertLayer*, bool ok) {
                if (ok) EditorUI::onPlaytest(sender);
            }
        ).showInstant();
    }
};
