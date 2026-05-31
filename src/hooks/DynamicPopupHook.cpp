// Animaciones de entrada y salida para popups
// Blur configurable detras de los popups del mod.

#include <Geode/modify/FLAlertLayer.hpp>
#include <Geode/modify/ProfilePage.hpp>
#include <Geode/modify/SetupTriggerPopup.hpp>
#include <Geode/modify/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/loader/Mod.hpp>
#include "../utils/DynamicPopupRegistry.hpp"
#include "../blur/BlurSystem.hpp"
#include "../blur/PopupBlurService.hpp"
#include "../core/Settings.hpp"
#include <algorithm>
#include <cmath>

using namespace geode::prelude;
using namespace cocos2d;

namespace {
using PopupBlurConfig = paimon::popupblur::Config;

PopupBlurConfig getPopupBlurConfig() {
    return paimon::popupblur::getConfig();
}

bool isEditorContextActive() {
    auto* director = CCDirector::get();
    if (!director) return false;

    auto* scene = director->getRunningScene();
    if (!scene) return false;

    return scene->getChildByType<LevelEditorLayer>(0) != nullptr ||
           scene->getChildByType<EditorUI>(0) != nullptr;
}
} // namespace

// Captura posicion del boton.
// Nota: no puede migrarse a MenuItemActivatedEvent (Geode 5.6.0) porque necesita
// ejecutarse ANTES de activate() (setHookPriorityPre) para capturar la posicion
// del boton antes de que el popup se abra. MenuItemActivatedEvent es post-activate.
class $modify(PaimonButtonOriginCapture, CCMenuItemSpriteExtra) {
    static void onModify(auto& self) {
        // Guarda posicion del boton antes del click. Usamos VeryEarly en
        // lugar de First para liberar el slot inicial de la cadena —
        // otros mods que necesiten correr LITERALMENTE primero (ej.
        // capturar estado pre-callback) conservan su prioridad.
        (void)self.setHookPriorityPre("CCMenuItemSpriteExtra::activate", geode::Priority::VeryEarly);
    }

