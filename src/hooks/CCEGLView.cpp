#include <Geode/modify/CCEGLView.hpp>
#include <Geode/loader/Log.hpp>
#include "../features/capture/services/FramebufferCapture.hpp"
#include "../features/pet/services/PetManager.hpp"
#include "../blur/BlurSystem.hpp"

#ifdef GEODE_IS_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../features/capture/ui/CaptureOverlay.hpp"
#include <Geode/binding/PlayLayer.hpp>

static WNDPROC s_originalWndProc = nullptr;

static LRESULT CALLBACK customWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_RBUTTONDOWN) {
        auto* pl = PlayLayer::get();
        if (!pl || pl->m_isPaused) {
            geode::Loader::get()->queueInMainThread([]() {
                CaptureOverlay::show();
            });
        }
    }
    return CallWindowProcW(s_originalWndProc, hwnd, uMsg, wParam, lParam);
}
#endif

using namespace geode::prelude;

class $modify(CaptureView, CCEGLView) {
    static void onModify(auto& self) {
        // Capturamos el back buffer justo antes del swap. Usamos VeryLate
        // (no Last) para no monopolizar el slot terminal de la cadena —
        // otros mods que tambien capturen frame podrian necesitar Last
        // legitimamente (rare, pero posible).
        (void)self.setHookPriorityPre("cocos2d::CCEGLView::swapBuffers", geode::Priority::VeryLate);
    }

    void swapBuffers() {
#ifdef GEODE_IS_WINDOWS
        static bool s_wndProcHooked = false;
        if (!s_wndProcHooked) {
            s_wndProcHooked = true;
            HWND hwnd = GetActiveWindow();
            if (!hwnd) {
                hwnd = FindWindowA(nullptr, "Geometry Dash");
            }
            if (hwnd) {
                s_originalWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
                    hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(customWndProc)
                ));
            }
        }
#endif

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
