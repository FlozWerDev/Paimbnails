// ═══════════════════════════════════════════════════════════════
// TransitionHook: intercepta las transiciones del juego
//
// Hookea CCDirector::replaceScene, pushScene y popSceneWithTransition
// para reemplazar las transiciones nativas con la que configure el usuario.
//
// ARQUITECTURA:
//   - Solo intercepta escenas que YA vienen envueltas en CCTransitionScene
//     (no afecta replaceScene directos sin transicion).
//   - Detecta PlayLayer para aplicar levelEntryConfig si esta configurada.
//   - Guard de reentrada robusto con RAII para evitar doble intercepcion
//     (especialmente desde CustomTransitionScene::onTransitionFinished).
// ═══════════════════════════════════════════════════════════════

#include <Geode/Geode.hpp>
#include <Geode/modify/CCDirector.hpp>
#include <exception>
#include "../services/TransitionManager.hpp"
#include "../ui/CustomTransitionScene.hpp"

using namespace geode::prelude;

// flag para evitar recursion infinita (protegido con RAII)
static std::atomic<bool> s_applying{false};

// el juego no esta listo hasta que MenuLayer aparezca al menos una vez
static std::atomic<bool> s_gameReady{false};

// RAII guard para s_applying
// garantiza que se restaure aunque haya un return temprano o una excepcion
struct ApplyingGuard {
    ApplyingGuard()  { s_applying.store(true); }
    ~ApplyingGuard() { s_applying.store(false); }
    ApplyingGuard(ApplyingGuard const&) = delete;
    ApplyingGuard& operator=(ApplyingGuard const&) = delete;
};

// extrae la escena destino real de una CCTransitionScene
static CCScene* unwrapTransition(CCTransitionScene* trans) {
    if (trans && trans->m_pInScene) return trans->m_pInScene;
    return nullptr;
}

// Detecta si la transicion ya fue creada por otro mod custom (no por
// cocos/GD vanilla). Si el typeid no es de cocos2d::CCTransition*, asumimos
// que es de otro mod y dejamos pasar tal cual para no pisar su intencion.
//
// Cocos2d-x ships con: CCTransitionFade, CCTransitionMoveInL/R/T/B,
// CCTransitionSlideInL/R/T/B, CCTransitionShrinkGrow, CCTransitionRotoZoom,
// CCTransitionFlipX/Y/Angular, CCTransitionZoomFlipX/Y/Angular,
// CCTransitionCrossFade, CCTransitionTurnOffTiles, CCTransitionSplitCols/Rows,
// CCTransitionFadeTR/BL/Up/Down, CCTransitionPageTurn, CCTransitionProgress*.
// Todos llevan "cocos2d::CCTransition" en su typeid.
static bool isVanillaTransition(CCTransitionScene* trans) {
    if (!trans) return false;
    char const* name = typeid(*trans).name();
    if (!name) return false;
    std::string_view sv(name);
    // MSVC: "class cocos2d::CCTransitionFade", GCC: "N7cocos2d18CCTransitionFadeE"
    // Buscamos el patron comun.
    return sv.find("cocos2d") != std::string_view::npos &&
           sv.find("CCTransition") != std::string_view::npos;
}

static bool shouldIntercept() {
    if (s_applying) return false;
    if (!s_gameReady) return false;
    if (!TransitionManager::get().isEnabled()) return false;
    return true;
}

// ── Detecta si la escena destino contiene un PlayLayer ──────
// Esto permite aplicar levelEntryConfig cuando el usuario navega hacia un nivel.
static bool destContainsPlayLayer(CCScene* scene) {
    return scene && scene->getChildByType<PlayLayer>(0);
}

// ── Selecciona la configuracion de transicion apropiada ─────
// Si la escena destino contiene PlayLayer y hay una config de nivel configurada,
// usa esa; de lo contrario usa la global.
static TransitionConfig selectConfig(CCScene* destScene) {
    auto& tm = TransitionManager::get();
    if (tm.hasLevelEntryConfig() && destContainsPlayLayer(destScene)) {
        return tm.getLevelEntryConfig();
    }
    return tm.getGlobalConfig();
}

static CCScene* createTransitionSafe(CCScene* realDest, TransitionConfig const& cfg) {
    auto& tm = TransitionManager::get();
    if (!realDest || tm.isCustomSafeModeTripped()) {
        auto fallbackCfg = cfg;
        fallbackCfg.type = TransitionType::Fade;
        auto* fallback = tm.createNativeTransition(fallbackCfg, realDest);
        return fallback ? static_cast<CCScene*>(fallback) : realDest;
    }

    auto* trans = tm.createTransition(cfg, realDest);
    if (trans) return trans;

    // createTransition returned nullptr — trip safe mode and fallback
    tm.tripCustomSafeMode("createTransition returned nullptr");
    log::warn("[TransitionHook] createTransition returned nullptr, falling back");

    auto fallbackCfg = cfg;
    fallbackCfg.type = TransitionType::Fade;
    auto* fallback = tm.createNativeTransition(fallbackCfg, realDest);
    return fallback ? static_cast<CCScene*>(fallback) : realDest;
}