    $override
    void activate() {
        // Aislamiento total del editor: aqui el hook es un passthrough puro.
        // No captura ni consume origenes ni toca estado del mod, de modo que el
        // mod no participa en la cadena activate() de los botones del editor
        // (incluida la del ColorSelectPopup que dispara el crash conocido).
        if (isEditorContextActive()) {
            CCMenuItemSpriteExtra::activate();
            return;
        }
        // Cachear el valor del setting para evitar pagar el costo del
        // mutex de getSettingValue() en CADA click de TODOS los botones
        // del juego (incluyendo botones que nunca abren popups). El
        // listener actualiza el cache cuando el usuario cambia el setting.
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

// Animaciones de entrada y salida

class $modify(PaimonDynamicPopupHook, FLAlertLayer) {

    struct Fields {
        bool    m_exiting = false;
        CCPoint m_origin  = {-1.f, -1.f};
        CCPoint m_finalPos= {0.f, 0.f};
        Ref<FLAlertLayer> m_exitGuard = nullptr;
        Ref<CCNode> m_blurNode = nullptr;
        Ref<CCTexture2D> m_snapshotTexture = nullptr;
        CCSize m_snapshotSize = CCSizeZero;
        GLubyte m_blurTargetOpacity = 255;
        int m_blurRequestToken = 0;
    };

    static void onModify(auto& self) {
        (void)self.setHookPriorityPost("FLAlertLayer::show",           geode::Priority::Late);
        (void)self.setHookPriorityPost("FLAlertLayer::keyBackClicked", geode::Priority::Late);
        (void)self.setHookPriorityPost("FLAlertLayer::removeFromParentAndCleanup", geode::Priority::Late);
    }

    // Helpers

    bool isPaimonPopup() {
        return paimon::isDynamicPopup(this);
    }

    bool isAnyGamePopup() {
        return this->m_mainLayer != nullptr;
    }

    bool shouldAnimatePopup() {
        return isPaimonPopup() && !isEditorContextActive() && Mod::get()->getSettingValue<bool>("dynamic-popup-enabled");
    }

    bool shouldAnimateExit() {
        return shouldAnimatePopup() && Mod::get()->getSettingValue<bool>("dynamic-exit-enabled");
    }

    // Solo aplicamos blur a popups del propio mod. Aplicarlo a CUALQUIER
    // FLAlertLayer rompe la apariencia esperada de popups de otros mods
    // (Globed, EclipseMenu, GDShare, etc.) y no es lo que el setting promete
    // ("Blur the background behind Paimbnails popups.").
    bool shouldApplyPopupBlur() {
        return isPaimonPopup() && getPopupBlurConfig().enabled && !isEditorContextActive();
    }

    float getSpeed() {
        float speed = static_cast<float>(
            Mod::get()->getSavedValue<double>("dynamic-popup-speed", 1.0)
        );
        if (!(speed > 0.f)) {
            speed = 1.0f;
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
        return std::max(0.1f, speed);
    }

    std::string getStyle() {
        return Mod::get()->getSavedValue<std::string>("dynamic-popup-style", "paimonUI");
    }

    void invalidateBlurRequest() {
        ++m_fields->m_blurRequestToken;
        m_fields->m_snapshotTexture = nullptr;
        m_fields->m_snapshotSize = CCSizeZero;
    }

    void removePopupBlurNode() {
        // ── FIX (crash con globed RoomPopup) ──
        // No remover el blur SINCRONICAMENTE. Cuando esta funcion se llama
        // desde onExit() (que se invoca por detachChild del parent durante
        // removeFromParent), mutar la lista de hijos del MISMO parent
        // (el blur es sibling del popup) corrompe el estado del CCArray
        // que cocos esta a punto de tocar al volver de onExit.
        //
        // Repro: globed cierra RoomPopup desde un event handler. La cadena
        //   Popup::onClose → removeFromParent → detachChild → onExit (hook)
        //   → blur->removeFromParent  ← muta children[]
        //   ← vuelve a detachChild → m_pChildren->removeObject(popup)
        //   → memmove() lee memoria invalida → access violation
        //
        // Diferimos el removeFromParent al siguiente tick del main thread.
        // Para entonces el detachChild del popup ya termino y el children
        // array del parent es estable.
        if (Ref<CCNode> blur = m_fields->m_blurNode) {
            geode::Loader::get()->queueInMainThread([blur]() {
                if (auto* node = blur.data(); node && node->getParent()) {
                    node->removeFromParent();
                }
            });
        }
        m_fields->m_blurNode = nullptr;
        m_fields->m_blurTargetOpacity = 255;
        // Desregistrar del servicio compartido
        paimon::popupblur::cleanup(this);
    }

    // Hace fade-out del blur node y lo remueve al terminar. Usado durante
    // animacion de salida del popup — evita el "pop" visual cuando el popup
    // desaparece y el blur se corta abruptamente.
    //
    // Importante: saca del registry INMEDIATAMENTE (antes del fade) para que
    // otros popups que abran durante el fade no vean este blur como activo
    // en hideAllActiveBlurs. Tambien evita leaks: si el popup se destruye
    // durante el fade, el blur se auto-remueve al terminar la CCSequence.
    void fadeOutAndRemoveBlur(float duration) {
        // Saca del registry compartido ya
        paimon::popupblur::cleanup(this);

        if (!m_fields->m_blurNode) {
            m_fields->m_blurTargetOpacity = 255;
            return;
        }
        auto* node = m_fields->m_blurNode.data();
        // Suelta la referencia del field — el node se mantiene vivo por ser
        // hijo del parent hasta que runAction -> removeFromParent lo quite.
        m_fields->m_blurNode = nullptr;
        m_fields->m_blurTargetOpacity = 255;

        if (!node || !node->getParent()) return;

        if (duration <= 0.01f) {
            // Defer al siguiente tick — vease comentario en removePopupBlurNode
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
        invalidateBlurRequest();
        removePopupBlurNode();
    }

    bool capturePopupBlurSnapshot() {
        invalidateBlurRequest();

        // Delega la captura al servicio centralizado que ya maneja:
        // - Ocultacion de blur nodes activos (anti-feedback loop)
        // - beginWithClear atomico (fix cuadrados blancos)
        // - glFinish() para GPUs con deferred rendering
        // - Validacion de FBO completeness
        // - Captura a resolucion reducida (optimizacion)
        CCSize captureSize = CCSizeZero;
        auto* tex = paimon::popupblur::captureSceneTexture(this, captureSize);
        if (!tex || captureSize.width <= 0.f || captureSize.height <= 0.f) return false;

        m_fields->m_snapshotTexture = tex;
        m_fields->m_snapshotSize = captureSize;
        return true;
    }

    void installFullscreenPopupBlur(CCSprite* blurredSprite, PopupBlurConfig const& cfg) {
        if (!blurredSprite) return;

        auto captureSize = m_fields->m_snapshotSize;
        if (captureSize.width <= 0.f || captureSize.height <= 0.f) return;

        // winSize real de la pantalla (el blur node debe cubrir toda la ventana)
        auto winSize = CCDirector::get()->getWinSize();

        // ── FIX (popup blur thumbnail en esquina + todo en negro) ──
        // Normalizar el sprite blureado a un FBO de winSize antes de pasarlo a
        // buildBlurNode. Garantiza contentSize correcto y orientacion correcta
        // sin importar el estado del sprite recibido.
        if (auto* normalized = paimon::popupblur::normalizeBlurSpriteToWinSize(blurredSprite, winSize)) {
            blurredSprite = normalized;
        }

        // Delega la construccion del blur node al servicio compartido.
        // buildBlurNode se encarga de escalar el sprite (que puede ser de
        // menor resolucion) para cubrir winSize completo.
        auto root = paimon::popupblur::buildBlurNode(blurredSprite, winSize, cfg);
        if (!root) return;

        removePopupBlurNode();
        if (auto* parent = this->getParent()) {
            parent->addChild(root, this->getZOrder() - 1);
        } else {
            this->addChild(root, -999);
        }
        m_fields->m_blurNode = root;

        // Registra en el servicio compartido para que otros popups puedan
        // ocultarnos automaticamente cuando hagan su captura (evita
        // acumulacion de blur-sobre-blur).
        paimon::popupblur::registerExternalBlur(this, root);

        // Fade-in en sync con la animacion de entrada del popup. Duracion
        // corta: que no se sienta lenta pero que no haga "pop" instantaneo.
        float fadeDuration = std::clamp(
            static_cast<float>(paimon::settings::popupblur::fadeDuration()),
            0.0f, 1.0f);
        if (fadeDuration > 0.01f) {
            root->setOpacity(0);
            root->runAction(CCFadeTo::create(fadeDuration, 255));
        }
    }

    void applyPopupBlur() {
        if (!m_fields->m_snapshotTexture || m_fields->m_snapshotSize.width <= 0.f || m_fields->m_snapshotSize.height <= 0.f) {
            return;
        }

        auto cfg = getPopupBlurConfig();
        m_fields->m_blurTargetOpacity = 255;

        ++m_fields->m_blurRequestToken;

        float effectiveIntensity = cfg.intensity;
        if (cfg.style == "paimonblur") {
            effectiveIntensity = std::min(10.0f, cfg.intensity * 1.15f + 0.35f);
        }

        // ── Reuso del blur cacheado entre popups ──
        // Si el servicio compartido ya tiene un blur computado del mismo
        // snapshot (ej: popup anterior), reusamos su textura. Esto elimina
        // los 4-6 FBO passes de blur cuando se abren popups en cascada.
        CCSprite* blurredSprite = paimon::popupblur::reuseBlurForSnapshot(
            m_fields->m_snapshotTexture.data(),
            cfg.style, effectiveIntensity, cfg.darkness);

        if (!blurredSprite) {
            if (cfg.style == "paimonblur") {
                blurredSprite = Shaders::createPopupPaimonBlurredSprite(
                    m_fields->m_snapshotTexture.data(),
                    m_fields->m_snapshotSize,
                    effectiveIntensity
                );
            } else {
                blurredSprite = Shaders::createPopupBlurredSprite(
                    m_fields->m_snapshotTexture.data(),
                    m_fields->m_snapshotSize,
                    effectiveIntensity
                );
            }
            // Guardar en el cache compartido para el siguiente popup
            if (blurredSprite) {
                paimon::popupblur::storeBlurForSnapshot(
                    m_fields->m_snapshotTexture.data(),
                    blurredSprite, cfg.style, effectiveIntensity, cfg.darkness);
            }
        }

        // ── Fallback visual (Fase 1 fix) ──
        // Si el blur falla, usar un sprite negro como fallback para que nunca
        // se vea blanco. El overlay de darkness del buildBlurNode se encarga
        // de oscurecer apropiadamente.
        if (!blurredSprite) {
            geode::log::warn("[PopupBlur/Hook] Blur failed, using dark fallback");
            auto* fallbackTex = new CCTexture2D();
            unsigned char blackPixel[4] = {0, 0, 0, 255};
            fallbackTex->initWithData(blackPixel, kCCTexture2DPixelFormat_RGBA8888, 1, 1, CCSizeMake(1, 1));
            fallbackTex->autorelease();
            blurredSprite = CCSprite::createWithTexture(fallbackTex);
        }

        if (blurredSprite) {
            installFullscreenPopupBlur(blurredSprite, cfg);
        }

        m_fields->m_snapshotTexture = nullptr;
        m_fields->m_snapshotSize = CCSizeZero;
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

            // Fases de expansion y ajuste (muy fluidas)
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

    // Salida

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

        // -- paimonUI --
        if (sty == "paimonUI") {
            dur = 0.28f / spd;
            // Primero se estira un pelo, luego se encoge al boton
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
        // Posponer removeFromParentAndCleanup al siguiente frame para evitar retornar
        // a memoria liberada si el objeto se destruye durante la llamada.
        auto self = Ref<FLAlertLayer>(this);
        Loader::get()->queueInMainThread([self]() {
            self->removeFromParentAndCleanup(true);
        });
    }

    // --- hooks ---

    $override
    void show() {
        bool wantsBlur = shouldApplyPopupBlur();
        if (wantsBlur) {
            capturePopupBlurSnapshot();
        }

        FLAlertLayer::show();

        if (wantsBlur) {
            applyPopupBlur();
        }
        if (shouldAnimatePopup()) {
            runEntryAnimation();
        }
    }

    void draw() {
        // El blur node es sibling del popup en el scene graph (lo agregamos
        // al parent con z = popupZ-1), asi que cocos lo dibuja antes que el
        // popup automaticamente. NO necesitamos saltar el draw() del popup
        // — hacerlo dejaria el popup invisible si el blur fallara o si otro
        // mod inyecta contenido aqui.
        FLAlertLayer::draw();
    }

    $override
    void keyBackClicked() {
        if (!shouldAnimateExit()) {
            FLAlertLayer::keyBackClicked();
            return;
        }
        this->removeFromParentAndCleanup(true);
    }

    $override
    void removeFromParentAndCleanup(bool cleanup) {
        invalidateBlurRequest();

        if (!shouldAnimateExit() || m_fields->m_exiting) {
            // Sin animacion del popup: usar la duracion de fade configurada
            // para que el blur desaparezca suavemente.
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

        // Fade-out del blur sincronizado con la duracion estimada de la
        // animacion de salida. Usa el maximo entre la duracion de la animacion
        // y el setting de fade para que el blur no desaparezca antes que el popup.
        float spd = getExitSpeed();
        float sty_dur_max = 0.25f / spd; // aprox max de las exits styles
        float settingFade = std::clamp(
            static_cast<float>(paimon::settings::popupblur::fadeDuration()),
            0.0f, 1.0f);
        fadeOutAndRemoveBlur(std::max(sty_dur_max, settingFade));

        runExitAnimation();
    }

    // onExit se llama en el momento exacto en que el popup sale del scene graph,
    // ANTES de que las referencias cuenten a cero. Sin hookear esto, el blur
    // quedaba visible durante segundos en estos casos:
    //  - El usuario navega a otra scene con el popup aun abierto (SceneManager
    //    pushea una transicion — el popup queda retenido por la transicion
    //    anterior mientras el blur sigue pintandose).
    //  - Otro mod cierra el popup sin pasar por keyBackClicked (FLAlertLayer
    //    ::removeFromParent directo, o un $override de close).
    //  - El popup se cierra con un boton custom que NO llama keyBackClicked.
    // Hookear onExit garantiza cleanup instantaneo en todos esos paths.
    $override
    void onExit() {
        // Solo hacer cleanup si no fue hecho ya por keyBackClicked. El fade
        // de keyBackClicked ya nullificó m_blurNode, asi que este call
        // sera no-op en el flujo normal.
        if (m_fields->m_blurNode) {
            // No hay animacion en curso — remocion instantanea.
            removePopupBlurNode();
        }
        // El cleanup() del service se encarga de sacar del registry compartido
        // si de alguna manera quedo algo.
        paimon::popupblur::cleanup(this);
        FLAlertLayer::onExit();
    }

    ~PaimonDynamicPopupHook() {
        clearPopupBlurState();
    }
};


// ────────────────────────────────────────────────────────────────────────────
// Popup blur para clases que NO pasan por FLAlertLayer::show
//
// El hook anterior en FLAlertLayer::show captura la mayoria de popups (Popup<>,
// FLAlertLayer, etc). Pero ProfilePage y SetupTriggerPopup sobreescriben show()
// con su propia implementacion (ver bindings), asi que el hook virtual NO se
// dispara para ellos.
//
// Solucion: hook directo al show() de cada clase base, delegando al servicio
// compartido PopupBlurService. Cualquier subclase (SetupMoveCommandPopup,
// SetupPulseTriggerPopup, etc.) hereda del hook automaticamente.
//
// Exclusiones:
// - SetupShaderEffectPopup: el usuario necesita ver el shader en tiempo real
//   sobre el fondo de PlayLayer. Aplicarle blur lo haria inutilizable.
// - Cualquier popup flageado con paimon::markPopupBlurOptOut() tampoco recibe
//   blur — util para popups custom del mod que necesiten fondo nitido.
// ────────────────────────────────────────────────────────────────────────────

#include <Geode/binding/SetupShaderEffectPopup.hpp>

namespace {
// Lista de clases excluidas del popup blur. Cualquier popup cuyo typeinfo_cast
// a alguno de estos tipos sea no-null es dejado sin blur.
bool isShaderRelatedPopup(cocos2d::CCNode* popup) {
    if (!popup) return false;
    // SetupShaderEffectPopup: el trigger del editor que configura el shader
    // post-proceso. Necesita mostrar el fondo de gameplay en tiempo real
    // para que el usuario vea el efecto aplicandose.
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
