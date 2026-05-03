#include "../services/MainMenuLayoutManager.hpp"
#include "../ui/MainMenuLayoutEditor.hpp"

#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/utils/Keyboard.hpp>

using namespace geode::prelude;

class $modify(PaimonMainMenuLayoutHook, MenuLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPost("MenuLayer::init", geode::Priority::Late);
    }

    $override
    bool init() {
        if (!MenuLayer::init()) {
            return false;
        }

        paimon::menu_layout::MainMenuLayoutManager::get().load();

        // Re-aplica despues del init para capturar tambien botones agregados por otros hooks.
        this->scheduleOnce(schedule_selector(PaimonMainMenuLayoutHook::applyDeferredMenuLayout), 0.f);
        this->scheduleOnce(schedule_selector(PaimonMainMenuLayoutHook::applyDeferredMenuLayout), 0.15f);
        this->scheduleOnce(schedule_selector(PaimonMainMenuLayoutHook::applyDeferredMenuLayout), 0.5f);

        this->addEventListener(
            KeybindSettingPressedEventV3(Mod::get(), "main-menu-layout-keybind"),
            [this](Keybind const&, bool down, bool repeat, double) {
                if (!down || repeat || !this->isRunning()) return;

                if (auto* active = paimon::menu_layout::MainMenuLayoutEditor::getActive()) {
                    if (active->getTargetLayer() == this) {
                        active->saveAndClose();
                    }
                    return;
                }

                paimon::menu_layout::MainMenuLayoutEditor::open(this);
            }
        );

        return true;
    }

    void applyDeferredMenuLayout(float) {
        paimon::menu_layout::MainMenuLayoutManager::get().captureDefaultsAndApply(this);
    }
};
