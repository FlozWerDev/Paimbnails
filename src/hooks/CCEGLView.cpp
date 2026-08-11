#include <Geode/Geode.hpp>
#include <Geode/modify/CCEGLView.hpp>
#include <Geode/modify/CCEGLViewProtocol.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/utils/Keyboard.hpp>
#include <atomic>
#include "../features/capture/services/FramebufferCapture.hpp"
#include "../features/capture/ui/CaptureMenuPopup.hpp"
#include "../features/cursor/services/CursorManager.hpp"
#include "../features/editor-colorpicker/ui/ColorPickerOverlay.hpp"
#include "../features/pet/services/PetManager.hpp"
#include "../features/quick-hub/services/QuickHubButtonCapture.hpp"
#include "../blur/BlurSystem.hpp"
#include "../core/RuntimeLifecycle.hpp"

using namespace geode::prelude;

// MouseInputEvent keeps right-click handling cross-platform.

// Leak the global listener for the session; Geode frees it on unload.
$execute {
    MouseInputEvent().listen(+[](MouseInputData& data) -> bool {
        if (data.button != MouseInputData::Button::Right) return false;

        bool const isPress = (data.action == MouseInputData::Action::Press);

        auto* pl = PlayLayer::get();
        bool const playing = pl && !pl->m_isPaused;

        // During play, right-click is jump; consume it after handling.
        if (playing && Mod::get()->getSavedValue<bool>("invert-mouse-inputs", false)) {
            pl->handleButton(isPress, 1, true);
            return true;
        }

        // Outside play, right-click opens the capture menu; Alt+right-click stays with the editor.
        if (isPress && !(data.modifiers & KeyboardModifier::Alt)
            && (!pl || pl->m_isPaused)
            && Mod::get()->getSettingValue<bool>("capture-menu-rightclick")) {
            // Quick Hub owns clicks over its buttons.
            if (paimon::quickhub::handleQuickButtonRightClick()) return true;

            Loader::get()->queueInMainThread([]() {
                if (paimon::isRuntimeShuttingDown()) return;
                CaptureMenuPopup::toggle();
            });
        }

        return false;
    }).leak();
}

// The keybind mirrors right-click and is disabled during active play.
$execute {
    KeybindSettingPressedEventV3(Mod::get(), "capture-menu-keybind").listen(
        +[](Keybind const&, bool down, bool repeat, double) -> void {
            if (!down || repeat) return;

            auto* pl = PlayLayer::get();
            if (pl && !pl->m_isPaused) return;

            Loader::get()->queueInMainThread([]() {
                if (paimon::isRuntimeShuttingDown()) return;
                CaptureMenuPopup::toggle();
            });
        }
    ).leak();
}

// This hook exists only on Windows/Android; macOS/iOS use different view classes.
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_ANDROID)

class $modify(CaptureView, CCEGLView) {
    static void onModify(auto& self) {
        // Capture before swap; leave Last available to other frame-capture mods.
        (void)self.setHookPriorityPre("cocos2d::CCEGLView::swapBuffers", geode::Priority::VeryLate);
    }

    void swapBuffers() {
        if (FramebufferCapture::hasPendingCapture()) {
            log::debug("[CaptureView] Executing capture in swapBuffers (back buffer)");
            FramebufferCapture::executeIfPending();
        } else {
            // Sample before drawing the cursor so the picker never sees it.
            paimon::editorcp::ColorPickerOverlay::onPreSwapSample();
        }

        // Revisit the cursor after ImGui and after capture so it stays visible but screenshots remain cursor-free.
        CursorManager::get().renderOverlay();

        CCEGLView::swapBuffers();

        FramebufferCapture::processDeferredCallbacks();
    }

    void setFrameSize(float w, float h) {
        CCEGLView::setFrameSize(w, h);
        BlurSystem::getInstance()->onWindowResized(
            static_cast<int>(w), static_cast<int>(h));
    }

};

// handleTouchesBegin is declared on CCEGLViewProtocol, so it needs a separate $modify.
class $modify(CaptureTouchView, CCEGLViewProtocol) {
    void handleTouchesBegin(int num, int ids[], float xs[], float ys[], double timestamp) {
        CCEGLViewProtocol::handleTouchesBegin(num, ids, xs, ys, timestamp);

        if (!PetManager::get().config().enableClickInteraction) {
            return;
        }

        for (int i = 0; i < num; ++i) {
            PetManager::get().registerClick({xs[i], ys[i]});
        }
    }
};

#endif // defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_ANDROID)
