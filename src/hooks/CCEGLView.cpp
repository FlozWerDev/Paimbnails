#include <Geode/Geode.hpp>
#include <Geode/modify/CCEGLView.hpp>
#include <Geode/modify/CCEGLViewProtocol.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/utils/Keyboard.hpp>
#include <atomic>
#include "../features/capture/services/FramebufferCapture.hpp"
#include "../features/capture/ui/CaptureMenuPopup.hpp"
#if defined(GEODE_IS_DESKTOP)
#include "../features/cursor/services/CursorManager.hpp"
#endif
#include "../features/editor-colorpicker/ui/ColorPickerOverlay.hpp"
#include "../features/pet/services/PetManager.hpp"
#include "../features/quick-hub/services/QuickHubButtonCapture.hpp"
#include "../features/rtx/services/RTXRenderer.hpp"
#include "../blur/BlurSystem.hpp"
#include "../core/RuntimeLifecycle.hpp"
#include "../framework/HookConventions.hpp"
#include "../utils/ExtendedKeybind.hpp"
#include "../utils/FramePerf.hpp"

using namespace geode::prelude;

namespace {

bool hasCustomCaptureMenuBind() {
    auto* mod = Mod::get();
    if (mod && mod->hasSetting("capture-menu-keybind")) {
        if (auto setting = cast::typeinfo_pointer_cast<KeybindSettingV3>(
                mod->getSetting("capture-menu-keybind"))) {
            for (auto const& kb : setting->getValue()) {
                if (kb.key != cocos2d::KEY_None || kb.modifiers != KeyboardModifier::None) {
                    return true;
                }
            }
        }
    }
    return !paimon::keybinds::loadExtendedKeybind("capture-menu-keybind").isEmpty();
}

} // namespace

// mouseinputevent keeps right-click handling cross-platform.

// leak the global listener for the session; geode frees it on unload.
$execute {
    MouseInputEvent().listen(+[](MouseInputData& data) -> bool {
        if (paimon::keybinds::isRecordingKeybind()) return false;
#if defined(GEODE_IS_DESKTOP)
        if (data.button == MouseInputData::Button::Left &&
            data.action == MouseInputData::Action::Press) {
            if (PetManager::get().config().enabled &&
                PetManager::get().config().enableClickInteraction) {
                PetManager::get().registerClick(geode::cocos::getMousePos());
            }
            return false;
        }
#endif
        if (data.button != MouseInputData::Button::Right) return false;

        bool const isPress = (data.action == MouseInputData::Action::Press);

        auto* pl = PlayLayer::get();
        bool const playing = pl && !pl->m_isPaused;

        // during play, right-click is jump; consume it after handling.
        if (playing && Mod::get()->getSavedValue<bool>("invert-mouse-inputs", false)) {
            pl->handleButton(isPress, 1, true);
            return true;
        }

        // alt+right-click stays with the editor.
        if (isPress && !(data.modifiers & KeyboardModifier::Alt)
            && (!pl || pl->m_isPaused)) {
            // quick hub owns clicks over its buttons.
            if (paimon::quickhub::handleQuickButtonRightClick()) return true;

            // Only fall back to default right-click when the user has not
            // bound a custom trigger to capture-menu-keybind.
            if (!hasCustomCaptureMenuBind()
                && Mod::get()->hasSetting("capture-menu-rightclick")
                && Mod::get()->getSettingValue<bool>("capture-menu-rightclick")) {
                Loader::get()->queueInMainThread([]() {
                    if (paimon::isRuntimeShuttingDown()) return;
                    CaptureMenuPopup::toggle();
                });
            }
        }

        return false;
    }).leak();
}

$execute {
    KeybindSettingPressedEventV3(Mod::get(), "capture-menu-keybind").listen(
        +[](Keybind const&, bool down, bool repeat, double) -> void {
            if (!down || repeat) return;
            if (paimon::keybinds::isRecordingKeybind()) return;

            auto* pl = PlayLayer::get();
            if (pl && !pl->m_isPaused) return;

            // If the custom trigger is Right Click, keep Alt+RightClick for the editor
            // and let Quick Hub consume clicks over its own buttons.
            auto const ext = paimon::keybinds::loadExtendedKeybind("capture-menu-keybind");
            if (ext.kind == paimon::keybinds::ExtendedKind::Mouse
                && ext.button == paimon::keybinds::MouseButton::Right) {
                if ((paimon::keybinds::currentModifiers().value & KeyboardModifier::Alt)
                    && !(ext.modifiers.value & KeyboardModifier::Alt)) {
                    return;
                }
                if (paimon::quickhub::handleQuickButtonRightClick()) return;
            }

            Loader::get()->queueInMainThread([]() {
                if (paimon::isRuntimeShuttingDown()) return;
                CaptureMenuPopup::toggle();
            });
        }
    ).leak();
}

