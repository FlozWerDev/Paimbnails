// Animaciones de entrada y salida para popups

#include <Geode/modify/FLAlertLayer.hpp>
#include <Geode/modify/CCMenuItemSpriteExtra.hpp>
#include <Geode/loader/Mod.hpp>
#include "../utils/DynamicPopupRegistry.hpp"
#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

// Captura posicion del boton.
// Nota: no puede migrarse a MenuItemActivatedEvent (Geode 5.6.0) porque necesita
// ejecutarse ANTES de activate() (setHookPriorityPre) para capturar la posicion
// del boton antes de que el popup se abra. MenuItemActivatedEvent es post-activate.
class $modify(PaimonButtonOriginCapture, CCMenuItemSpriteExtra) {
    static void onModify(auto& self) {
        // Guarda posicion del boton antes del click
        (void)self.setHookPriorityPre("CCMenuItemSpriteExtra::activate", geode::Priority::First);
    }

    $override
    void activate() {
        // Solo captura si la feature esta activa
        if (Mod::get()->getSettingValue<bool>("dynamic-popup-enabled")) {
            auto sz = this->getContentSize();
            paimon::storeButtonOrigin(
                this->convertToWorldSpace({sz.width / 2.f, sz.height / 2.f})
            );
        }
        CCMenuItemSpriteExtra::activate();
    }
};

// Animaciones de entrada y salida

class $modify(PaimonDynamicPopupHook, FLAlertLayer) {

    struct Fields {
        bool    m_exiting = false;
        CCPoint m_origin  = {-1.f, -1.f};
        CCPoint m_finalPos= {0.f, 0.f};
        Ref<FLAlertLayer> m_exitGuard = nullptr;
    };

    static void onModify(auto& self) {
        (void)self.setHookPriorityPost("FLAlertLayer::show",           geode::Priority::Late);
        (void)self.setHookPriorityPost("FLAlertLayer::keyBackClicked", geode::Priority::Late);
    }

    // Helpers

    bool isPaimonPopup() {
        return paimon::isDynamicPopup(this)
            && Mod::get()->getSettingValue<bool>("dynamic-popup-enabled");
    }

    float getSpeed() {
        float speed = static_cast<float>(
            Mod::get()->getSettingValue<double>("dynamic-popup-speed")
        );
        if (!(speed > 0.f)) {
            speed = 1.0f;
        }
        return std::max(0.1f, speed);
    }

    std::string getStyle() {
        return Mod::get()->getSettingValue<std::string>("dynamic-popup-style");
    }

    CCPoint worldToMLParent(CCPoint wp) {
        if (!m_mainLayer) return wp;
        auto* p = m_mainLayer->getParent();
        return p ? p->convertToNodeSpace(wp) : wp;
    }

    // Obtiene o limpia el origen del boton
    CCPoint resolveOrigin(CCPoint const& fallback) {
        CCPoint o = fallback;
        if (paimon::hasButtonOrigin())
            o = worldToMLParent(paimon::consumeButtonOrigin());
        else
            paimon::consumeButtonOrigin(); // Limpia si habia uno viejo
        m_fields->m_origin = o;
        return o;
    }

    // Entrada

