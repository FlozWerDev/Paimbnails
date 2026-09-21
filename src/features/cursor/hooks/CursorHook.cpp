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
// En movil no hay raton: la fuente de apretar/soltar son los toques, enganchados
// en el dispatcher antes de que los botones se traguen el toque.
namespace {
void feedTouch(CCSet* touches, int state) {
    if (!touches) return;
    auto* touch = static_cast<CCTouch*>(touches->anyObject());
    if (!touch) return;

    auto& cm = CursorManager::get();
    cm.setTouchPoint(touch->getLocation());
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

        // El host vive en OverlayManager y persiste entre escenas.
        if (cm.config().enabled) {
            if (!cm.isAttached()) cm.attachToOverlay();
        } else if (cm.isAttached()) {
            cm.detachFromScene();
        }

        // Los efectos de click funcionan aunque el cursor este apagado (en movil no hay cursor).
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

    // Click-hold global para el estado Click y los efectos (idea de Ecuet's "Custom Cursor");
    // listener de sesion intencional con .leak().
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
