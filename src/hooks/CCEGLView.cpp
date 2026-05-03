#include <Geode/modify/CCEGLView.hpp>
#include <Geode/loader/Log.hpp>
#include "../features/capture/services/FramebufferCapture.hpp"
#include "../features/pet/services/PetManager.hpp"
#include "../blur/BlurSystem.hpp"

using namespace geode::prelude;

class $modify(CaptureView, CCEGLView) {
    static void onModify(auto& self) {
        // Hook de ultima prioridad para capturar el back buffer
        (void)self.setHookPriorityPre("cocos2d::CCEGLView::swapBuffers", geode::Priority::Last);
    }

    void swapBuffers() {
        // Captura antes del swap
        if (FramebufferCapture::hasPendingCapture()) {
            log::debug("[CaptureView] Executing capture in swapBuffers (back buffer)");
            FramebufferCapture::executeIfPending();
        }

        CCEGLView::swapBuffers();

        FramebufferCapture::processDeferredCallbacks();
    }

    void setFrameSize(float w, float h) {
        CCEGLView::setFrameSize(w, h);
        // Invalida FBOs de blur al redimensionar
        BlurSystem::getInstance()->onWindowResized(
            static_cast<int>(w), static_cast<int>(h));
    }

    void handleTouchesBegin(int num, int ids[], float xs[], float ys[], double timestamp) {
        CCEGLView::handleTouchesBegin(num, ids, xs, ys, timestamp);

        if (!PetManager::get().config().enableClickInteraction) {
            return;
        }

        for (int i = 0; i < num; ++i) {
            PetManager::get().registerClick({xs[i], ys[i]});
        }
    }
};
