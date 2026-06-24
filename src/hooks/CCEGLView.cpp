#include <Geode/Geode.hpp>
#include <Geode/modify/CCEGLView.hpp>
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

// Right-click toggles the capture overlay. Uses Geode's MouseInputEvent
// (cross-platform) instead of subclassing the window's WndProc.

// Register the global right-click listener once. .leak() keeps the subscription
// alive for the session; Geode frees it on unload (hot-reload included).
$execute {
    MouseInputEvent().listen(+[](MouseInputData& data) -> bool {
        if (data.button != MouseInputData::Button::Right) return false;

        bool const isPress = (data.action == MouseInputData::Action::Press);

        auto* pl = PlayLayer::get();
        bool const playing = pl && !pl->m_isPaused;

        // "Invert inputs": during active play right-click acts as jump
        // (button 1) and we consume the event so GD doesn't reprocess it.
        if (playing && Mod::get()->getSavedValue<bool>("invert-mouse-inputs", false)) {
            pl->handleButton(isPress, 1, true);
            return true; // consumed
        }

        // Default: right-click toggles the capture menu popup when not playing
        // (menus/pause), on Press only. The popup uses the mod's popup template
        // (geode::Popup) for visual consistency with the rest of the mod.
        if (isPress && (!pl || pl->m_isPaused)) {
            // A button under the cursor belongs to Quick Hub capture. Consume
            // the click so the generic capture menu is not opened too.
            if (paimon::quickhub::handleQuickButtonRightClick()) return true;

            Loader::get()->queueInMainThread([]() {
                if (paimon::isRuntimeShuttingDown()) return;
                CaptureMenuPopup::toggle();
            });
        }

        return false;
    }).leak();
}

// cocos2d::CCEGLView is only linked on Windows and Android; mac/iOS use
// different classes (CCEAGLView, CCEGLView_mac). Pet click tracking and blur
// FBO invalidation done here are therefore Windows/Android only.
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_ANDROID)

class $modify(CaptureView, CCEGLView) {
    static void onModify(auto& self) {
        // Capture the back buffer just before swap. VeryLate (not Last) so
        // other frame-capturing mods can still claim Last legitimately.
        (void)self.setHookPriorityPre("cocos2d::CCEGLView::swapBuffers", geode::Priority::VeryLate);
    }

    void swapBuffers() {
        if (FramebufferCapture::hasPendingCapture()) {
            log::debug("[CaptureView] Executing capture in swapBuffers (back buffer)");
            FramebufferCapture::executeIfPending();
        } else {
            // Real-time editor color picker: sample a small region of the
            // freshly rendered back-buffer around the cursor. Done here (before
            // the custom cursor is drawn) so the cursor sprite never tints the
            // picked pixel, and skipped while a framebuffer capture is in flight.
            paimon::editorcp::ColorPickerOverlay::onPreSwapSample();
        }

        // Render the custom cursor on top of any ImGui-based menu (e.g.
        // EclipseMenu via gd-imgui-cocos). imgui-cocos hooks this same
        // function with Priority::Early/default, so by the time our VeryLate
        // hook runs, ImGui has already painted its frame on top of cocos2d.
        // Re-visiting the cursor node here puts it back on top right before
        // the GL buffer swap. Done AFTER the framebuffer capture call above
        // so screenshots stay cursor-free, which matches OS-cursor behavior.
        CursorManager::get().renderOverlay();

        CCEGLView::swapBuffers();

        FramebufferCapture::processDeferredCallbacks();
    }

    void setFrameSize(float w, float h) {
        CCEGLView::setFrameSize(w, h);
        // Invalidate blur FBOs on resize
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
