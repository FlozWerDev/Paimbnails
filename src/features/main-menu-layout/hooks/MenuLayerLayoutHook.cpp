#include "../services/MainMenuLayoutManager.hpp"
#include "LayoutEditorKeybind.hpp"

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

        paimon::menu_layout::MainMenuLayoutManager::get().load();

        // Register the layout editor keybind for the main menu only.
        paimon::menu_layout::registerLayoutEditorKeybind(this);

        // Re-apply after init to catch buttons added by other hooks.
        // Three distinct selectors are used because scheduleOnce with the
        // same selector only updates the existing timer (cocos2d-x), so only
        // one of three deferred passes would fire otherwise.
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
