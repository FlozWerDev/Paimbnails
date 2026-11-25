#include "../services/SmoothScrollController.hpp"
#include <Geode/Geode.hpp>

using namespace geode::prelude;

// desktop-only: on ios dispatchscrollmsg is inlined and there is no wheel.
#if defined(GEODE_IS_DESKTOP)

#include <Geode/modify/CCMouseDispatcher.hpp>
#include <Geode/modify/CCScrollLayerExt.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
    bool s_tickScheduled = false;

    class DeclineScrollDelegate final : public CCMouseDelegate {
    public:
        bool scrollWheel(float, float) override { return false; }
    };

    DeclineScrollDelegate s_declineScroll;

    // the dispatcher walks the delegates from the newest one and stops at the
    // first that accepts. A hidden list (another tab, a page that was swapped
    // out) still accepts, so for one replayed dispatch its handler points at a
    // delegate that declines and the travel reaches the list on screen.
    class ScopedHiddenListMute {
    public:
        explicit ScopedHiddenListMute(CCMouseDispatcher* dispatcher) : m_dispatcher(dispatcher) {
            auto* handlers = dispatcher ? dispatcher->m_pMouseHandlers : nullptr;
            if (!handlers) return;

            for (auto* handler : CCArrayExt<CCMouseHandler*>(handlers)) {
                if (!handler) continue;
                auto* list = paimon::smoothscroll::hiddenScrollList(handler->m_pDelegate);
                if (!list) continue;

                m_muted.push_back({
                    handler, handler->m_pDelegate, list,
                    list->isRunning(), list->isMouseEnabled()
                });
                // written directly: setDelegate would retain the placeholder.
                handler->m_pDelegate = &s_declineScroll;
            }
        }

        ~ScopedHiddenListMute() {
            if (m_muted.empty()) return;
            auto* handlers = m_dispatcher->m_pMouseHandlers;

            for (auto& entry : m_muted) {
                entry.handler->m_pDelegate = entry.delegate;

                // a removal requested during the dispatch was matched against
                // the placeholder and skipped, so it is applied now.
                bool const left = entry.wasRunning && !entry.list->isRunning();
                bool const disabled = entry.wasMouseEnabled && !entry.list->isMouseEnabled();
                if ((left || disabled) && handlers && handlers->containsObject(entry.handler)) {
                    m_dispatcher->forceRemoveDelegate(entry.delegate);
                }
            }
        }

        ScopedHiddenListMute(ScopedHiddenListMute const&) = delete;
        ScopedHiddenListMute& operator=(ScopedHiddenListMute const&) = delete;

    private:
        struct Entry {
            // keeps the handler (and the delegate it retains) alive while muted.
            geode::Ref<CCMouseHandler> handler;
            CCMouseDelegate* delegate;
            CCScrollLayerExt* list;
            bool wasRunning;
            bool wasMouseEnabled;
        };

        CCMouseDispatcher* m_dispatcher;
        std::vector<Entry> m_muted;
    };
}

