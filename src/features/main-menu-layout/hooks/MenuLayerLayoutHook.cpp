#include "../services/MainMenuLayoutManager.hpp"
#include "LayoutEditorKeybind.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"

#include <Geode/modify/MenuLayer.hpp>
#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;

class $modify(PaimonMainMenuLayoutHook, MenuLayer) {
    static void onModify(auto& self) {
        // Last among Paimbnails hooks on MenuLayer::init (after node-ids and buttons).
        paimon::hooks::afterAllPaimonUiOrVeryLate(self, "MenuLayer::init");
    }

    $override
    bool init() {
        if (!MenuLayer::init()) {
            return false;
        }

        if (!paimon::modules::isEnabled("paimbnails.menulayout.menu")) return true;


        paimon::menu_layout::MainMenuLayoutManager::get().load();

        paimon::menu_layout::registerLayoutEditorKeybind(this);

        // Three distinct selectors: scheduleOnce with the same selector only
        // updates the existing timer (cocos2d-x), firing just one pass.
        this->scheduleOnce(schedule_selector(PaimonMainMenuLayoutHook::applyDeferredMenuLayout), 0.f);
        this->scheduleOnce(schedule_selector(PaimonMainMenuLayoutHook::applyDeferredMenuLayout2), 0.15f);
        this->scheduleOnce(schedule_selector(PaimonMainMenuLayoutHook::applyDeferredMenuLayout3), 0.5f);

        return true;
    }

    void applyDeferredMenuLayout(float) {
        paimon::menu_layout::MainMenuLayoutManager::get().captureDefaultsAndApply(this);
    }

    void applyDeferredMenuLayout2(float) { this->applyDeferredMenuLayout(0.f); }
    void applyDeferredMenuLayout3(float) { this->applyDeferredMenuLayout(0.f); }

    $override
    void onExit() {
        this->unschedule(schedule_selector(PaimonMainMenuLayoutHook::applyDeferredMenuLayout));
        this->unschedule(schedule_selector(PaimonMainMenuLayoutHook::applyDeferredMenuLayout2));
        this->unschedule(schedule_selector(PaimonMainMenuLayoutHook::applyDeferredMenuLayout3));
        MenuLayer::onExit();
    }
};
