#include "../services/DynamicTransitionManager.hpp"
#include <Geode/modify/CCMenuItem.hpp>
#include <Geode/modify/CCMenuItemSpriteExtra.hpp>

using namespace geode::prelude;
namespace dynamic = paimon::transitions::dynamic;

namespace {
unsigned s_activationDepth = 0;

struct ActivationGuard {
    explicit ActivationGuard(CCMenuItem* button) {
        if (s_activationDepth++ == 0) dynamic::captureButton(button);
    }
    ~ActivationGuard() {
        if (--s_activationDepth == 0 && !CCDirector::get()->getNextScene()) dynamic::deferDiscardButton();
    }
};
}

class $modify(PaimonDynamicMenuActivation, CCMenuItem) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPre("cocos2d::CCMenuItem::activate", geode::Priority::VeryEarly);
    }

    void activate() {
        ActivationGuard guard(this);
        CCMenuItem::activate();
    }
};

class $modify(PaimonDynamicSpriteActivation, CCMenuItemSpriteExtra) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPre("CCMenuItemSpriteExtra::activate", geode::Priority::VeryEarly);
    }

    void activate() {
        ActivationGuard guard(this);
        CCMenuItemSpriteExtra::activate();
    }
};
