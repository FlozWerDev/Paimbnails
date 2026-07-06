// Entry/exit animations and configurable background blur for the mod's popups.

#include "../framework/HookConventions.hpp"
#include <Geode/modify/FLAlertLayer.hpp>
#include <Geode/modify/ProfilePage.hpp>
#include <Geode/modify/SetupTriggerPopup.hpp>
#include <Geode/modify/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/loader/Mod.hpp>
#include "../utils/DynamicPopupRegistry.hpp"
#include "../blur/PopupBlurService.hpp"
#include "../core/Settings.hpp"
#include "../core/RuntimeLifecycle.hpp"
#include <algorithm>
#include <cmath>

using namespace geode::prelude;
using namespace cocos2d;

namespace {
bool isEditorContextActive() {
    auto* director = CCDirector::get();
    if (!director) return false;

    auto* scene = director->getRunningScene();
    if (!scene) return false;

    return scene->getChildByType<LevelEditorLayer>(0) != nullptr ||
           scene->getChildByType<EditorUI>(0) != nullptr;
}
} // namespace

// Capture button position. Can't use MenuItemActivatedEvent (Geode 5.6.0): we
// need to run before activate() (setHookPriorityPre) to capture the position
// before the popup opens, and that event is post-activate.
class $modify(PaimonButtonOriginCapture, CCMenuItemSpriteExtra) {
    static void onModify(auto& self) {
        // VeryEarly (not First) so mods that must run literally first keep their slot.
        (void)self.setHookPriorityPre("CCMenuItemSpriteExtra::activate", geode::Priority::VeryEarly);
    }

    $override
    void activate() {
        // Full editor isolation: pure passthrough here, so the mod doesn't join
        // the editor's button activate() chain (incl. ColorSelectPopup's, which
        // triggers the known crash).
        if (isEditorContextActive()) {
            CCMenuItemSpriteExtra::activate();
            return;
        }
        // Cache the setting to avoid getSettingValue()'s mutex on every button
        // click; the listener refreshes the cache when the user changes it.
        static bool s_enabled = Mod::get()->getSettingValue<bool>("dynamic-popup-enabled");
        static auto s_listener = []{
            geode::listenForSettingChanges<bool>("dynamic-popup-enabled", [](bool v){
                s_enabled = v;
            });
            return 0;
        }();
        (void)s_listener;
        if (s_enabled && this->getParent()) {
            auto sz = this->getContentSize();
            if (sz.width > 0.f && sz.height > 0.f) {
                paimon::storeButtonOrigin(
                    this->convertToWorldSpace({sz.width / 2.f, sz.height / 2.f})
                );
            }
        }
        CCMenuItemSpriteExtra::activate();
    }
};

class $modify(PaimonDynamicPopupHook, FLAlertLayer) {

