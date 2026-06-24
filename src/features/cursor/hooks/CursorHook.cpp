#include "CursorHook.hpp"
#include "../services/CursorManager.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/Keyboard.hpp>

using namespace geode::prelude;
using namespace cocos2d;

// CursorTickerNode: dedicated CCNode that drives CursorManager::update()
// every frame via scheduleUpdateForTarget. Mirrors PetTickerNode pattern.
// Direct hook to CCScheduler::update is explicitly discouraged by Geode.

class CursorTickerNode : public CCNode {
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

        if (!cm.config().enabled) {
            if (cm.isAttached()) cm.detachFromScene();
            return;
        }

        // The cursor host lives in the global OverlayManager: attach once and
        // it persists across scenes/transitions (no re-parenting, no Z-order
        // fights). Per-frame visibility — scene filter, gameplay, window
        // bounds — is decided inside CursorManager::update().
        if (!cm.isAttached()) {
            cm.attachToOverlay();
        }
        cm.update(dt);
    }
};

// Ref<> keeps the node alive so the scheduler never releases it prematurely
static Ref<CursorTickerNode> s_cursorTicker = nullptr;
static bool s_mouseListenerRegistered = false;

void initCursorTicker() {
    if (s_cursorTicker) return;
    s_cursorTicker = CursorTickerNode::create();
    // Register directly with the global scheduler (paused=false) so the node
    // keeps ticking even when it is not part of a running scene.
    CCDirector::get()->getScheduler()->scheduleUpdateForTarget(
        s_cursorTicker.data(), 0, false
    );

    // Global left-click hold tracking drives the Click cursor state (inspired by
    // Ecuet/Custom-Cursor). A single leaked listener is fine: it mirrors only a
    // bool into CursorManager and lives for the whole session.
    if (!s_mouseListenerRegistered) {
        s_mouseListenerRegistered = true;
        MouseInputEvent().listen(+[](MouseInputData& data) {
            if (data.button == MouseInputData::Button::Left) {
                CursorManager::get().setMouseDown(
                    data.action == MouseInputData::Action::Press);
            }
            return ListenerResult::Propagate;
        }).leak();
    }
}

void shutdownCursorTicker() {
    if (!s_cursorTicker) return;
    if (auto* director = CCDirector::get()) {
        if (auto* scheduler = director->getScheduler()) {
            scheduler->unscheduleUpdateForTarget(s_cursorTicker.data());
        }
    }
    (void)s_cursorTicker.take();
}

$on_game(Exiting) {
    shutdownCursorTicker();
}
