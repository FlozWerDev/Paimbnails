#include "../services/MainMenuLayoutManager.hpp"
#include "LayoutEditorKeybind.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"

#include <Geode/modify/PauseLayer.hpp>
#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;

class $modify(PaimonPauseLayerLayoutHook, PauseLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterAllPaimonUiOrVeryLate(self, "PauseLayer::customSetup");
    }

    $override
    void customSetup() {
        PauseLayer::customSetup();

        if (!paimon::modules::isEnabled("paimbnails.menulayout.menu")) return;


        paimon::menu_layout::MainMenuLayoutManager::get().load();

        // Register the layout editor keybind for the pause menu only.
        paimon::menu_layout::registerLayoutEditorKeybind(this);

        // Re-apply after setup to catch buttons added by other hooks (capture, screenshot, etc.).
        // Three distinct selectors because scheduleOnce with the same selector only
        // updates the existing timer, so only one deferred pass would fire otherwise.
        this->scheduleOnce(schedule_selector(PaimonPauseLayerLayoutHook::applyDeferredPauseLayout), 0.f);
        this->scheduleOnce(schedule_selector(PaimonPauseLayerLayoutHook::applyDeferredPauseLayout2), 0.15f);
        this->scheduleOnce(schedule_selector(PaimonPauseLayerLayoutHook::applyDeferredPauseLayout3), 0.5f);
    }

    void applyDeferredPauseLayout(float) {
        paimon::menu_layout::MainMenuLayoutManager::get().captureDefaultsAndApply(this);
    }

    void applyDeferredPauseLayout2(float) { this->applyDeferredPauseLayout(0.f); }
    void applyDeferredPauseLayout3(float) { this->applyDeferredPauseLayout(0.f); }

    $override
    void onExit() {
        this->unschedule(schedule_selector(PaimonPauseLayerLayoutHook::applyDeferredPauseLayout));
        this->unschedule(schedule_selector(PaimonPauseLayerLayoutHook::applyDeferredPauseLayout2));
        this->unschedule(schedule_selector(PaimonPauseLayerLayoutHook::applyDeferredPauseLayout3));
        PauseLayer::onExit();
    }
};
