#include "PopupBlurService.hpp"

#include <Geode/Geode.hpp>
#include "../core/Settings.hpp"
#include "../core/RuntimeLifecycle.hpp"
#include "../framework/compat/ModCompat.hpp"
#include "PaiblurNode.hpp"
#include <algorithm>
#include <unordered_map>

using namespace cocos2d;
using namespace geode::prelude;

namespace paimon::popupblur {

bool isEditorContextActive() {
    auto* director = CCDirector::get();
    if (!director) return false;
    auto* scene = director->getRunningScene();
    if (!scene) return false;
    return scene->getChildByType<LevelEditorLayer>(0) != nullptr ||
           scene->getChildByType<EditorUI>(0) != nullptr;
}

static std::string const& blurNodeIdKey() {
    static std::string const key = Mod::get()->getID() + "/popup-blur-node-id";
    return key;
}

struct RegistryEntry {
    CCNode* popupPtr = nullptr;    // identity only
    Ref<CCNode> blurRef;
    WeakRef<CCNode> blurWeak;       // liveness oracle
    CCNode* parentPtr = nullptr;    // registration snapshot
    WeakRef<CCNode> parentWeak;     // liveness oracle
    float ageSeconds = 0.f;
    bool fadingOut = false;
};

// Return the blur only while its WeakRef proves it is alive.
static CCNode* liveBlur(RegistryEntry const& e) {
    return e.blurWeak.lock().data();
}

// Parent liveness guard: never walk or detach a node after its parent dies.
static bool parentAlive(RegistryEntry const& e) {
    return e.parentWeak.valid();
}

static std::unordered_map<CCNode*, RegistryEntry>& blurRegistry() {
    static auto* map = new std::unordered_map<CCNode*, RegistryEntry>();
    return *map;
}

class WatchdogTarget : public cocos2d::CCObject {
public:
    void tick(float dt);
};

static bool g_watchdogScheduled = false;

static WatchdogTarget* getWatchdogTarget() {
    static WatchdogTarget* target = []() {
        auto* t = new WatchdogTarget();
        t->autorelease();
        t->retain();
        return t;
    }();
    return target;
}

static void fadeAndRemoveBlurNode(CCNode* blur, float duration) {
    if (!blur || !blur->getParent()) return;
    blur->stopAllActions();
    if (duration <= 0.01f) {
        blur->removeFromParent();
        return;
    }
    blur->runAction(CCSequence::create(
        CCFadeTo::create(duration, 0),
        CCCallFunc::create(blur, callfunc_selector(CCNode::removeFromParent)),
        nullptr
    ));
}

static void scheduleWatchdogIfNeeded() {
    if (g_watchdogScheduled) return;
    auto* director = CCDirector::get();
    if (!director) return;
    auto* scheduler = director->getScheduler();
    if (!scheduler) return;
    scheduler->scheduleSelector(
        schedule_selector(WatchdogTarget::tick),
        getWatchdogTarget(),
        0.25f,
        false
    );
    g_watchdogScheduled = true;
}

static void unscheduleWatchdogIfIdle() {
    if (!g_watchdogScheduled) return;
    if (!blurRegistry().empty()) return;
    auto* director = CCDirector::get();
    if (!director) return;
    auto* scheduler = director->getScheduler();
    if (!scheduler) return;
    scheduler->unscheduleSelector(
        schedule_selector(WatchdogTarget::tick),
        getWatchdogTarget()
    );
    g_watchdogScheduled = false;
}

static CCNode* findSceneRoot(CCNode* node) {
    if (!node) return nullptr;
    CCNode* cur = node;
    while (cur->getParent()) cur = cur->getParent();
    return cur;
}

static bool popupStillChildOf(CCNode* parent, CCNode* popupPtr) {
    if (!parent || !popupPtr) return false;
    auto* children = parent->getChildren();
    if (!children) return false;
    int count = children->count();
    for (int i = 0; i < count; ++i) {
        if (children->objectAtIndex(i) == static_cast<cocos2d::CCObject*>(popupPtr)) {
            return true;
        }
    }
    return false;
}

void WatchdogTarget::tick(float dt) {
    auto& reg = blurRegistry();
    if (reg.empty()) {
        unscheduleWatchdogIfIdle();
        return;
    }

    auto* director = CCDirector::get();
    auto* runningScene = director ? director->getRunningScene() : nullptr;

    std::vector<CCNode*> toRemoveFromRegistry;
    std::vector<CCNode*> toFadeBlurs;
    std::vector<CCNode*> toInstantRemoveBlurs;

    for (auto& [popupKey, entry] : reg) {
        entry.ageSeconds += dt;

        CCNode* blur = liveBlur(entry);
        if (!blur) {                                       // freed
            toRemoveFromRegistry.push_back(popupKey);
            continue;
        }
        if (!parentAlive(entry) || !blur->getParent()) {   // orphaned: drop it
            toRemoveFromRegistry.push_back(popupKey);
            continue;
        }

        if (runningScene) {
            CCNode* blurScene = findSceneRoot(blur);
            if (blurScene && blurScene != runningScene) {
                toInstantRemoveBlurs.push_back(blur);
                toRemoveFromRegistry.push_back(popupKey);
                continue;
            }
        }

        if (entry.fadingOut) continue;

        CCNode* parent = blur->getParent();
        entry.parentPtr = parent;

        bool popupPresent = popupStillChildOf(parent, popupKey);
        if (!popupPresent && entry.ageSeconds > 0.2f) {
            toFadeBlurs.push_back(blur);
            entry.fadingOut = true;
            continue;
        }
    }

    for (auto* b : toInstantRemoveBlurs) {
        if (b && b->getParent()) b->removeFromParent();
    }
    for (auto* b : toFadeBlurs) {
        fadeAndRemoveBlurNode(b, 0.18f);
    }
    for (auto* k : toRemoveFromRegistry) {
        reg.erase(k);
    }

    if (reg.empty()) {
        unscheduleWatchdogIfIdle();
    }
}

static void pruneDeadEntries() {
    auto& reg = blurRegistry();
    for (auto it = reg.begin(); it != reg.end();) {
        CCNode* blur = liveBlur(it->second);
        if (!blur || !parentAlive(it->second) || !blur->getParent()) {
            it = reg.erase(it);
        } else {
            ++it;
        }
    }
}

Config getConfig() {
    Config cfg;
    cfg.enabled = paimon::settings::popupblur::enabled();
    cfg.style = "paiblur";
    cfg.intensity = std::max(0.1f, static_cast<float>(paimon::settings::popupblur::intensity()));
    cfg.darkness = std::clamp(static_cast<float>(paimon::settings::popupblur::darkness()), 0.0f, 1.0f);
    return cfg;
}

void registerExternalBlur(CCNode* popup, CCNode* blurNode) {
    if (!popup || !blurNode) return;
    pruneDeadEntries();
    RegistryEntry entry;
    entry.popupPtr = popup;
    entry.blurRef = Ref<CCNode>(blurNode);
    entry.blurWeak = blurNode;
    entry.parentPtr = popup->getParent();
    entry.parentWeak = popup->getParent();
    entry.ageSeconds = 0.f;
    entry.fadingOut = false;
    blurRegistry()[popup] = std::move(entry);
    scheduleWatchdogIfNeeded();
}

static cocos2d::CCNode* applyPaiblurDynamic(CCNode* popup, CCNode* parent, Config const& cfg) {
    if (!popup || !parent) return nullptr;
    auto* director = CCDirector::get();
    if (!director) return nullptr;
    auto winSize = director->getWinSize();
    if (winSize.width <= 0.f || winSize.height <= 0.f) return nullptr;

    auto* paiblur = paimon::paiblur::PaiblurNode::create(winSize, cfg.intensity, cfg.darkness);
    if (!paiblur) return nullptr;

    parent->addChild(paiblur, popup->getZOrder() - 1);

    {
        auto& reg = blurRegistry();
        for (auto& [key, entry] : reg) {
            if (key == popup) continue;
            CCNode* prevBlur = liveBlur(entry);
            if (prevBlur && prevBlur->isVisible() && !entry.fadingOut) {
                prevBlur->setVisible(false);
            }
        }
    }

    RegistryEntry entry;
    entry.popupPtr = popup;
    entry.blurRef = Ref<CCNode>(paiblur);
    entry.blurWeak = paiblur;
    entry.parentPtr = parent;
    entry.parentWeak = parent;
    entry.ageSeconds = 0.f;
    entry.fadingOut = false;
    blurRegistry()[popup] = std::move(entry);
    scheduleWatchdogIfNeeded();

    float fadeDuration = std::clamp(
        static_cast<float>(paimon::settings::popupblur::fadeDuration()),
        0.0f, 1.0f);
    paiblur->fadeIn(fadeDuration);

    return paiblur;
}

bool captureAndApply(CCNode* popup) {
    if (!popup) return false;
    auto cfg = getConfig();
    if (!cfg.enabled) return false;
    return captureAndApplyWithConfig(popup, cfg);
}

bool captureAndApplyWithConfig(CCNode* popup, Config cfg) {
    if (!popup) return false;

    if (isEditorContextActive()) return false;

    if (paimon::compat::ModCompat::externalGlobalBlurActive()) {
        return false;
    }

    auto* parent = popup->getParent();
    if (!parent) return false;

    cleanup(popup);

    cfg.style = "paiblur";
    if (auto* applied = applyPaiblurDynamic(popup, parent, cfg)) {
        return true;
    }

    log::warn("[PopupBlur] Paiblur unavailable; popup blur skipped");
    return false;
}

static void cleanupImpl(CCNode* popup, float fadeDuration) {
    if (!popup) return;

    auto& reg = blurRegistry();
    auto it = reg.find(popup);
    if (it == reg.end()) return;

    // Move the entry out before erasing so its Ref stays valid.
    RegistryEntry entry = std::move(it->second);
    reg.erase(it);
    unscheduleWatchdogIfIdle();

    // Restore sibling blurs hidden behind this one.
    for (auto& [key, other] : reg) {
        CCNode* prevBlur = liveBlur(other);
        if (prevBlur && !prevBlur->isVisible() && !other.fadingOut) {
            prevBlur->setVisible(true);
        }
    }

    CCNode* blurNode = liveBlur(entry);
    // Drop freed/orphaned nodes without touching their dangling parent.
    if (!blurNode || !parentAlive(entry) || !blurNode->getParent()) return;

    if (fadeDuration <= 0.01f) {
        Ref<CCNode> keepAlive = Ref<CCNode>(blurNode);
        Loader::get()->queueInMainThread([keepAlive]() {
            if (paimon::isRuntimeShuttingDown()) return;
            if (auto* node = keepAlive.data(); node && node->getParent()) {
                node->removeFromParent();
            }
        });
        return;
    }

    blurNode->stopAllActions();
    blurNode->runAction(CCSequence::create(
        CCFadeTo::create(fadeDuration, 0),
        CCCallFunc::create(blurNode, callfunc_selector(CCNode::removeFromParent)),
        nullptr
    ));
}

void cleanup(CCNode* popup) {
    cleanupImpl(popup, 0.f);
}

void cleanupWithFade(CCNode* popup, float duration) {
    cleanupImpl(popup, duration);
}

void cleanupAllActive(float fadeDuration) {
    auto& reg = blurRegistry();
    if (reg.empty()) return;

    // Fade only live parents; reg.clear() disposes orphaned entries safely.
    std::vector<Ref<CCNode>> toFade;
    toFade.reserve(reg.size());
    for (auto& [_, entry] : reg) {
        CCNode* blur = liveBlur(entry);
        if (blur && parentAlive(entry) && blur->getParent()) {
            toFade.push_back(Ref<CCNode>(blur));
        }
    }
    reg.clear();
    unscheduleWatchdogIfIdle();

    for (auto& ref : toFade) {
        fadeAndRemoveBlurNode(ref.data(), fadeDuration);
    }
}

static void fadeBlurNode(CCNode* blur, bool hide, float duration) {
    if (!blur) return;
    GLubyte target = hide ? 0 : 255;
    float dur = std::max(0.f, duration);

    // Fade opacity; PaiblurNode derives blur radius from it. Avoid dynamic_cast.
    if (auto* rgba = typeinfo_cast<CCNodeRGBA*>(blur)) {
        rgba->stopAllActions();
        if (dur <= 0.01f) {
            rgba->setOpacity(target);
            rgba->setVisible(true); // keep in tree; opacity alone drives draw
            return;
        }
        rgba->setVisible(true);
        rgba->runAction(CCFadeTo::create(dur, target));
        return;
    }

    blur->setVisible(!hide);
}

void setLivePreviewMode(CCNode* popup, bool active, float duration) {
    if (!popup) return;

    auto& reg = blurRegistry();
    auto it = reg.find(popup);
    if (it != reg.end() && !it->second.fadingOut) {
        if (CCNode* blur = liveBlur(it->second)) {
            if (parentAlive(it->second) && blur->getParent()) {
                fadeBlurNode(blur, active, duration);
            }
        }
    }

    // Also hunt sibling/scene children to cover reparent races and external keys.
    auto fadeMatching = [&](CCNode* parent) {
        if (!parent) return;
        auto* kids = parent->getChildren();
        if (!kids) return;
        for (auto* child : CCArrayExt<CCNode*>(kids)) {
            if (!child) continue;
            auto id = std::string(child->getID());
            if (id.find("paiblur") != std::string::npos ||
                id.find("popup-blur") != std::string::npos) {
                fadeBlurNode(child, active, duration);
            }
        }
    };

    fadeMatching(popup->getParent());
    if (auto* dir = CCDirector::get()) {
        fadeMatching(dir->getRunningScene());
    }
}

}
