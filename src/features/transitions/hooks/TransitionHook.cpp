// TransitionHook: intercepts GD scene transitions.
//
// Hooks CCDirector::replaceScene, pushScene and popSceneWithTransition
// to replace native transitions with the one the user configured.
//
// Only intercepts scenes already wrapped in a CCTransitionScene.
// Detects PlayLayer for levelEntryConfig.
// RAII re-entry guard prevents double-interception.

#include <Geode/Geode.hpp>
#include <Geode/modify/CCDirector.hpp>
#include <exception>
#include "../services/TransitionManager.hpp"
#include "../ui/CustomTransitionScene.hpp"

using namespace geode::prelude;

// Re-entry guard (RAII).
static std::atomic<bool> s_applying{false};

// True after MenuLayer appears at least once.
static std::atomic<bool> s_gameReady{false};

// RAII guard for s_applying.
struct ApplyingGuard {
    ApplyingGuard()  { s_applying.store(true); }
    ~ApplyingGuard() { s_applying.store(false); }
    ApplyingGuard(ApplyingGuard const&) = delete;
    ApplyingGuard& operator=(ApplyingGuard const&) = delete;
};

// Extract the real destination scene from a CCTransitionScene.
static CCScene* unwrapTransition(CCTransitionScene* trans) {
    if (trans && trans->m_pInScene) return trans->m_pInScene;
    return nullptr;
}

// Returns true if the transition is a stock cocos2d/GD transition (not from another mod).
// All cocos2d transitions have "cocos2d::CCTransition" in their typeid name.
static bool isVanillaTransition(CCTransitionScene* trans) {
    if (!trans) return false;
    char const* name = typeid(*trans).name();
    if (!name) return false;
    std::string_view sv(name);
    // MSVC: "class cocos2d::CCTransitionFade", GCC: "N7cocos2d18CCTransitionFadeE"
    return sv.find("cocos2d") != std::string_view::npos &&
           sv.find("CCTransition") != std::string_view::npos;
}

static bool shouldIntercept() {
    if (s_applying) return false;
    if (!s_gameReady) return false;
    if (!TransitionManager::get().isEnabled()) return false;
    return true;
}

// Detecta si la escena destino contiene un PlayLayer
// Esto permite aplicar levelEntryConfig cuando el usuario navega hacia un nivel.
static bool destContainsPlayLayer(CCScene* scene) {
    return scene && scene->getChildByType<PlayLayer>(0);
}

// Use levelEntryConfig when navigating to PlayLayer, otherwise use global.
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
        // Don't hook popScene() — some mods use it intentionally for an immediate back.
    }

    // replaceScene
    bool replaceScene(CCScene* scene) {
        if (!scene) return CCDirector::replaceScene(scene);

        // Mark game as ready when MenuLayer first appears; let this transition through.
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
            // Let the first MenuLayer transition through.
            return CCDirector::replaceScene(scene);
        }

        if (!shouldIntercept()) return CCDirector::replaceScene(scene);

        // Don't re-intercept our own CustomTransitionScene.
        if (typeinfo_cast<CustomTransitionScene*>(scene)) return CCDirector::replaceScene(scene);

        // Only intercept scenes already wrapped in CCTransitionScene.
        auto* nativeTrans = typeinfo_cast<CCTransitionScene*>(scene);
        if (!nativeTrans) return CCDirector::replaceScene(scene);

        // Don't intercept custom transitions from other mods.
        if (!isVanillaTransition(nativeTrans)) return CCDirector::replaceScene(scene);

        CCScene* realDest = unwrapTransition(nativeTrans);
        if (!realDest) return CCDirector::replaceScene(scene);

        // Ref<> manages refcount automatically.
        Ref<CCScene> safeDest = realDest;
        auto cfg = selectConfig(realDest);
        ApplyingGuard guard;
        auto* ourTrans = createTransitionSafe(realDest, cfg);
        return CCDirector::replaceScene(ourTrans ? ourTrans : realDest);
    }

    // pushScene
    bool pushScene(CCScene* scene) {
        if (!scene || !shouldIntercept()) return CCDirector::pushScene(scene);

        // Don't re-intercept our own CustomTransitionScene.
        if (typeinfo_cast<CustomTransitionScene*>(scene)) return CCDirector::pushScene(scene);

        auto* nativeTrans = typeinfo_cast<CCTransitionScene*>(scene);
        if (!nativeTrans) return CCDirector::pushScene(scene);

        // Don't intercept custom transitions from other mods.
        if (!isVanillaTransition(nativeTrans)) return CCDirector::pushScene(scene);

        CCScene* realDest = unwrapTransition(nativeTrans);
        if (!realDest) return CCDirector::pushScene(scene);

        // Ref<> manages refcount automatically.
        Ref<CCScene> safeDest = realDest;
        auto cfg = selectConfig(realDest);
        ApplyingGuard guard;
        auto* ourTrans = createTransitionSafe(realDest, cfg);
        return CCDirector::pushScene(ourTrans ? ourTrans : realDest);
    }

// popSceneWithTransition (covers most "back" navigation in GD)
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

        // Capture fromScene BEFORE popScene so the transition has the correct "from" scene.
        auto* fromScene = m_pRunningScene;
        Ref<CCScene> safeFrom = fromScene;
        Ref<CCScene> safeDest = destScene;

        auto cfg = selectConfig(destScene);

        ApplyingGuard guard;
        // Normal pop (removes current scene from the stack).
        CCDirector::popScene();

        // Temporarily restore m_pRunningScene to fromScene so createTransitionSafe
        // sees the correct "from" scene.
        auto* savedRunning = m_pRunningScene;
        m_pRunningScene = fromScene;

        auto* ourTrans = createTransitionSafe(destScene, cfg);

        m_pRunningScene = savedRunning;

        return CCDirector::replaceScene(ourTrans ? ourTrans : destScene);
    }

    // Plain popScene() is not intercepted to avoid breaking mods that use it
    // for intentional instant-back navigation (e.g. Globed, BetterInfo).
    void popScene() {
        CCDirector::popScene();
    }
};
