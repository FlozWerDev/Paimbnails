#include "CursorHook.hpp"
#if defined(GEODE_IS_DESKTOP)
#include "../services/CursorManager.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/Keyboard.hpp>

using namespace geode::prelude;
using namespace cocos2d;

// avoids hooking ccscheduler::update directly, which geode discourages.
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

        // overlay host outlives scenes.
        if (cm.config().enabled) {
            if (!cm.isAttached()) cm.attachToOverlay();
        } else if (cm.isAttached()) {
            cm.detachFromScene();
        }

        cm.update(dt);
    }
};

// ref<> keeps the node alive so the scheduler never releases it prematurely
static Ref<CursorTickerNode> s_cursorTicker = nullptr;
static bool s_mouseListenerRegistered = false;

void initCursorTicker() {
    if (s_cursorTicker) return;
    s_cursorTicker = CursorTickerNode::create();
    // global scheduler (paused=false) so the node ticks outside a running scene.
    CCDirector::get()->getScheduler()->scheduleUpdateForTarget(
        s_cursorTicker.data(), 0, false
    );

    // global click-hold for click state/effects (ecuet's custom cursor idea); intentional session .leak().
    if (!s_mouseListenerRegistered) {
        s_mouseListenerRegistered = true;
        MouseInputEvent().listen(+[](MouseInputData& data) {
            bool pressed = data.action == MouseInputData::Action::Press;
            if (data.button == MouseInputData::Button::Left) {
                CursorManager::get().setMouseDown(pressed);
            } else if (data.button == MouseInputData::Button::Right) {
                CursorManager::get().setSecondaryMouseDown(pressed);
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

// Geode 6 clears OverlayManager when textures unload; attach again right away
// instead of waiting for the next ticker update.
$on_game(TexturesLoaded) {
    auto& cm = CursorManager::get();
    if (cm.config().enabled && !cm.isAttached()) cm.attachToOverlay();
}

$on_game(Exiting) {
    shutdownCursorTicker();
}
#else
void initCursorTicker() {}
void shutdownCursorTicker() {}
#endif
