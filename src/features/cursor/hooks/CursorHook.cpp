#include "CursorHook.hpp"
#include "../services/CursorManager.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/Keyboard.hpp>

#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
#include <Geode/modify/CCTouchDispatcher.hpp>
#endif

using namespace geode::prelude;
using namespace cocos2d;

#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
// No mouse on mobile: press/release comes from touches, hooked before buttons swallow them.
namespace {
void feedTouch(CCSet* touches, int state) {
    if (!touches) return;

    auto& cm = CursorManager::get();
    for (auto it = touches->begin(); it != touches->end(); ++it) {
        if (auto* touch = static_cast<CCTouch*>(*it)) {
            cm.setTouchPoint(touch->getLocation());
        }
    }
    if (state >= 0) cm.setMouseDown(state != 0);
}
} // namespace

class $modify(PaimonClickTouchDispatcher, CCTouchDispatcher) {
    $override
    void touchesBegan(CCSet* touches, CCEvent* event) {
        feedTouch(touches, 1);
        CCTouchDispatcher::touchesBegan(touches, event);
    }
    $override
    void touchesMoved(CCSet* touches, CCEvent* event) {
        feedTouch(touches, -1);
        CCTouchDispatcher::touchesMoved(touches, event);
    }
    $override
    void touchesEnded(CCSet* touches, CCEvent* event) {
        feedTouch(touches, 0);
        CCTouchDispatcher::touchesEnded(touches, event);
    }
    $override
    void touchesCancelled(CCSet* touches, CCEvent* event) {
        feedTouch(touches, 0);
        CCTouchDispatcher::touchesCancelled(touches, event);
    }
};
#endif

// Avoids hooking CCScheduler::update directly, which Geode discourages.
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

        // Overlay host outlives scenes.
        if (cm.config().enabled) {
            if (!cm.isAttached()) cm.attachToOverlay();
        } else if (cm.isAttached()) {
            cm.detachFromScene();
        }

        // Click FX run with the cursor off (mobile has none).
        cm.update(dt);
    }
};

// Ref<> keeps the node alive so the scheduler never releases it prematurely
static Ref<CursorTickerNode> s_cursorTicker = nullptr;
static bool s_mouseListenerRegistered = false;

void initCursorTicker() {
    if (s_cursorTicker) return;
    s_cursorTicker = CursorTickerNode::create();
    // Global scheduler (paused=false) so the node ticks outside a running scene.
    CCDirector::get()->getScheduler()->scheduleUpdateForTarget(
        s_cursorTicker.data(), 0, false
    );

    // Global click-hold for Click state/effects (Ecuet's Custom Cursor idea); intentional session .leak().
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

$on_game(Exiting) {
    shutdownCursorTicker();
}
