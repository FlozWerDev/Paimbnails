#include <Geode/modify/EditorPauseLayer.hpp>

#include "../../../framework/compat/ModCompat.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
#include "../services/MenuLoopManager.hpp"
#include "../services/MenuLoopControl.hpp"
#include "../../menu-music/services/MenuMusicPlayer.hpp"

using namespace geode::prelude;

class $modify(PaimonMenuLoopEditorPauseHook, EditorPauseLayer) {
    $override
    void onExitEditor(CCObject* sender) {
        if (!paimon::modules::isEnabled("paimbnails.menuloop.menu") ||
            paimon::compat::ModCompat::isMenuLoopRandomizerLoaded()) {
            EditorPauseLayer::onExitEditor(sender);
            return;
        }

        auto& sm = paimon::menuloop::MenuLoopManager::get();
        const bool randomize = Mod::get()->getSettingValue<bool>(
            "menuLoopRandomizeOnEditorExit");
        const bool restore = Mod::get()->getSettingValue<bool>(
            "menuLoopRestoreOnEditorExit");
        if (randomize) {
            sm.setShouldRestoreMenuLoopPoint(false);
            auto& player = paimon::menumusic::MenuMusicPlayer::get();
            if (player.isManagingPlayback()) player.playNext();
            else paimon::menuloop::MenuLoopControl::shuffleSong();
        } else if (restore) {
            sm.setShouldRestoreMenuLoopPoint(true);
        }
        EditorPauseLayer::onExitEditor(sender);
    }
};
