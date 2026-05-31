#include <Geode/modify/CCEGLView.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/loader/GameEvent.hpp>
#include <atomic>
#include "../features/capture/services/FramebufferCapture.hpp"
#include "../features/pet/services/PetManager.hpp"
#include "../blur/BlurSystem.hpp"

#ifdef GEODE_IS_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../features/capture/ui/CaptureOverlay.hpp"
#include <Geode/binding/PlayLayer.hpp>

static WNDPROC s_originalWndProc = nullptr;
// Atomic flag — swapBuffers puede ejecutarse en multiples threads en algunos
// drivers, y el hot-reload de Geode tambien necesita ver un valor coherente.
static std::atomic<bool> s_wndProcHooked{false};
static HWND s_hookedHwnd = nullptr;

static LRESULT CALLBACK customWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_RBUTTONDOWN || uMsg == WM_RBUTTONUP) {
        auto* pl = PlayLayer::get();
        bool const playing = pl && !pl->m_isPaused;

        // "Invertir Inputs": para quienes usan el clic derecho como boton
        // principal. Durante el juego activo, el clic derecho actua como salto
        // (boton 1) y consumimos el mensaje para que GD/GLFW no lo reprocesen.
        if (playing && geode::Mod::get()->getSavedValue<bool>("invert-mouse-inputs", false)) {
            pl->handleButton(uMsg == WM_RBUTTONDOWN, 1, true);
            return 0;
        }

        // Comportamiento por defecto: el clic derecho abre el overlay de captura
        // cuando no se esta jugando (menus / pausa).
        if (uMsg == WM_RBUTTONDOWN && (!pl || pl->m_isPaused)) {
            geode::Loader::get()->queueInMainThread([]() {
                CaptureOverlay::show();
            });
        }
    }
    return CallWindowProcW(s_originalWndProc, hwnd, uMsg, wParam, lParam);
}
#endif

using namespace geode::prelude;

#ifdef GEODE_IS_WINDOWS
// Restaura la WndProc original al cerrar el juego. Sin esto, si Geode
// descarga el DLL del mod (hot-reload), customWndProc apunta a memoria
// liberada y el siguiente WM_RBUTTONDOWN crashea el juego.
$on_game(Exiting) {
    if (s_wndProcHooked.exchange(false, std::memory_order_acq_rel)) {
        if (s_hookedHwnd && s_originalWndProc) {
            SetWindowLongPtrW(
                s_hookedHwnd, GWLP_WNDPROC,
                reinterpret_cast<LONG_PTR>(s_originalWndProc)
            );
        }
        s_originalWndProc = nullptr;
        s_hookedHwnd = nullptr;
    }
}
#endif

// cocos2d::CCEGLView solo esta linkeado en Windows y Android (ver Cocos2d.bro:
// "[[link(win, android)]] class cocos2d::CCEGLView"). En macOS/iOS las clases
// equivalentes son distintas (CCEAGLView en iOS, CCEGLView_mac propio), asi que
// hookear cocos2d::CCEGLView ahi produciria fallos de link y/o NOOPs silenciosos.
// Mantenemos el hook activo solo en las plataformas donde el binding existe.
//
// Consecuencia para PetManager y BlurSystem: en mac/iOS las features de
//   - registro de clicks via handleTouchesBegin
//   - invalidacion de FBOs en setFrameSize
// no se enganchan aqui. Si esas features se necesitan multiplataforma, se
// debe anadir un hook alternativo (p.ej. CCDirector::reshapeProjection o un
// CCNode global) protegido por su propio guard de plataforma.
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_ANDROID)

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
        // Hook lazy: solo en la primera invocacion exitosa, atomicamente.
        bool expected = false;
        if (s_wndProcHooked.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            HWND hwnd = GetActiveWindow();
            if (!hwnd) {
                hwnd = FindWindowA(nullptr, "Geometry Dash");
            }
            if (hwnd) {
                auto prev = SetWindowLongPtrW(
                    hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(customWndProc)
                );
                s_originalWndProc = reinterpret_cast<WNDPROC>(prev);
                s_hookedHwnd = hwnd;
            } else {
                // No conseguimos handle; revertir el flag para reintentar
                // en la proxima invocacion.
                s_wndProcHooked.store(false, std::memory_order_release);
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

#endif // defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_ANDROID)
