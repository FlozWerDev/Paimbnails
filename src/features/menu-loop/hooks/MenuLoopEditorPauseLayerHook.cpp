// MenuLoopEditorPauseLayerHook — runs the same "randomize / restore on
// exit" logic the reference mod (Menu Loop Randomizer) applies when the
// user quits back to the editor main screen.
//
// Without this hook, leaving the editor would keep playing the same
// menu loop from its last position only if GD itself decided to reuse
// it; with the hook we honour the user's preference between:
//   * menuLoopRandomizeOnEditorExit → shuffle immediately
//   * menuLoopRestoreOnEditorExit    → flag the manager so the stored
//                                      position is re-applied on
//                                      GameManager::fadeInMenuMusic

#include <Geode/modify/EditorPauseLayer.hpp>

#include "../services/MenuLoopManager.hpp"
#include "../services/MenuLoopControl.hpp"

using namespace geode::prelude;

class $modify(PaimonMenuLoopEditorPauseHook, EditorPauseLayer) {
    $override
    void onExitEditor(CCObject* sender) {
        auto& sm = paimon::menuloop::MenuLoopManager::get();
        const bool randomize = Mod::get()->getSavedValue<bool>(
            "menuLoopRandomizeOnEditorExit", false);
        const bool restore = Mod::get()->getSavedValue<bool>(
            "menuLoopRestoreOnEditorExit", true);
        if (randomize) {
            sm.setShouldRestoreMenuLoopPoint(false);
            paimon::menuloop::MenuLoopControl::shuffleSong();
        } else if (restore) {
            sm.setShouldRestoreMenuLoopPoint(true);
        }
        EditorPauseLayer::onExitEditor(sender);
    }
};