    struct Fields {
        bool    m_exiting = false;
        CCPoint m_origin  = {-1.f, -1.f};
        CCPoint m_finalPos= {0.f, 0.f};
        Ref<FLAlertLayer> m_exitGuard = nullptr;
        Ref<CCNode> m_blurNode = nullptr;
    };

    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "FLAlertLayer::show");
        paimon::hooks::afterNodeIdsOrLate(self, "FLAlertLayer::keyBackClicked");
        paimon::hooks::afterNodeIdsOrLate(self, "FLAlertLayer::removeFromParentAndCleanup");
        paimon::hooks::afterNodeIdsOrLate(self, "FLAlertLayer::removeFromParent");
    }

    bool isPaimonPopup() {
        return paimon::isDynamicPopup(this);
    }

    bool isAnyGamePopup() {
        return this->m_mainLayer != nullptr;
    }

    bool shouldAnimatePopup() {
        return isPaimonPopup()
            && !isEditorContextActive()
            && !paimon::settings::smoothui::reducedMotion()
            && Mod::get()->getSettingValue<bool>("dynamic-popup-enabled");
    }

    bool shouldAnimateExit() {
        return shouldAnimatePopup() && Mod::get()->getSettingValue<bool>("dynamic-exit-enabled");
    }

    // Only blur the mod's own popups; blurring any FLAlertLayer would break
    // other mods' popups (Globed, EclipseMenu, GDShare, etc.).
    bool shouldApplyPopupBlur() {
        return isPaimonPopup() && paimon::popupblur::getConfig().enabled && !isEditorContextActive();
    }

    float getSpeed() {
        float speed = static_cast<float>(
            Mod::get()->getSavedValue<double>("dynamic-popup-speed", 1.0)
        );
        if (!(speed > 0.f)) {
            speed = 1.0f;
        }
        if (paimon::settings::smoothui::enabled()) {
            speed *= static_cast<float>(std::clamp(
                paimon::settings::smoothui::globalSpeed(), 0.35, 2.5));
        }
        return std::max(0.1f, speed);
    }

    float getExitSpeed() {
        float speed = static_cast<float>(
            Mod::get()->getSavedValue<double>("dynamic-exit-speed", 1.0)
        );
        if (!(speed > 0.f)) {
            speed = 1.0f;
        }
        if (paimon::settings::smoothui::enabled()) {
            speed *= static_cast<float>(std::clamp(
                paimon::settings::smoothui::globalSpeed(), 0.35, 2.5));
        }
        return std::max(0.1f, speed);
    }

    std::string getStyle() {
        return Mod::get()->getSavedValue<std::string>("dynamic-popup-style", "paimonUI");
    }

    void removePopupBlurNode() {
        // Don't remove the blur synchronously: when called from onExit() (via the
        // parent's detachChild during removeFromParent), mutating the same parent's
        // child list corrupts the CCArray cocos is about to touch on return. Defer
        // to the next main-thread tick, after the popup's detachChild has finished.
        if (Ref<CCNode> blur = m_fields->m_blurNode) {
            geode::Loader::get()->queueInMainThread([blur]() {
                if (auto* node = blur.data(); node && node->getParent()) {
                    node->removeFromParent();
                }
            });
        }
        m_fields->m_blurNode = nullptr;
        // Unregister from the shared service
        paimon::popupblur::cleanup(this);
    }

    // Fade out the blur node and remove it when done. Unregisters immediately
    // (before the fade) so popups opened during the fade don't see this blur as
    // active; the CCSequence self-removes the node if the popup dies mid-fade.
    void fadeOutAndRemoveBlur(float duration) {
        paimon::popupblur::cleanupWithFade(this, duration);

        if (!m_fields->m_blurNode) {
            return;
        }
        auto* node = m_fields->m_blurNode.data();
        // Drop the field ref; the node stays alive as a child until removeFromParent.
        m_fields->m_blurNode = nullptr;

        if (!node || !node->getParent()) return;

        if (duration <= 0.01f) {
            // Defer to next tick — see removePopupBlurNode.
            Ref<CCNode> keepAlive = Ref<CCNode>(node);
            geode::Loader::get()->queueInMainThread([keepAlive]() {
                if (auto* n = keepAlive.data(); n && n->getParent()) {
                    n->removeFromParent();
                }
            });
            return;
        }

        node->stopAllActions();
        node->runAction(CCSequence::create(
            CCFadeTo::create(duration, 0),
            CCCallFunc::create(node, callfunc_selector(CCNode::removeFromParent)),
            nullptr
        ));
    }

    void clearPopupBlurState() {
        removePopupBlurNode();
    }

    CCPoint worldToMLParent(CCPoint wp) {
        if (!m_mainLayer) return wp;
        auto* p = m_mainLayer->getParent();
        return p ? p->convertToNodeSpace(wp) : wp;
    }

    CCPoint resolveOrigin(CCPoint const& fallback) {
        CCPoint o = fallback;
        if (paimon::hasButtonOrigin())
            o = worldToMLParent(paimon::consumeButtonOrigin());
        else
            paimon::consumeButtonOrigin(); // clear any stale one
        m_fields->m_origin = o;
        return o;
    }

    void runEntryAnimation() {
        auto* ml = m_mainLayer;
        if (!ml) return;
        ml->stopAllActions();

        float       spd = getSpeed();
        std::string sty = getStyle();
        CCPoint     fp  = ml->getPosition();
        m_fields->m_finalPos = fp;

        if (sty == "paimonUI") {
            CCPoint org = resolveOrigin(fp);

            ml->setScale(0.0f);
            ml->setPosition(org);

            float dur = 0.42f / spd;

            auto phase1 = CCEaseExponentialOut::create(CCScaleTo::create(dur * 0.65f, 1.05f));
            auto phase2 = CCEaseSineInOut::create(CCScaleTo::create(dur * 0.20f, 0.985f));
            auto phase3 = CCEaseSineOut::create(CCScaleTo::create(dur * 0.15f, 1.00f));
            ml->runAction(CCSequence::create(phase1, phase2, phase3, nullptr));

            ml->runAction(CCEaseExponentialOut::create(CCMoveTo::create(dur * 0.70f, fp)));

        } else if (sty == "jelly") {
            resolveOrigin(fp);
            ml->setScaleX(0.0f);
            ml->setScaleY(0.0f);
            ml->setPosition(fp);

            float dur = 0.55f / spd;
            auto jellySeq = CCSequence::create(
                CCEaseExponentialOut::create(CCScaleTo::create(dur * 0.35f, 1.14f, 0.86f)),
                CCEaseSineInOut::create(CCScaleTo::create(dur * 0.22f, 0.92f, 1.07f)),
                CCEaseSineInOut::create(CCScaleTo::create(dur * 0.20f, 1.03f, 0.97f)),
                CCEaseSineOut::create(CCScaleTo::create(dur * 0.23f, 1.00f, 1.00f)),
                nullptr
            );
            ml->runAction(jellySeq);

        } else if (sty == "spiral") {
            resolveOrigin(fp);
            ml->setScale(0.0f);
            ml->setPosition(fp);
            ml->setRotation(-180.f);

            float dur = 0.48f / spd;
            ml->runAction(CCEaseBackOut::create(CCScaleTo::create(dur, 1.00f)));
            ml->runAction(CCEaseExponentialOut::create(CCRotateTo::create(dur, 0.f)));

        } else if (sty == "drop-bounce") {
            resolveOrigin(fp);
            ml->setPosition(fp + CCPoint(0.f, 260.f));
            ml->setScaleX(0.88f);
            ml->setScaleY(1.15f);

            float dur = 0.58f / spd;
            ml->runAction(CCEaseBounceOut::create(CCMoveTo::create(dur, fp)));
            auto scaleSeq = CCSequence::create(
                CCEaseSineOut::create(CCScaleTo::create(dur * 0.50f, 1.04f, 0.96f)),
                CCEaseSineInOut::create(CCScaleTo::create(dur * 0.30f, 0.98f, 1.02f)),
                CCEaseSineOut::create(CCScaleTo::create(dur * 0.20f, 1.00f, 1.00f)),
                nullptr
            );
            ml->runAction(scaleSeq);

        } else if (sty == "skew-pop") {
            resolveOrigin(fp);
            ml->setScale(0.0f);
            ml->setPosition(fp);
            ml->setSkewX(22.f);
            ml->setSkewY(12.f);

            float dur = 0.45f / spd;
            ml->runAction(CCEaseBackOut::create(CCScaleTo::create(dur, 1.00f)));
            ml->runAction(CCEaseExponentialOut::create(CCSkewTo::create(dur, 0.f, 0.f)));

        } else if (sty == "slide-left") {
            resolveOrigin(fp);
            ml->setPosition(fp + CCPoint(-160.f, 0.f));
            ml->setScale(0.92f);

            float dur = 0.42f / spd;
            ml->runAction(CCEaseBackOut::create(CCMoveTo::create(dur, fp)));
            ml->runAction(CCEaseExponentialOut::create(CCScaleTo::create(dur, 1.00f)));

        } else if (sty == "slide-right") {
            resolveOrigin(fp);
            ml->setPosition(fp + CCPoint(160.f, 0.f));
            ml->setScale(0.92f);

            float dur = 0.42f / spd;
            ml->runAction(CCEaseBackOut::create(CCMoveTo::create(dur, fp)));
            ml->runAction(CCEaseExponentialOut::create(CCScaleTo::create(dur, 1.00f)));

        } else if (sty == "slide-up") {
            resolveOrigin(fp);
            ml->setScale(0.92f);
            ml->setPosition(fp + CCPoint(0.f, -80.f));

            float dur = 0.42f / spd;
            ml->runAction(CCEaseBackOut::create(CCMoveTo::create(dur, fp)));
            ml->runAction(CCEaseExponentialOut::create(CCScaleTo::create(dur, 1.00f)));

        } else if (sty == "slide-down") {
            resolveOrigin(fp);
            ml->setScale(0.92f);
            ml->setPosition(fp + CCPoint(0.f, 80.f));

            float dur = 0.42f / spd;
            ml->runAction(CCEaseBackOut::create(CCMoveTo::create(dur, fp)));
            ml->runAction(CCEaseExponentialOut::create(CCScaleTo::create(dur, 1.00f)));

        } else if (sty == "zoom-fade") {
            resolveOrigin(fp);
            ml->setScale(0.55f);
            ml->setPosition(fp);

            float dur = 0.32f / spd;
            ml->runAction(CCEaseExponentialOut::create(CCScaleTo::create(dur, 1.00f)));

        } else if (sty == "elastic") {
            resolveOrigin(fp);
            ml->setScale(0.0f);
            ml->setPosition(fp);

            float dur = 0.52f / spd;
            ml->runAction(CCEaseElasticOut::create(CCScaleTo::create(dur, 1.00f), 0.40f));

        } else if (sty == "bounce") {
            resolveOrigin(fp);
            ml->setScale(0.0f);
            ml->setPosition(fp);

            float dur = 0.48f / spd;
            ml->runAction(CCEaseBounceOut::create(CCScaleTo::create(dur, 1.00f)));

        } else if (sty == "flip") {
            resolveOrigin(fp);
            ml->setScaleX(0.0f);
            ml->setScaleY(1.0f);
            ml->setPosition(fp);

            float dur = 0.45f / spd;
            ml->runAction(CCEaseBackOut::create(CCScaleTo::create(dur, 1.00f, 1.00f)));

        } else if (sty == "fold") {
            resolveOrigin(fp);
            ml->setScaleX(1.0f);
            ml->setScaleY(0.0f);
            ml->setPosition(fp);

            float dur = 0.45f / spd;
            ml->runAction(CCEaseBackOut::create(CCScaleTo::create(dur, 1.00f, 1.00f)));

        } else if (sty == "pop-rotate") {
            resolveOrigin(fp);
            ml->setScale(0.0f);
            ml->setRotation(-12.f);
            ml->setPosition(fp);

            float dur = 0.46f / spd;
            ml->runAction(CCEaseBackOut::create(CCScaleTo::create(dur, 1.00f)));
            ml->runAction(CCEaseBackOut::create(CCRotateTo::create(dur, 0.f)));

        } else if (sty == "elastic-drop") {
            resolveOrigin(fp);
            ml->setPosition(fp + CCPoint(0.f, 320.f));
            ml->setScaleX(0.75f);
            ml->setScaleY(1.35f);

            float dur = 0.65f / spd;
            ml->runAction(CCEaseElasticOut::create(CCMoveTo::create(dur, fp), 0.45f));
            ml->runAction(CCEaseElasticOut::create(CCScaleTo::create(dur, 1.00f, 1.00f), 0.45f));

        } else if (sty == "glitch-shake") {
            resolveOrigin(fp);
            ml->setScale(0.1f);
            ml->setPosition(fp);

            float dur = 0.45f / spd;
            ml->runAction(CCSequence::create(
                CCSpawn::create(CCScaleTo::create(dur * 0.15f, 1.15f, 0.85f), CCSkewTo::create(dur * 0.15f, 15.f, 5.f), nullptr),
                CCSpawn::create(CCScaleTo::create(dur * 0.15f, 0.85f, 1.20f), CCSkewTo::create(dur * 0.15f, -15.f, -5.f), nullptr),
                CCSpawn::create(CCScaleTo::create(dur * 0.15f, 1.05f, 0.95f), CCSkewTo::create(dur * 0.15f, 5.f, 2.f), nullptr),
                CCSpawn::create(CCScaleTo::create(dur * 0.15f, 0.98f, 1.02f), CCSkewTo::create(dur * 0.15f, -2.f, -1.f), nullptr),
                CCSpawn::create(CCScaleTo::create(dur * 0.40f, 1.00f, 1.00f), CCSkewTo::create(dur * 0.40f, 0.f, 0.f), nullptr),
                nullptr
            ));

        } else if (sty == "card-turn") {
            resolveOrigin(fp);
            ml->setScaleX(0.0f);
            ml->setScaleY(1.0f);
            ml->setPosition(fp);
            ml->setSkewY(15.f);

            float dur = 0.48f / spd;
            ml->runAction(CCEaseExponentialOut::create(CCScaleTo::create(dur, 1.00f, 1.00f)));
            ml->runAction(CCEaseExponentialOut::create(CCSkewTo::create(dur, 0.f, 0.f)));

        } else if (sty == "fly-spin") {
            resolveOrigin(fp);
            ml->setScale(0.0f);
            ml->setRotation(-720.f);
            ml->setPosition(fp);

            float dur = 0.55f / spd;
            ml->runAction(CCEaseExponentialOut::create(CCScaleTo::create(dur, 1.00f)));
            ml->runAction(CCEaseExponentialOut::create(CCRotateTo::create(dur, 0.f)));

        } else {
            resolveOrigin(fp);
            ml->setScale(0.60f);
            ml->setPosition(fp);
            float dur = 0.30f / spd;
            ml->runAction(CCEaseExponentialOut::create(CCScaleTo::create(dur, 1.00f)));
        }
    }

    void runExitAnimation() {
        auto* ml = m_mainLayer;
        if (!ml) { FLAlertLayer::keyBackClicked(); return; }

        ml->stopAllActions();
        this->stopAllActions();
        m_fields->m_exitGuard = this;

        float       spd = getExitSpeed();
        std::string sty = getStyle();
        CCPoint     org = m_fields->m_origin;
        CCPoint     pos = ml->getPosition();
        if (org.x < 0.f) org = pos;

        float dur = 0.f;

        if (sty == "paimonUI") {
            dur = 0.28f / spd;
            // Stretch slightly, then shrink to the button
            auto scaleUp = CCEaseSineOut::create(CCScaleTo::create(dur * 0.22f, 1.03f));
            auto shrink = CCSpawn::create(
                CCEaseExponentialIn::create(CCScaleTo::create(dur * 0.78f, 0.00f)),
                CCEaseExponentialIn::create(CCMoveTo::create(dur * 0.78f, org)),
                nullptr
            );
            ml->runAction(CCSequence::create(scaleUp, shrink, nullptr));

        } else if (sty == "jelly") {
            dur = 0.25f / spd;
            auto exitSeq = CCSequence::create(
                CCEaseSineInOut::create(CCScaleTo::create(dur * 0.30f, 1.12f, 0.72f)),
                CCEaseExponentialIn::create(CCScaleTo::create(dur * 0.70f, 0.00f, 0.00f)),
                nullptr
            );
            ml->runAction(exitSeq);

        } else if (sty == "spiral") {
            dur = 0.32f / spd;
            ml->runAction(CCEaseBackIn::create(CCScaleTo::create(dur, 0.0f)));
            ml->runAction(CCEaseExponentialIn::create(CCRotateTo::create(dur, 180.f)));

        } else if (sty == "drop-bounce") {
            dur = 0.30f / spd;
            ml->runAction(CCSpawn::create(
                CCEaseExponentialIn::create(CCMoveTo::create(dur, pos + CCPoint(0.f, -280.f))),
                CCEaseExponentialIn::create(CCScaleTo::create(dur, 0.85f, 1.15f)),
                nullptr
            ));

        } else if (sty == "skew-pop") {
            dur = 0.25f / spd;
            ml->runAction(CCEaseBackIn::create(CCScaleTo::create(dur, 0.0f)));
            ml->runAction(CCEaseExponentialIn::create(CCSkewTo::create(dur, -22.f, -12.f)));

        } else if (sty == "slide-left") {
            dur = 0.24f / spd;
            ml->runAction(CCSpawn::create(
                CCEaseExponentialIn::create(CCMoveTo::create(dur, pos + CCPoint(160.f, 0.f))),
                CCEaseExponentialIn::create(CCScaleTo::create(dur, 0.92f)),
                nullptr
            ));

        } else if (sty == "slide-right") {
            dur = 0.24f / spd;
            ml->runAction(CCSpawn::create(
                CCEaseExponentialIn::create(CCMoveTo::create(dur, pos + CCPoint(-160.f, 0.f))),
                CCEaseExponentialIn::create(CCScaleTo::create(dur, 0.92f)),
                nullptr
            ));

        } else if (sty == "slide-up") {
            dur = 0.22f / spd;
            ml->runAction(CCSpawn::create(
                CCEaseExponentialIn::create(CCMoveTo::create(dur, pos + CCPoint(0.f, -80.f))),
                CCEaseExponentialIn::create(CCScaleTo::create(dur, 0.92f)),
                nullptr
            ));

        } else if (sty == "slide-down") {
            dur = 0.22f / spd;
            ml->runAction(CCSpawn::create(
                CCEaseExponentialIn::create(CCMoveTo::create(dur, pos + CCPoint(0.f, 80.f))),
                CCEaseExponentialIn::create(CCScaleTo::create(dur, 0.92f)),
                nullptr
            ));

        } else if (sty == "zoom-fade") {
            dur = 0.20f / spd;
            ml->runAction(CCEaseExponentialIn::create(CCScaleTo::create(dur, 0.55f)));

        } else if (sty == "elastic") {
            dur = 0.22f / spd;
            ml->runAction(CCEaseSineIn::create(CCScaleTo::create(dur, 0.0f)));

        } else if (sty == "bounce") {
            dur = 0.24f / spd;
            ml->runAction(CCEaseSineInOut::create(CCScaleTo::create(dur, 0.0f)));

        } else if (sty == "flip") {
            dur = 0.22f / spd;
            ml->runAction(CCEaseBackIn::create(CCScaleTo::create(dur, 0.0f, 1.0f)));

        } else if (sty == "fold") {
            dur = 0.22f / spd;
            ml->runAction(CCEaseBackIn::create(CCScaleTo::create(dur, 1.0f, 0.0f)));

        } else if (sty == "pop-rotate") {
            dur = 0.25f / spd;
            ml->runAction(CCEaseBackIn::create(CCScaleTo::create(dur, 0.0f)));
            ml->runAction(CCEaseBackIn::create(CCRotateTo::create(dur, 12.f)));

        } else if (sty == "elastic-drop") {
            dur = 0.35f / spd;
            ml->runAction(CCSpawn::create(
                CCEaseElasticIn::create(CCMoveTo::create(dur, pos + CCPoint(0.f, 320.f)), 0.45f),
                CCEaseElasticIn::create(CCScaleTo::create(dur, 0.75f, 1.35f), 0.45f),
                nullptr
            ));

        } else if (sty == "glitch-shake") {
            dur = 0.28f / spd;
            ml->runAction(CCSequence::create(
                CCSpawn::create(CCScaleTo::create(dur * 0.25f, 1.25f, 0.70f), CCSkewTo::create(dur * 0.25f, 20.f, 8.f), nullptr),
                CCSpawn::create(CCScaleTo::create(dur * 0.25f, 0.70f, 1.30f), CCSkewTo::create(dur * 0.25f, -20.f, -8.f), nullptr),
                CCSpawn::create(CCScaleTo::create(dur * 0.50f, 2.00f, 0.00f), CCSkewTo::create(dur * 0.50f, 0.f, 0.f), nullptr),
                nullptr
            ));

        } else if (sty == "card-turn") {
            dur = 0.28f / spd;
            ml->runAction(CCSpawn::create(
                CCEaseExponentialIn::create(CCScaleTo::create(dur, 0.00f, 1.00f)),
                CCEaseExponentialIn::create(CCSkewTo::create(dur, 15.f, 0.f)),
                nullptr
            ));

        } else if (sty == "fly-spin") {
            dur = 0.32f / spd;
            ml->runAction(CCEaseExponentialIn::create(CCScaleTo::create(dur, 0.0f)));
            ml->runAction(CCEaseExponentialIn::create(CCRotateTo::create(dur, 540.f)));

        } else {
            dur = 0.20f / spd;
            ml->runAction(CCEaseExponentialIn::create(CCScaleTo::create(dur, 0.60f)));
        }

        this->runAction(CCSequence::create(
            CCDelayTime::create(dur + 0.01f),
            CCCallFunc::create(this, callfunc_selector(PaimonDynamicPopupHook::finishExit)),
            nullptr
        ));
    }

    void finishExit() {
        paimon::unmarkDynamicPopup(this);
        this->scheduleOnce(schedule_selector(PaimonDynamicPopupHook::deferredClose), 0.f);
    }

    void deferredClose(float) {
        m_fields->m_exitGuard = nullptr;
        // Defer removeFromParentAndCleanup one frame to avoid returning into
        // freed memory if the object is destroyed during the call.
        auto self = Ref<FLAlertLayer>(this);
        Loader::get()->queueInMainThread([self]() {
            if (paimon::isRuntimeShuttingDown()) return;
            self->removeFromParentAndCleanup(true);
        });
    }

    $override
    void show() {
        FLAlertLayer::show();

        if (shouldApplyPopupBlur()) {
            paimon::popupblur::captureAndApply(this);
        }
        if (shouldAnimatePopup()) {
            runEntryAnimation();
        }
    }

    void draw() {
        // The blur node is a sibling drawn before the popup (z = popupZ-1), so
        // don't skip the popup's draw() — that would hide it if the blur failed.
        FLAlertLayer::draw();
    }

    $override
    void removeFromParent() {
        paimon::popupblur::cleanup(this);
        FLAlertLayer::removeFromParent();
    }

    $override
    void keyBackClicked() {
        if (!shouldAnimateExit()) {
            float fadeDur = std::clamp(
                static_cast<float>(paimon::settings::popupblur::fadeDuration()),
                0.0f, 1.0f);
            paimon::popupblur::cleanupWithFade(this, fadeDur);
            FLAlertLayer::keyBackClicked();
            return;
        }
        this->removeFromParentAndCleanup(true);
    }

    $override
    void removeFromParentAndCleanup(bool cleanup) {
        if (!shouldAnimateExit() || m_fields->m_exiting) {
            // No popup animation: use the configured fade duration so the blur
            // fades out smoothly.
            float fadeDur = std::clamp(
                static_cast<float>(paimon::settings::popupblur::fadeDuration()),
                0.0f, 1.0f);
            fadeOutAndRemoveBlur(fadeDur);
            FLAlertLayer::removeFromParentAndCleanup(cleanup);
            return;
        }
        m_fields->m_exiting = true;
        this->setKeypadEnabled(false);
        this->setTouchEnabled(false);

        // Fade the blur over max(exit animation duration, fade setting) so it
        // doesn't vanish before the popup.
        float spd = getExitSpeed();
        float sty_dur_max = 0.25f / spd; // approx max of the exit styles
        float settingFade = std::clamp(
            static_cast<float>(paimon::settings::popupblur::fadeDuration()),
            0.0f, 1.0f);
        fadeOutAndRemoveBlur(std::max(sty_dur_max, settingFade));

        runExitAnimation();
    }

    // onExit fires when the popup leaves the scene graph, before refs hit zero.
    // Guarantees blur cleanup on paths that skip keyBackClicked (scene change,
    // another mod removing the popup directly, a custom close button).
    $override
    void onExit() {
        this->unschedule(schedule_selector(PaimonDynamicPopupHook::deferredClose));
        // Only clean up if keyBackClicked didn't already; its fade nulls
        // m_blurNode, so this is a no-op in the normal flow.
        if (m_fields->m_blurNode) {
            removePopupBlurNode();
        }
        // Service cleanup() removes anything left in the shared registry.
        paimon::popupblur::cleanup(this);
        FLAlertLayer::onExit();
    }

    ~PaimonDynamicPopupHook() {
        clearPopupBlurState();
    }
};


