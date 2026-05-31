#include "../services/MainMenuLayoutManager.hpp"
#include "LayoutEditorKeybind.hpp"

#include <Geode/modify/PauseLayer.hpp>

using namespace geode::prelude;

class $modify(PaimonPauseLayerLayoutHook, PauseLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPost("PauseLayer::customSetup", geode::Priority::Late);
    }

    $override
    void customSetup() {
        PauseLayer::customSetup();

        paimon::menu_layout::MainMenuLayoutManager::get().load();

        // Registra el keybind para abrir/guardar el editor de layout
        // SOLO en el menu de pausa.
        paimon::menu_layout::registerLayoutEditorKeybind(this);

        // Re-aplica despues del setup para capturar tambien botones que
        // agreguen otros hooks (capture, screenshot, etc.) sobre la pausa.
        this->scheduleOnce(schedule_selector(PaimonPauseLayerLayoutHook::applyDeferredPauseLayout), 0.f);
        this->scheduleOnce(schedule_selector(PaimonPauseLayerLayoutHook::applyDeferredPauseLayout), 0.15f);
        this->scheduleOnce(schedule_selector(PaimonPauseLayerLayoutHook::applyDeferredPauseLayout), 0.5f);
    }

    void applyDeferredPauseLayout(float) {
        paimon::menu_layout::MainMenuLayoutManager::get().captureDefaultsAndApply(this);
    }
};