    void runEntryAnimation() {
        auto* ml = m_mainLayer;
        if (!ml) return;
        ml->stopAllActions();

        float       spd = getSpeed();
        std::string sty = getStyle();
        CCPoint     fp  = ml->getPosition();
        m_fields->m_finalPos = fp;

        // -- paimonUI --
        if (sty == "paimonUI") {
            CCPoint org = resolveOrigin(fp);

            ml->setScale(0.0f);
            ml->setPosition(org);

            float dur = 0.42f / spd;

            // Fases de expansion y ajuste
            auto phase1 = CCEaseExponentialOut::create(CCScaleTo::create(dur * 0.70f, 1.06f));
            auto phase2 = CCEaseSineInOut::create(CCScaleTo::create(dur * 0.18f, 0.985f));
            auto phase3 = CCEaseSineOut::create(CCScaleTo::create(dur * 0.12f, 1.00f));
            ml->runAction(CCSequence::create(phase1, phase2, phase3, nullptr));

            // Movimiento a posicion final
            ml->runAction(
                CCEaseExponentialOut::create(CCMoveTo::create(dur * 0.70f, fp))
            );

        } else if (sty == "slide-up") {
            resolveOrigin(fp);
            ml->setScale(0.96f);
            ml->setPosition(fp + CCPoint(0.f, -55.f));

            float dur = 0.38f / spd;
            ml->runAction(CCSpawn::create(
                CCEaseExponentialOut::create(CCMoveTo::create(dur, fp)),
                CCEaseExponentialOut::create(CCScaleTo::create(dur, 1.00f)),
                nullptr
            ));

        } else if (sty == "slide-down") {
            resolveOrigin(fp);
            ml->setScale(0.96f);
            ml->setPosition(fp + CCPoint(0.f, 55.f));

            float dur = 0.38f / spd;
            ml->runAction(CCSpawn::create(
                CCEaseExponentialOut::create(CCMoveTo::create(dur, fp)),
                CCEaseExponentialOut::create(CCScaleTo::create(dur, 1.00f)),
                nullptr
            ));

        } else if (sty == "zoom-fade") {
            resolveOrigin(fp);
            ml->setScale(0.70f);

            float dur = 0.28f / spd;
            ml->runAction(
                CCEaseExponentialOut::create(CCScaleTo::create(dur, 1.00f))
            );

        } else if (sty == "elastic") {
            resolveOrigin(fp);
            ml->setScale(0.0f);

            float dur = 0.55f / spd;
            ml->runAction(
                CCEaseElasticOut::create(CCScaleTo::create(dur, 1.00f), 0.35f)
            );

        } else if (sty == "bounce") {
            resolveOrigin(fp);
            ml->setScale(0.0f);
            ml->setPosition(fp + CCPoint(0.f, 30.f));

            float dur = 0.50f / spd;
            ml->runAction(CCSpawn::create(
                CCEaseBounceOut::create(CCScaleTo::create(dur, 1.00f)),
                CCEaseBounceOut::create(CCMoveTo::create(dur, fp)),
                nullptr
            ));

        } else if (sty == "flip") {
            resolveOrigin(fp);
            ml->setScaleX(0.0f);
            ml->setScaleY(1.0f);

            float dur = 0.35f / spd;
            // Overshoot en scaleX
            auto flipX1 = CCEaseExponentialOut::create(CCScaleTo::create(dur * 0.75f, 1.04f, 1.0f));
            auto flipX2 = CCEaseSineOut::create(CCScaleTo::create(dur * 0.25f, 1.0f, 1.0f));
            ml->runAction(CCSequence::create(flipX1, flipX2, nullptr));

        } else if (sty == "fold") {
            resolveOrigin(fp);
            ml->setScaleX(1.0f);
            ml->setScaleY(0.0f);

            float dur = 0.35f / spd;
            auto foldY1 = CCEaseExponentialOut::create(CCScaleTo::create(dur * 0.75f, 1.0f, 1.04f));
            auto foldY2 = CCEaseSineOut::create(CCScaleTo::create(dur * 0.25f, 1.0f, 1.0f));
            ml->runAction(CCSequence::create(foldY1, foldY2, nullptr));

        } else if (sty == "pop-rotate") {
            resolveOrigin(fp);
            ml->setScale(0.0f);
            ml->setRotation(-8.f);

            float dur = 0.40f / spd;
            // Escala de entrada
            auto s1 = CCEaseExponentialOut::create(CCScaleTo::create(dur * 0.70f, 1.05f));
            auto s2 = CCEaseSineOut::create(CCScaleTo::create(dur * 0.30f, 1.00f));
            ml->runAction(CCSequence::create(s1, s2, nullptr));
            // Rotacion de entrada
            auto r1 = CCEaseExponentialOut::create(CCRotateTo::create(dur * 0.65f, 2.f));
            auto r2 = CCEaseSineOut::create(CCRotateTo::create(dur * 0.35f, 0.f));
            ml->runAction(CCSequence::create(r1, r2, nullptr));

        } else {
            resolveOrigin(fp);
            ml->setScale(0.70f);
            float dur = 0.28f / spd;
            ml->runAction(CCEaseExponentialOut::create(CCScaleTo::create(dur, 1.00f)));
        }
    }

