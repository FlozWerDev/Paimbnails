// Replace supported native GD transitions with the user's configured one.
// Custom transitions and plain popScene calls are left untouched.

#include <Geode/Geode.hpp>
#include <Geode/modify/CCDirector.hpp>
#include <Geode/modify/GameManager.hpp>
#include <exception>
#include "../services/LevelEntryEffects.hpp"
#include "../services/TransitionManager.hpp"
#include "../ui/CustomTransitionScene.hpp"

using namespace geode::prelude;

static std::atomic<bool> s_applying{false};

static std::atomic<bool> s_gameReady{false};

struct ApplyingGuard {
    ApplyingGuard()  { s_applying.store(true); }
    ~ApplyingGuard() { s_applying.store(false); }
    ApplyingGuard(ApplyingGuard const&) = delete;
    ApplyingGuard& operator=(ApplyingGuard const&) = delete;
};

static CCScene* unwrapTransition(CCTransitionScene* trans) {
    if (trans && trans->m_pInScene) return trans->m_pInScene;
    return nullptr;
}

// Stock transitions contain "cocos2d::CCTransition" in their type name.
static bool isVanillaTransition(CCTransitionScene* trans) {
    if (!trans) return false;
    char const* name = typeid(*trans).name();
    if (!name) return false;
    std::string_view sv(name);
    return sv.find("cocos2d") != std::string_view::npos &&
           sv.find("CCTransition") != std::string_view::npos;
}

static bool canIntercept() {
    if (s_applying) return false;
    if (!s_gameReady) return false;
    if (CustomTransitionScene::isActive()) return false;
    return true;
}

// Detect PlayLayer destinations so levelEntryConfig can override the global preset.
static bool destContainsPlayLayer(CCScene* scene) {
    return scene && scene->getChildByType<PlayLayer>(0);
}

static TransitionConfig selectConfig(CCScene* destScene) {
    auto& tm = TransitionManager::get();
    if (tm.hasLevelEntryConfig() && destContainsPlayLayer(destScene)) {
        return tm.getLevelEntryConfig();
    }
    return tm.getGlobalConfig();
}

static CCScene* createTransitionSafe(
    CCScene* realDest,
    TransitionConfig const& cfg,
    bool isPush = false)
{
    auto& tm = TransitionManager::get();
    if (!realDest || tm.isCustomSafeModeTripped()) {
        auto fallbackCfg = cfg;
        fallbackCfg.type = TransitionType::Fade;
        auto* fallback = tm.createNativeTransition(fallbackCfg, realDest);
        return fallback ? static_cast<CCScene*>(fallback) : realDest;
    }

    auto* trans = tm.createTransition(cfg, realDest, isPush);
    if (trans) return trans;

// If transition creation fails, enter safe mode and fall back.
    tm.tripCustomSafeMode("createTransition returned nullptr");
    log::warn("[TransitionHook] createTransition returned nullptr, falling back");

    auto fallbackCfg = cfg;
    fallbackCfg.type = TransitionType::Fade;
    auto* fallback = tm.createNativeTransition(fallbackCfg, realDest);
    return fallback ? static_cast<CCScene*>(fallback) : realDest;
}

class $modify(PaimonDirector, CCDirector) {
    static void onModify(auto& self) {
        if (self.setHookPriorityBeforePre(
                "cocos2d::CCDirector::replaceScene",
                "undefined0.smooth-level-enter"
            ).isErr()) {
            (void)self.setHookPriorityPre(
                "cocos2d::CCDirector::replaceScene",
                geode::Priority::VeryEarly
            );
        }
        (void)self.setHookPriorityPre("cocos2d::CCDirector::pushScene", geode::Priority::VeryEarly);
        (void)self.setHookPriorityPre("cocos2d::CCDirector::popSceneWithTransition", geode::Priority::VeryEarly);
// Leave plain popScene() alone; some mods use it for an intentional instant back.
    }

    bool replaceScene(CCScene* scene) {
        if (!scene) return CCDirector::replaceScene(scene);

        if (paimon::transitions::isLevelExitTransitionPending()) {
            log::info("[TransitionHook] Exit replace received (ready: {}, applying: {})",
                s_gameReady.load(), s_applying.load());
        }

    // Let the first MenuLayer transition through and mark the game ready.
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
            return CCDirector::replaceScene(scene);
        }

        if (!canIntercept()) return CCDirector::replaceScene(scene);

    // Do not re-intercept our own custom scene.
        if (typeinfo_cast<CustomTransitionScene*>(scene)) return CCDirector::replaceScene(scene);

    // Only replace transitions already wrapped in CCTransitionScene.
        auto* nativeTrans = typeinfo_cast<CCTransitionScene*>(scene);
        if (!nativeTrans) return CCDirector::replaceScene(scene);

        CCScene* realDest = unwrapTransition(nativeTrans);
        if (!realDest) return CCDirector::replaceScene(scene);

        Ref<CCScene> safeDest = realDest;
        if (destContainsPlayLayer(realDest) &&
            paimon::transitions::shouldUseLevelEntryTransition()) {
            ApplyingGuard guard;
            if (auto* levelTransition =
                    paimon::transitions::createLevelEntryTransition(realDest)) {
                log::debug("[TransitionHook] Using Smooth+ for PlayLayer");
                return CCDirector::replaceScene(levelTransition);
            }
            log::warn("[TransitionHook] Level entry transition could not be created; using configured fallback");
        }

