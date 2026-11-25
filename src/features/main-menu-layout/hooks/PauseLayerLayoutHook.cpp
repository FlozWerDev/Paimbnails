#include "../services/MainMenuLayoutManager.hpp"
#include "../services/PauseMenuLayout.hpp"
#include "LayoutEditorKeybind.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"

#include <Geode/modify/PauseLayer.hpp>
#include "../../../framework/HookConventions.hpp"
#include "../../../utils/ActivePauseLayer.hpp"

using namespace geode::prelude;

class $modify(PaimonPauseLayerLayoutHook, PauseLayer) {
    static void onModify(auto& self) {
        paimon::hooks::veryLatePost(self, "PauseLayer::customSetup");
    }

    $override
    void customSetup() {
        PauseLayer::customSetup();

        if (!paimon::modules::isEnabled("paimbnails.menulayout.menu")) return;


        auto& layouts = paimon::menu_layout::MainMenuLayoutManager::get();
        layouts.load();

        paimon::menu_layout::registerLayoutEditorKeybind(this);

        // customSetup runs while the layer is being built, before it is drawn:
        // applying here means the pause menu never shows the vanilla positions
        // first and then slides into the custom ones. The vanilla sidebar is
        // arranged here too, since the deferred pass used to be the first thing
        // to move those buttons.
        paimon::menu_layout::arrangePauseMenu(this);
        layouts.applyCustom(this);

        // late passes only catch buttons other hooks add after customSetup
        // (capture, screenshot, etc.).
        this->scheduleOnce(schedule_selector(PaimonPauseLayerLayoutHook::applyDeferredPauseLayout2), 0.15f);
        this->scheduleOnce(schedule_selector(PaimonPauseLayerLayoutHook::applyDeferredPauseLayout3), 0.5f);
    }

    void applyDeferredPauseLayout(float) {
        if (!paimon::isPauseLayerOpen(this)) return;
        paimon::menu_layout::MainMenuLayoutManager::get().captureDefaultsAndApply(this);
    }

    void applyDeferredPauseLayout2(float) { this->applyDeferredPauseLayout(0.f); }
    void applyDeferredPauseLayout3(float) { this->applyDeferredPauseLayout(0.f); }

};
