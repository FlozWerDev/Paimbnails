#include "../services/MainMenuLayoutManager.hpp"
#include "LayoutEditorKeybind.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"

#include <Geode/modify/MenuLayer.hpp>
#include <Geode/ui/NodeEvent.hpp>
#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;

class $modify(PaimonMainMenuLayoutHook, MenuLayer) {
    struct Fields {
        uint64_t shopRevision = 0;
        bool hasLayoutKeybind = false;
    };
    static void onModify(auto& self) {
        paimon::hooks::veryLatePost(self, "MenuLayer::init");
    }

    $override
    bool init() {
        if (!MenuLayer::init()) {
            return false;
        }

        if (!paimon::modules::isEnabled("paimbnails.menulayout.menu")) return true;


        auto& layouts = paimon::menu_layout::MainMenuLayoutManager::get();
        layouts.load();

        paimon::menu_layout::registerLayoutEditorKeybind(this);
        m_fields->hasLayoutKeybind = true;

        // applied on the spot: MenuLayer::init runs before the layer is first
        // drawn, so deferring this let the vanilla positions reach the screen
        // for a frame and the buttons appeared to slide into their custom place.
        // only the saved layout is applied here; defaults are still captured by
        // the deferred passes, once GD has finished positioning everything.
        layouts.applyCustom(this);
        m_fields->shopRevision = layouts.customRevision();

        // late passes only exist to catch buttons other hooks add afterwards.
        // distinct selectors: scheduleonce with the same selector just updates
        // the existing timer (cocos2d-x), firing a single pass.
        this->scheduleOnce(schedule_selector(PaimonMainMenuLayoutHook::applyDeferredMenuLayout2), 0.15f);
        this->scheduleOnce(schedule_selector(PaimonMainMenuLayoutHook::applyDeferredMenuLayout3), 0.5f);
        this->addOnEnterCallback([this] {
            this->handleEnter();
            return true;
        });
        this->addOnExitCallback([this] {
            this->handleExit();
            return true;
        });

        return true;
    }

    void applyDeferredMenuLayout(float) {
        paimon::menu_layout::MainMenuLayoutManager::get().captureDefaultsAndApply(this);
    }

    void handleEnter() {
        if (!paimon::modules::isEnabled("paimbnails.menulayout.menu")) return;

        auto& layouts = paimon::menu_layout::MainMenuLayoutManager::get();
        if (!m_fields->hasLayoutKeybind) {
            paimon::menu_layout::registerLayoutEditorKeybind(this);
            m_fields->hasLayoutKeybind = true;
        }
        // a new preset invalidates what is on screen: restore the vanilla
        // positions first, then re-apply the custom ones. Both happen before the
        // scene is drawn, so nothing is painted in the intermediate state.
        if (m_fields->shopRevision != layouts.customRevision()) {
            m_fields->shopRevision = layouts.customRevision();
            layouts.applyDefaults(this);
        }
        layouts.applyCustom(this);

        this->scheduleOnce(schedule_selector(PaimonMainMenuLayoutHook::applyDeferredMenuLayout2), 0.15f);
        this->scheduleOnce(schedule_selector(PaimonMainMenuLayoutHook::applyDeferredMenuLayout3), 0.5f);
    }

    void applyDeferredMenuLayout2(float) { this->applyDeferredMenuLayout(0.f); }
    void applyDeferredMenuLayout3(float) { this->applyDeferredMenuLayout(0.f); }

    void handleExit() {
        this->unschedule(schedule_selector(PaimonMainMenuLayoutHook::applyDeferredMenuLayout));
        this->unschedule(schedule_selector(PaimonMainMenuLayoutHook::applyDeferredMenuLayout2));
        this->unschedule(schedule_selector(PaimonMainMenuLayoutHook::applyDeferredMenuLayout3));
    }
};