    // Salida

    void runExitAnimation() {
        auto* ml = m_mainLayer;
        if (!ml) { FLAlertLayer::keyBackClicked(); return; }

        ml->stopAllActions();
        this->stopAllActions();
        m_fields->m_exitGuard = this;

        float       spd = getSpeed();
        std::string sty = getStyle();
        CCPoint     org = m_fields->m_origin;
        CCPoint     pos = ml->getPosition();
        if (org.x < 0.f) org = pos;

        float dur = 0.f;

        // -- paimonUI --
        if (sty == "paimonUI") {
            dur = 0.25f / spd;
            // Encoge hacia el boton
            ml->runAction(CCSpawn::create(
                CCEaseIn::create(CCScaleTo::create(dur, 0.0f), 2.8f),
                CCEaseIn::create(CCMoveTo::create(dur, org), 2.8f),
                nullptr
            ));

        } else if (sty == "slide-up") {
            dur = 0.20f / spd;
            ml->runAction(CCSpawn::create(
                CCEaseIn::create(CCScaleTo::create(dur, 0.96f), 2.f),
                CCEaseIn::create(CCMoveTo::create(dur, pos + CCPoint(0.f, -40.f)), 2.f),
                nullptr
            ));

        } else if (sty == "slide-down") {
            dur = 0.20f / spd;
            ml->runAction(CCSpawn::create(
                CCEaseIn::create(CCScaleTo::create(dur, 0.96f), 2.f),
                CCEaseIn::create(CCMoveTo::create(dur, pos + CCPoint(0.f, 40.f)), 2.f),
                nullptr
            ));

        } else if (sty == "zoom-fade") {
            dur = 0.16f / spd;
            ml->runAction(
                CCEaseIn::create(CCScaleTo::create(dur, 0.70f), 2.f)
            );

        } else if (sty == "elastic") {
            dur = 0.22f / spd;
            ml->runAction(
                CCEaseIn::create(CCScaleTo::create(dur, 0.0f), 2.5f)
            );

        } else if (sty == "bounce") {
            dur = 0.22f / spd;
            ml->runAction(CCSpawn::create(
                CCEaseIn::create(CCScaleTo::create(dur, 0.0f), 2.f),
                CCEaseIn::create(CCMoveTo::create(dur, pos + CCPoint(0.f, 20.f)), 2.f),
                nullptr
            ));

        } else if (sty == "flip") {
            dur = 0.22f / spd;
            ml->runAction(
                CCEaseIn::create(CCScaleTo::create(dur, 0.0f, 1.0f), 2.5f)
            );

        } else if (sty == "fold") {
            dur = 0.22f / spd;
            ml->runAction(
                CCEaseIn::create(CCScaleTo::create(dur, 1.0f, 0.0f), 2.5f)
            );

        } else if (sty == "pop-rotate") {
            dur = 0.22f / spd;
            ml->runAction(CCSpawn::create(
                CCEaseIn::create(CCScaleTo::create(dur, 0.0f), 2.5f),
                CCEaseIn::create(CCRotateTo::create(dur, 8.f), 2.f),
                nullptr
            ));

        } else {
            dur = 0.16f / spd;
            ml->runAction(CCEaseIn::create(CCScaleTo::create(dur, 0.70f), 2.f));
        }

        // Cierra despues de la animacion
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
        FLAlertLayer::keyBackClicked();
    }

    // --- hooks ---

    $override
    void show() {
        FLAlertLayer::show();
        if (!isPaimonPopup()) return;
        runEntryAnimation();
    }

    $override
    void keyBackClicked() {
        if (!isPaimonPopup() || m_fields->m_exiting) {
            FLAlertLayer::keyBackClicked();
            return;
        }
        m_fields->m_exiting = true;
        this->setKeypadEnabled(false);
        runExitAnimation();
    }
};