// these hooks bind through the vtable / exported symbols, so they need no
// .bro address; the geode log confirms them live on win/android. other
// platforms present frames through different classes.
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_ANDROID)

class $modify(CaptureView, CCEGLView) {
    static void onModify(auto& self) {
        // capture before swap; leave last available to other frame-capture mods.
        (void)self.setHookPriorityPre("cocos2d::CCEGLView::swapBuffers", geode::Priority::VeryLate);
    }

    void swapBuffers() {
        if (paimon::isRuntimeShuttingDown()) {
            CCEGLView::swapBuffers();
            return;
        }
        {
        paimon::frameperf::Scope perf(paimon::frameperf::Stage::Capture);
        if (FramebufferCapture::hasPendingCapture()) {
            log::debug("[CaptureView] Executing capture in swapBuffers (back buffer)");
            FramebufferCapture::executeIfPending();
        } else {
            // sample before drawing the cursor so the picker never sees it.
            paimon::editorcp::ColorPickerOverlay::onPreSwapSample();
        }
        }

        // post-processing shaders run before the cursor overlay so the custom cursor is not blurred or ray-traced.
        {
            paimon::frameperf::Scope perf(paimon::frameperf::Stage::RTX);
            paimon::rtx::RTXRenderer::get().renderFrame();
        }

        // cursor last: visible but out of screenshots (desktop only)
#if defined(GEODE_IS_DESKTOP)
        {
            paimon::frameperf::Scope perf(paimon::frameperf::Stage::Cursor);
            CursorManager::get().renderOverlay();
        }
#endif

        {
            paimon::frameperf::Scope perf(paimon::frameperf::Stage::Present);
            CCEGLView::swapBuffers();
        }

        {
            paimon::frameperf::Scope perf(paimon::frameperf::Stage::CaptureCallbacks);
            FramebufferCapture::processDeferredCallbacks();
        }
    }

#ifdef GEODE_IS_WINDOWS
    void setFrameSize(float w, float h) {
        CCEGLView::setFrameSize(w, h);
        if (paimon::isRuntimeShuttingDown()) return;
        BlurSystem::getInstance()->onWindowResized(
            static_cast<int>(w), static_cast<int>(h));
    }
#endif

};

// cceglviewprotocol::handletouchesbegin has addresses on both android and ios.
#endif // defined(geode_is_windows) || defined(geode_is_android)
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_MOBILE)

class $modify(CaptureTouchView, CCEGLViewProtocol) {
    static void onModify(auto& self) {
        paimon::hooks::veryLatePost(self, "cocos2d::CCEGLViewProtocol::handleTouchesBegin");
    }

    void handleTouchesBegin(int num, int ids[], float xs[], float ys[], double timestamp) {
        CCEGLViewProtocol::handleTouchesBegin(num, ids, xs, ys, timestamp);

#if defined(GEODE_IS_MOBILE)
        if (num <= 0 || !ids || !xs || !ys) return;
        if (!PetManager::get().config().enabled ||
            !PetManager::get().config().enableClickInteraction) return;

        auto* dir = CCDirector::get();
        auto* glView = dir ? dir->getOpenGLView() : nullptr;
        auto const win = dir ? dir->getWinSize() : CCSizeZero;
        auto const frame = glView ? glView->getFrameSize() : CCSizeZero;
        if (win.width <= 0.f || win.height <= 0.f || frame.width <= 0.f || frame.height <= 0.f) return;

        for (int i = 0; i < num; ++i) {
            // Convert top-left raw screen pixels into Cocos2d bottom-left design coordinates.
            CCPoint const glPt{
                (xs[i] / frame.width) * win.width,
                ((frame.height - ys[i]) / frame.height) * win.height
            };
            PetManager::get().registerClick(glPt);
        }
#endif
    }

#if defined(GEODE_IS_ANDROID)
    void setFrameSize(float w, float h) {
        CCEGLViewProtocol::setFrameSize(w, h);
        if (paimon::isRuntimeShuttingDown()) return;
        BlurSystem::getInstance()->onWindowResized(
            static_cast<int>(w), static_cast<int>(h));
    }
#endif
};

#endif // defined(geode_is_windows) || defined(geode_is_mobile)
