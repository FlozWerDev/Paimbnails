#include "../services/MainMenuLayoutManager.hpp"
#include "LayoutEditorKeybind.hpp"

#include <Geode/modify/MenuLayer.hpp>

using namespace geode::prelude;

class $modify(PaimonMainMenuLayoutHook, MenuLayer) {
    static void onModify(auto& self) {
        // El layout hook debe correr el ULTIMO entre todos los hooks
        // de Paimbnails sobre MenuLayer::init para que pueda
        // posicionar/agrupar todos los botones (incluyendo los que
        // otros hooks de Paimbnails añadieron). VeryLate + scheduleOnce
        // dentro de init garantiza que aplicamos el layout custom
        // despues de node-ids y de los hooks que registran botones.
        (void)self.setHookPriorityPost("MenuLayer::init", geode::Priority::VeryLate);
    }

    $override
    bool init() {
        if (!MenuLayer::init()) {
            return false;
        }

        paimon::menu_layout::MainMenuLayoutManager::get().load();

        // Registra el keybind para abrir/guardar el editor de layout
        // SOLO en el menu principal.
        paimon::menu_layout::registerLayoutEditorKeybind(this);

        // Re-aplica despues del init para capturar tambien botones agregados por otros hooks.
        this->scheduleOnce(schedule_selector(PaimonMainMenuLayoutHook::applyDeferredMenuLayout), 0.f);
        this->scheduleOnce(schedule_selector(PaimonMainMenuLayoutHook::applyDeferredMenuLayout), 0.15f);
        this->scheduleOnce(schedule_selector(PaimonMainMenuLayoutHook::applyDeferredMenuLayout), 0.5f);

        return true;
    }

    void applyDeferredMenuLayout(float) {
        paimon::menu_layout::MainMenuLayoutManager::get().captureDefaultsAndApply(this);
    }
};