// Popup blur for classes that don't go through FLAlertLayer::show. ProfilePage
// and SetupTriggerPopup override show() themselves, so the virtual hook above
// doesn't fire for them — hook each base's show() directly. Exclusions:
// SetupShaderEffectPopup (user needs the live shader over gameplay) and anything
// flagged with paimon::markPopupBlurOptOut().

#include <Geode/binding/SetupShaderEffectPopup.hpp>

namespace {
bool isShaderRelatedPopup(cocos2d::CCNode* popup) {
    if (!popup) return false;
    // SetupShaderEffectPopup needs the live gameplay background visible so the
    // user sees the shader effect applied.
    if (typeinfo_cast<SetupShaderEffectPopup*>(popup)) return true;
    return false;
}
} // namespace

class $modify(PaimonProfilePageBlur, ProfilePage) {
    $override
    void show() {
        ProfilePage::show();
        if (isEditorContextActive()) return;
        paimon::popupblur::captureAndApply(this);
    }

    $override
    void keyBackClicked() {
        float fadeDur = std::clamp(
            static_cast<float>(paimon::settings::popupblur::fadeDuration()),
            0.0f, 0.6f);
        paimon::popupblur::cleanupWithFade(this, fadeDur);
        ProfilePage::keyBackClicked();
    }

    $override
    void onExit() {
        paimon::popupblur::cleanup(this);
        ProfilePage::onExit();
    }
};

class $modify(PaimonSetupTriggerPopupBlur, SetupTriggerPopup) {
    $override
    void show() {
        SetupTriggerPopup::show();
        if (isEditorContextActive()) return;
        if (isShaderRelatedPopup(this)) return;
        paimon::popupblur::captureAndApply(this);
    }

    $override
    void keyBackClicked() {
        float fadeDur = std::clamp(
            static_cast<float>(paimon::settings::popupblur::fadeDuration()),
            0.0f, 0.6f);
        paimon::popupblur::cleanupWithFade(this, fadeDur);
        SetupTriggerPopup::keyBackClicked();
    }

    $override
    void onExit() {
        paimon::popupblur::cleanup(this);
        SetupTriggerPopup::onExit();
    }
};
