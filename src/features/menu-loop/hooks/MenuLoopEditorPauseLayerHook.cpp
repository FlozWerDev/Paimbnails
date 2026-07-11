#include <Geode/modify/EditorPauseLayer.hpp>

#include "../../../framework/compat/ModCompat.hpp"
#include "../services/MenuLoopManager.hpp"
#include "../services/MenuLoopControl.hpp"

using namespace geode::prelude;

class $modify(PaimonMenuLoopEditorPauseHook, EditorPauseLayer) {
    $override
    void onExitEditor(CCObject* sender) {
        if (paimon::compat::ModCompat::isMenuLoopRandomizerLoaded()) {
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
            paimon::menuloop::MenuLoopControl::shuffleSong();
        } else if (restore) {
            sm.setShouldRestoreMenuLoopPoint(true);
        }
        EditorPauseLayer::onExitEditor(sender);
    }
};