        bool leavingLevel = paimon::transitions::isLevelExitTransitionPending() ||
            destContainsPlayLayer(m_pRunningScene);
        if (!destContainsPlayLayer(realDest) && leavingLevel &&
            paimon::transitions::shouldUseLevelExitTransition()) {
            ApplyingGuard guard;
            if (auto* levelTransition =
                    paimon::transitions::createLevelExitTransition(realDest)) {
                log::debug("[TransitionHook] Using Smooth+ while leaving PlayLayer (pending: {})",
                    paimon::transitions::isLevelExitTransitionPending());
                return CCDirector::replaceScene(levelTransition);
            }
            log::warn("[TransitionHook] Level exit transition could not be created; using configured fallback");
        }

        if (!TransitionManager::get().isEnabled()) {
            return CCDirector::replaceScene(scene);
        }

    // Do not replace custom transitions from other mods.
        if (!isVanillaTransition(nativeTrans)) return CCDirector::replaceScene(scene);

        auto cfg = selectConfig(realDest);
        ApplyingGuard guard;
        auto* ourTrans = createTransitionSafe(realDest, cfg);
        return CCDirector::replaceScene(ourTrans ? ourTrans : realDest);
    }

    bool pushScene(CCScene* scene) {
        if (!scene || !canIntercept()) return CCDirector::pushScene(scene);

        if (typeinfo_cast<CustomTransitionScene*>(scene)) return CCDirector::pushScene(scene);

        auto* nativeTrans = typeinfo_cast<CCTransitionScene*>(scene);
        if (!nativeTrans) return CCDirector::pushScene(scene);

        CCScene* realDest = unwrapTransition(nativeTrans);
        if (!realDest) return CCDirector::pushScene(scene);

        Ref<CCScene> safeDest = realDest;
        if (destContainsPlayLayer(realDest) &&
            paimon::transitions::shouldUseLevelEntryTransition()) {
            ApplyingGuard guard;
            if (auto* levelTransition =
                    paimon::transitions::createLevelEntryTransition(realDest)) {
                return CCDirector::pushScene(levelTransition);
            }
        }

        if (!destContainsPlayLayer(realDest) &&
            destContainsPlayLayer(m_pRunningScene) &&
            paimon::transitions::shouldUseLevelExitTransition()) {
            ApplyingGuard guard;
            if (auto* levelTransition =
                    paimon::transitions::createLevelExitTransition(realDest)) {
                return CCDirector::pushScene(levelTransition);
            }
        }

        if (!TransitionManager::get().isEnabled()) return CCDirector::pushScene(scene);
        if (!isVanillaTransition(nativeTrans)) return CCDirector::pushScene(scene);

        auto cfg = selectConfig(realDest);
        ApplyingGuard guard;
        auto* ourTrans = createTransitionSafe(realDest, cfg, true);
        return CCDirector::pushScene(ourTrans ? ourTrans : realDest);
    }

    bool popSceneWithTransition(float duration, PopTransition type) {
        if (!canIntercept()) {
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

    // Capture fromScene before popping so the replacement has the correct source.
        auto* fromScene = m_pRunningScene;
        Ref<CCScene> safeFrom = fromScene;
        Ref<CCScene> safeDest = destScene;

        bool useLevelExit = destContainsPlayLayer(fromScene) &&
            !destContainsPlayLayer(destScene) &&
            paimon::transitions::shouldUseLevelExitTransition();
        if (!useLevelExit && !TransitionManager::get().isEnabled()) {
            return CCDirector::popSceneWithTransition(duration, type);
        }

        auto cfg = selectConfig(destScene);

        ApplyingGuard guard;
        CCDirector::popScene();

    // Temporarily restore m_pRunningScene so createTransitionSafe sees the source.
        auto* savedRunning = m_pRunningScene;
        m_pRunningScene = fromScene;

        CCScene* ourTrans = useLevelExit
            ? static_cast<CCScene*>(paimon::transitions::createLevelExitTransition(destScene))
            : createTransitionSafe(destScene, cfg);

        m_pRunningScene = savedRunning;

        return CCDirector::replaceScene(ourTrans ? ourTrans : destScene);
    }

    void popScene() {
        CCDirector::popScene();
    }
};

class $modify(PaimonTransitionGameManager, GameManager) {
    static void onModify(auto& self) {
        if (self.setHookPriorityBeforePre(
                "GameManager::returnToLastScene",
                "dankmeme.globed2"
            ).isErr()) {
            (void)self.setHookPriorityPre(
                "GameManager::returnToLastScene",
                geode::Priority::VeryEarly
            );
        }
    }

    void returnToLastScene(GJGameLevel* level) {
        auto* playLayer = PlayLayer::get();
        paimon::transitions::beginLevelExitTransition(playLayer);
        log::info("[TransitionHook] GameManager::returnToLastScene (playLayer: {})",
            playLayer != nullptr);
        GameManager::returnToLastScene(level);
        paimon::transitions::endLevelExitTransition();
    }
};
