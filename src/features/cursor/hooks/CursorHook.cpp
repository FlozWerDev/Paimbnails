#include "CursorHook.hpp"
#include "../services/CursorManager.hpp"
#include <Geode/Geode.hpp>

using namespace geode::prelude;
using namespace cocos2d;

// ──────────────────────────────────────────────────────────────────────────
// CursorTickerNode: dedicated CCNode that drives CursorManager::update()
// every frame via scheduleUpdateForTarget. Mirrors PetTickerNode pattern.
// Direct hook to CCScheduler::update is explicitly discouraged by Geode.
// ──────────────────────────────────────────────────────────────────────────

class CursorTickerNode : public CCNode {
    int m_frameCounter = 0;
    CCScene* m_lastScene = nullptr;

public:
    static CursorTickerNode* create() {
        auto ret = new CursorTickerNode();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    bool init() override {
        if (!CCNode::init()) return false;
        this->setID("paimon-cursor-ticker"_spr);
        return true;
    }

    void update(float dt) override {
        auto& cm = CursorManager::get();

        // Run the per-frame cursor update while the feature is attached
        if (cm.isAttached()) {
            cm.update(dt);
        }

        if (!cm.config().enabled) {
            if (cm.isAttached()) cm.detachFromScene();
            return;
        }

        auto scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene) {
            if (cm.isAttached()) cm.detachFromScene();
            m_lastScene = nullptr;
            return;
        }

        bool sceneChanged = scene != m_lastScene;
        m_lastScene = scene;

        // Reevaluate immediately on scene changes and poll more often the rest of the time.
        if (!sceneChanged && cm.isAttachedToScene(scene)) {
            if (++m_frameCounter % 2 != 0) return;
        } else {
            m_frameCounter = 0;
        }

        if (!cm.shouldShowOnCurrentScene()) {
            if (cm.isAttached()) cm.detachFromScene();
            return;
        }

        if (!cm.isAttachedToScene(scene)) {
            cm.attachToScene(scene);
        }
    }
};

// Ref<> keeps the node alive so the scheduler never releases it prematurely
static Ref<CursorTickerNode> s_cursorTicker = nullptr;

void initCursorTicker() {
    if (s_cursorTicker) return;
    s_cursorTicker = CursorTickerNode::create();
    // Register directly with the global scheduler (paused=false) so the node
    // keeps ticking even when it is not part of a running scene.
    CCDirector::sharedDirector()->getScheduler()->scheduleUpdateForTarget(
        s_cursorTicker.data(), 0, false
    );
}

void shutdownCursorTicker() {
    if (!s_cursorTicker) return;
    if (auto* director = CCDirector::sharedDirector()) {
        if (auto* scheduler = director->getScheduler()) {
            scheduler->unscheduleUpdateForTarget(s_cursorTicker.data());
        }
    }
    (void)s_cursorTicker.take();
}

$on_game(Exiting) {
    shutdownCursorTicker();
}