class $modify(PaimonDirector, CCDirector) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPre("cocos2d::CCDirector::replaceScene", geode::Priority::VeryLate);
        (void)self.setHookPriorityPre("cocos2d::CCDirector::pushScene", geode::Priority::VeryLate);
        (void)self.setHookPriorityPre("cocos2d::CCDirector::popSceneWithTransition", geode::Priority::VeryLate);
        // Nota: NO hookeamos popScene() (plain) — algunos mods lo usan
        // para volver atras sin transicion intencionalmente, y reemplazarlo
        // por nuestra transicion rompe esa intencion. Solo interceptamos
        // transiciones que YA vienen envueltas en CCTransitionScene.
    }

    // ── replaceScene ──
    bool replaceScene(CCScene* scene) {
        if (!scene) return CCDirector::replaceScene(scene);

        // Detectar cuando MenuLayer aparece → juego listo
        if (!s_gameReady) {
            bool foundMenu = false;
            if (scene->getChildByType<MenuLayer>(0)) foundMenu = true;
            if (!foundMenu) {
                if (auto* trans = typeinfo_cast<CCTransitionScene*>(scene)) {
                    if (trans->m_pInScene && trans->m_pInScene->getChildByType<MenuLayer>(0))
                        foundMenu = true;
                }
            }
            if (foundMenu) {
                s_gameReady = true;
            }
            // Dejar pasar la primera transicion a MenuLayer sin interceptar
            return CCDirector::replaceScene(scene);
        }

        if (!shouldIntercept()) return CCDirector::replaceScene(scene);

        // No re-interceptar nuestras propias CustomTransitionScene
        if (typeinfo_cast<CustomTransitionScene*>(scene)) return CCDirector::replaceScene(scene);

        // Solo interceptar si viene envuelta en CCTransitionScene
        auto* nativeTrans = typeinfo_cast<CCTransitionScene*>(scene);
        if (!nativeTrans) return CCDirector::replaceScene(scene);

        // No pisar transiciones custom de otros mods. Solo reemplazamos
        // transiciones vanilla de cocos2d/GD.
        if (!isVanillaTransition(nativeTrans)) return CCDirector::replaceScene(scene);

        CCScene* realDest = unwrapTransition(nativeTrans);
        if (!realDest) return CCDirector::replaceScene(scene);

        // Ref<> gestiona el refcount automaticamente — prohibido retain/release directo
        Ref<CCScene> safeDest = realDest;
        auto cfg = selectConfig(realDest);
        ApplyingGuard guard;
        auto* ourTrans = createTransitionSafe(realDest, cfg);
        return CCDirector::replaceScene(ourTrans ? ourTrans : realDest);
    }

    // ── pushScene ──
    bool pushScene(CCScene* scene) {
        if (!scene || !shouldIntercept()) return CCDirector::pushScene(scene);

        // No re-interceptar nuestras propias CustomTransitionScene
        if (typeinfo_cast<CustomTransitionScene*>(scene)) return CCDirector::pushScene(scene);

        auto* nativeTrans = typeinfo_cast<CCTransitionScene*>(scene);
        if (!nativeTrans) return CCDirector::pushScene(scene);

        // No pisar transiciones custom de otros mods.
        if (!isVanillaTransition(nativeTrans)) return CCDirector::pushScene(scene);

        CCScene* realDest = unwrapTransition(nativeTrans);
        if (!realDest) return CCDirector::pushScene(scene);

        // Ref<> gestiona el refcount automaticamente — prohibido retain/release directo
        Ref<CCScene> safeDest = realDest;
        auto cfg = selectConfig(realDest);
        ApplyingGuard guard;
        auto* ourTrans = createTransitionSafe(realDest, cfg);
        return CCDirector::pushScene(ourTrans ? ourTrans : realDest);
    }

// ── popSceneWithTransition (cubre la mayoria de "back" en GD) ──
    bool popSceneWithTransition(float duration, PopTransition type) {
        if (!shouldIntercept()) {
            return CCDirector::popSceneWithTransition(duration, type);
        }

        auto& stack = m_pobScenesStack;
        if (!stack || stack->count() < 2) {
            return CCDirector::popSceneWithTransition(duration, type);
        }

        auto* destScene = typeinfo_cast<CCScene*>(stack->objectAtIndex(stack->count() - 2));
        if (!destScene) {
            return CCDirector::popSceneWithTransition(duration, type);
        }

        // CRITICO: Capturar fromScene ANTES de popScene
        // para que la transicion tenga la escena "from" correcta
        auto* fromScene = m_pRunningScene;
        Ref<CCScene> safeFrom = fromScene;
        Ref<CCScene> safeDest = destScene;

        auto cfg = selectConfig(destScene);

        ApplyingGuard guard;
        // Hacer pop normal (remueve la escena actual del stack)
        CCDirector::popScene();

        // Restaurar temporalmente m_pRunningScene a fromScene
        // para que createTransitionSafe/getRunningScene capturen
        // la escena "from" correcta (no destScene que es la misma)
        auto* savedRunning = m_pRunningScene;
        m_pRunningScene = fromScene;

        auto* ourTrans = createTransitionSafe(destScene, cfg);

        // Restaurar (ourTrans reemplazara savedRunning como running scene)
        m_pRunningScene = savedRunning;

        return CCDirector::replaceScene(ourTrans ? ourTrans : destScene);
    }

    // ── popScene (sin transicion nativa) ──
    //
    // CRITICO para compat con otros mods: NO interceptamos el plain popScene().
    //
    // Mods como Globed/BetterInfo usan popScene() explicitamente cuando NO
    // quieren transicion (volver instantaneo a la escena anterior). Si lo
    // sustituimos por replaceScene+nuestra transicion, rompemos esa
    // intencionalidad. Mantenemos passthrough total: solo interceptamos
    // transiciones que YA vienen envueltas en CCTransitionScene
    // (replaceScene/pushScene/popSceneWithTransition).
    void popScene() {
        CCDirector::popScene();
    }
};