class $modify(PaimonSmoothScrollDispatcher, CCMouseDispatcher) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPre(
            "cocos2d::CCMouseDispatcher::dispatchScrollMSG",
            geode::Priority::Normal
        );
    }

    void tickSmoothScroll(float dt) {
        auto& ctrl = paimon::smoothscroll::SmoothScrollController::get();
        if (!ctrl.isActive()) {
            this->stopSmoothTick();
            return;
        }

        if (paimon::smoothscroll::shouldBypassSmoothScroll()) {
            this->stopSmoothTick();
            return;
        }

        ctrl.tick(dt, [this](float y, float x) {
            this->dispatchScrollMSG(y, x);
        });

        if (!ctrl.hasMomentum()) {
            this->stopSmoothTick();
        }
    }

    void startSmoothTick() {
        if (s_tickScheduled) return;
        s_tickScheduled = true;
        CCScheduler::get()->scheduleSelector(
            schedule_selector(PaimonSmoothScrollDispatcher::tickSmoothScroll),
            this,
            0.f,
            false
        );
    }

    void stopSmoothTick() {
        if (!s_tickScheduled) {
            paimon::smoothscroll::SmoothScrollController::get().reset();
            return;
        }
        s_tickScheduled = false;
        CCScheduler::get()->unscheduleSelector(
            schedule_selector(PaimonSmoothScrollDispatcher::tickSmoothScroll),
            this
        );
        paimon::smoothscroll::SmoothScrollController::get().reset();
    }

    bool dispatchScrollMSG(float y, float x) {
        auto& ctrl = paimon::smoothscroll::SmoothScrollController::get();

        if (ctrl.isReplaying()) {
            ScopedHiddenListMute mute(this);
            return CCMouseDispatcher::dispatchScrollMSG(y, x);
        }

        if (!ctrl.isActive()) {
            return CCMouseDispatcher::dispatchScrollMSG(y, x);
        }

        if (paimon::smoothscroll::shouldBypassSmoothScroll() ||
            !m_pMouseHandlers || m_pMouseHandlers->count() == 0) {
            this->stopSmoothTick();
            return CCMouseDispatcher::dispatchScrollMSG(y, x);
        }

        if (ctrl.queueInput(y, x)) {
            this->startSmoothTick();
            return false;
        }

        this->stopSmoothTick();
        return CCMouseDispatcher::dispatchScrollMSG(y, x);
    }
};

// GD keeps its own momentum for scroll layers (CCScrollLayerExt accumulates the
// wheel into m_scrollDecay and smooths it again in updateScrollDecay). While the
// mod replays its own momentum that second smoothing stage is what makes some
// zones feel faster or slower than others, so the pending travel is applied
// straight to the content instead of being smoothed twice.
class $modify(PaimonScrollLayerExt, CCScrollLayerExt) {
    void updateScrollDecay(float dt) {
        (void)dt;
        auto& ctrl = paimon::smoothscroll::SmoothScrollController::get();
        if (!ctrl.isDrivingScroll()) {
            CCScrollLayerExt::updateScrollDecay(dt);
            return;
        }

        float const pending = this->m_scrollDecay;
        this->m_scrollDecay = 0.f;

        if (pending == 0.f) {
            if (this->m_scrolling) {
                this->m_scrolling = false;
                this->unschedule(schedule_selector(CCScrollLayerExt::updateScrollDecay));
            }
            return;
        }

        if (this->m_disableVertical) return;

        auto* content = this->m_contentLayer;
        if (!content) return;

        CCPoint pos = content->getPosition();
        pos.y += pending;

        float const minY = this->getMinY();
        float const maxY = this->getMaxY();
        if (pos.y < minY) pos.y = minY;
        else if (pos.y > maxY) pos.y = maxY;

        content->setPosition(pos);
    }
};

// scrollwheel ignores magnitude, so replay feeds this frame's fraction of one step.
namespace {
    bool s_applyingFilteredEditorZoom = false;

    struct ScopedFilteredZoomGuard {
        bool previous;
        ScopedFilteredZoomGuard() : previous(s_applyingFilteredEditorZoom) {
            s_applyingFilteredEditorZoom = true;
        }
        ~ScopedFilteredZoomGuard() {
            s_applyingFilteredEditorZoom = previous;
        }
    };

    bool computeFilteredEditorZoom(EditorUI* ui, float& outZoom) {
        auto& ctrl = paimon::smoothscroll::SmoothScrollController::get();
        if (!ctrl.isEditorZoomReplay() || s_applyingFilteredEditorZoom) {
            return false;
        }

        float const steps = ctrl.replayedZoomSteps();
        if (std::abs(steps) < 0.000001f) {
            outZoom = -1.f;
            return true;
        }

        auto* objectLayer = ui && ui->m_editorLayer ? ui->m_editorLayer->m_objectLayer : nullptr;
        if (!objectLayer) {
            outZoom = -1.f;
            return true;
        }

        constexpr float kEditorZoomPerStep = 0.1f;
        constexpr float kEditorZoomMin = 0.1f;
        constexpr float kEditorZoomMax = 4.f;
        float const current = objectLayer->getScale();
        outZoom = std::clamp(
            current + steps * kEditorZoomPerStep,
            kEditorZoomMin,
            kEditorZoomMax
        );
        return true;
    }
}

