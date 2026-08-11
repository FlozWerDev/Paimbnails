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
// En movil no hay raton ni MouseInputEvent: la fuente de "apretar y soltar" son
// los toques. Se engancha el dispatcher en su punto de entrada, antes de que
// cualquier boton se trague el toque, para que el efecto salga en toda la
// pantalla y no solo en los huecos vacios. Solo se lee la posicion y se avisa a
// CursorManager; el toque sigue su camino intacto.
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

        // The cursor host lives in the global OverlayManager: attach once and
        // it persists across scenes/transitions (no re-parenting, no Z-order
        // fights). Per-frame visibility — scene filter, gameplay, window
        // bounds — is decided inside CursorManager::update().
        if (cm.config().enabled) {
            if (!cm.isAttached()) cm.attachToOverlay();
        } else if (cm.isAttached()) {
            cm.detachFromScene();
        }

        // Siempre: los efectos de click tienen su propio nodo y funcionan aunque
        // el cursor personalizado este apagado (en movil no hay cursor). Si todo
        // esta apagado, update() sale en la primera comprobacion.
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

    // Global click hold tracking drives the Click cursor state (inspired by
    // Ecuet/Custom-Cursor) and the click effects. A single leaked listener is
    // fine: it mirrors only two bools into CursorManager and lives for the whole
    // session.
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
