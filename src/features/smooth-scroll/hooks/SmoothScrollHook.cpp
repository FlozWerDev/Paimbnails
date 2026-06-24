#include "../services/SmoothScrollController.hpp"
#include <Geode/Geode.hpp>
#include <Geode/modify/CCMouseDispatcher.hpp>

using namespace geode::prelude;

namespace {
    bool s_tickScheduled = false;
}

class $modify(PaimonSmoothScrollDispatcher, CCMouseDispatcher) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("cocos2d::CCMouseDispatcher::dispatchScrollMSG", geode::Priority::Early);
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

        if (!ctrl.isActive() || ctrl.isReplaying()) {
            return CCMouseDispatcher::dispatchScrollMSG(y, x);
        }

        if (paimon::smoothscroll::shouldBypassSmoothScroll() || m_pMouseHandlers->count() == 0) {
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

#if defined(GEODE_IS_WINDOWS)
#include <Geode/modify/CCEGLView.hpp>
#include <Geode/cocos/CCDirector.h>

// NOTA: el nombre de la clase $modify DEBE ser unico en todo el binario. Antes
// se llamaba "CaptureView", colisionando con el $modify(CaptureView, CCEGLView)
// de src/hooks/CCEGLView.cpp (ODR violation): el enlazador se quedaba con una
// sola definicion y descartaba la otra, dejando sin aplicar el hook de
// swapBuffers (ejecutor de capturas). Renombrado a SmoothScrollEGLView para que
// ambos $modify coexistan (Geode los combina por nombre unico).
class $modify(SmoothScrollEGLView, CCEGLView) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("cocos2d::CCEGLView::onGLFWMouseScrollCallback", geode::Priority::Late);
    }

    void onGLFWMouseScrollCallback(GLFWwindow* window, double x, double y) {
        auto& ctrl = paimon::smoothscroll::SmoothScrollController::get();
        if (!ctrl.isActive()) {
            CCEGLView::onGLFWMouseScrollCallback(window, x, y);
            return;
        }

        constexpr double kWinWheelScale = 5.0;
        if (auto* director = CCDirector::get()) {
            if (auto* mouse = director->getMouseDispatcher()) {
                mouse->dispatchScrollMSG(
                    static_cast<float>(-y * kWinWheelScale),
                    static_cast<float>(x * kWinWheelScale)
                );
                return;
            }
        }

        CCEGLView::onGLFWMouseScrollCallback(window, x, y);
    }
};
#endif