class $modify(PaimonFilteredEditorZoom, EditorUI) {
    static void onModify(auto& self) {
        // normalize the requested zoom before later hooks observe it.
        (void)self.setHookPriorityPre("EditorUI::updateZoom", geode::Priority::VeryEarly);
    }

    void updateZoom(float zoom) {
        float filtered = 0.f;
        if (!computeFilteredEditorZoom(this, filtered)) {
            EditorUI::updateZoom(zoom);
            return;
        }
        if (filtered < 0.f) return;

        ScopedFilteredZoomGuard guard;
        EditorUI::updateZoom(filtered);
    }
};

// GD 2.2091 added a 2-arg updateZoom(float, CCPoint); kept in a separate $modify
// block because Geode keys m_hooks by "Class::Method" without parameter types.
class $modify(PaimonFilteredEditorZoomPoint, EditorUI) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPre("EditorUI::updateZoom", geode::Priority::VeryEarly);
    }

    void updateZoom(float zoom, cocos2d::CCPoint point) {
        float filtered = 0.f;
        if (!computeFilteredEditorZoom(this, filtered)) {
            EditorUI::updateZoom(zoom, point);
            return;
        }
        if (filtered < 0.f) return;

        ScopedFilteredZoomGuard guard;
        EditorUI::updateZoom(filtered, point);
    }
};

// Safety net when another delegate above EditorUI declines a replayed scroll:
// cancel remaining pixel/spring momentum and emit a single native step instead
// of driving EditorUI::scrollWheel once per frame.
class $modify(PaimonEditorScrollGuard, EditorUI) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPre("EditorUI::scrollWheel", geode::Priority::Late);
    }

    bool scrollWheel(float y, float x) {
        auto& ctrl = paimon::smoothscroll::SmoothScrollController::get();
        if (ctrl.isReplaying() && !ctrl.isEditorZoomReplay()) {
            ctrl.stop();
            auto unitStep = [](float v) -> float {
                if (!std::isfinite(v) || std::abs(v) < 0.0001f) return 0.f;
                float const unit = static_cast<float>(paimon::smoothscroll::kInputUnitsPerStep);
                return v > 0.f ? unit : -unit;
            };
            return EditorUI::scrollWheel(unitStep(y), unitStep(x));
        }
        return EditorUI::scrollWheel(y, x);
    }
};

#endif // geode_is_desktop

#if defined(GEODE_IS_WINDOWS)
#include <Geode/modify/CCEGLView.hpp>
#include <Geode/cocos/CCDirector.h>

// $modify needs a binary-unique name: "captureview" collided with src/hooks/cceglview.cpp (odr).
class $modify(SmoothScrollEGLView, CCEGLView) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPre(
            "cocos2d::CCEGLView::onGLFWMouseScrollCallback",
            geode::Priority::Late
        );
    }

    void onGLFWMouseScrollCallback(GLFWwindow* window, double x, double y) {
        auto& ctrl = paimon::smoothscroll::SmoothScrollController::get();
        if (!ctrl.isActive() || paimon::smoothscroll::shouldBypassSmoothScroll()) {
            CCEGLView::onGLFWMouseScrollCallback(window, x, y);
            return;
        }

        if (auto* director = CCDirector::get()) {
            if (auto* mouse = director->getMouseDispatcher()) {
                mouse->dispatchScrollMSG(
                    static_cast<float>(-y * paimon::smoothscroll::kInputUnitsPerStep),
                    static_cast<float>(x * paimon::smoothscroll::kInputUnitsPerStep)
                );
                return;
            }
        }

        CCEGLView::onGLFWMouseScrollCallback(window, x, y);
    }
};
#endif
