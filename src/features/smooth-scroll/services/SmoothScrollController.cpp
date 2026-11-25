#include "SmoothScrollController.hpp"
#include "../../../core/Settings.hpp"
#include "../../../framework/compat/ModCompat.hpp"
#include "../../volume-scroll/services/VolumeScrollManager.hpp"
#include <Geode/Geode.hpp>
#include <Geode/cocos/robtop/keyboard_dispatcher/CCKeyboardDispatcher.h>
#include <Geode/cocos/robtop/mouse_dispatcher/CCMouseDispatcher.h>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace paimon::smoothscroll {

namespace {
    // user-scaled base values: pixels = base * sensitivity, response = base / smoothness
    constexpr double kBaseResponse = 12.0;
    constexpr double kMaxActionSteps = 8.0;

    double boundedSetting(double value, double minimum, double maximum, double fallback) {
        if (!std::isfinite(value)) return fallback;
        return std::clamp(value, minimum, maximum);
    }

    void const* currentScrollTarget() {
        auto* director = CCDirector::get();
        auto* dispatcher = director ? director->getMouseDispatcher() : nullptr;
        auto* handlers = dispatcher ? dispatcher->m_pMouseHandlers : nullptr;
        if (!handlers) return nullptr;

        // the dispatcher visits the newest delegate first (count - 1 down to zero).
        // forward traversal can mistake the editor's background for the zoom recipient.
        // hidden lists are muted during the replay, so they are not the recipient either.
        for (auto i = handlers->count(); i > 0; --i) {
            auto* handler = static_cast<CCMouseHandler*>(handlers->objectAtIndex(i - 1));
            if (!handler || !handler->m_pDelegate) continue;
            if (hiddenScrollList(handler->m_pDelegate)) continue;
            return handler->m_pDelegate;
        }
        return nullptr;
    }

    bool isEditorUIActiveTarget() {
        auto* editorLayer = LevelEditorLayer::get();
        if (!editorLayer || !editorLayer->m_editorUI) return false;
        auto* editorTarget = static_cast<CCMouseDelegate*>(editorLayer->m_editorUI);
        return currentScrollTarget() == editorTarget;
    }

    float signUnit(float value) {
        if (!std::isfinite(value) || std::abs(value) < 0.0001f) return 0.f;
        return value > 0.f ? 1.f : -1.f;
    }

    float externalSmoothStep(float value) {
        if (!std::isfinite(value) || std::abs(value) < 0.0001f) return 0.f;
        // prevter spreads one event over frames: keep fractions, not full actions per frame.
        return std::clamp(
            value / static_cast<float>(kInputUnitsPerStep),
            -8.f,
            8.f
        );
    }

    double currentSensitivity() {
        return boundedSetting(
            paimon::settings::smoothscroll::sensitivity(),
            paimon::settings::smoothscroll::kSensitivityMin,
            paimon::settings::smoothscroll::kSensitivityMax,
            paimon::settings::smoothscroll::kSensitivityDefault
        );
    }

    double currentResponse() {
        double const m = boundedSetting(
            paimon::settings::smoothscroll::smoothness(),
            paimon::settings::smoothscroll::kSmoothnessMin,
            paimon::settings::smoothscroll::kSmoothnessMax,
            paimon::settings::smoothscroll::kSmoothnessDefault
        );
        return kBaseResponse / m;
    }

    double currentEditorZoomSensitivity() {
        return boundedSetting(
            paimon::settings::smoothscroll::editorZoomSensitivity(),
            paimon::settings::smoothscroll::kEditorZoomSensitivityMin,
            paimon::settings::smoothscroll::kEditorZoomSensitivityMax,
            paimon::settings::smoothscroll::kEditorZoomSensitivityDefault
        );
    }

    double currentEditorZoomResponse() {
        double const m = boundedSetting(
            paimon::settings::smoothscroll::editorZoomSmoothness(),
            paimon::settings::smoothscroll::kEditorZoomSmoothnessMin,
            paimon::settings::smoothscroll::kEditorZoomSmoothnessMax,
            paimon::settings::smoothscroll::kEditorZoomSmoothnessDefault
        );
        return kBaseResponse / m;
    }
}

SmoothScrollController& SmoothScrollController::get() {
    static SmoothScrollController instance;
    return instance;
}

CCScrollLayerExt* hiddenScrollList(CCMouseDelegate* delegate) {
    if (!delegate) return nullptr;
    auto* list = typeinfo_cast<CCScrollLayerExt*>(delegate);
    if (!list || geode::cocos::nodeIsVisible(list)) return nullptr;
    return list;
}

bool SmoothScrollController::isActive() const {
    if (!paimon::settings::general::smoothScroll()) return false;
    if (paimon::settings::smoothui::reducedMotion()) return false;
    if (paimon::compat::ModCompat::isPrevterSmoothScrollLoaded()) return false;
    return true;
}

bool shouldBypassSmoothScroll() {
    // volume applies 5% per step: replayed momentum would jump to top on one tiny scroll.
    if (paimon::volscroll::isVolumeGestureActive()) {
        return true;
    }

    if (paimon::compat::ModCompat::isQuickVolumeControlsLoaded()) {
        if (auto* kb = CCKeyboardDispatcher::get(); kb && kb->getAltKeyPressed()) {
            return true;
        }
    }

    // Editor panning uses EditorUI::scrollWheel/updatePanDecay in dispatcher/sign units
    // rather than menu-list pixels; feeding pixel momentum across many frames launches
    // the editor camera. Only Ctrl+scroll zoom uses smooth scroll in the editor.
    if (isEditorUIActiveTarget()) {
        auto* kb = CCKeyboardDispatcher::get();
        bool const ctrlHeld = kb && kb->getControlKeyPressed();
        if (!ctrlHeld || !paimon::settings::smoothscroll::editorZoomEnabled()) {
            return true;
        }
    }

    return false;
}

bool isEditorZoomGesture() {
    if (!isEditorUIActiveTarget()) return false;
    auto* kb = CCKeyboardDispatcher::get();
    return kb && kb->getControlKeyPressed();
}

float SmoothScrollController::pixelsPerNotch() const {
    return static_cast<float>(kBasePixelsPerNotch * currentSensitivity());
}

bool SmoothScrollController::queueInput(float wheelY, float wheelX) {
    bool const editorZoom = isEditorZoomGesture()
        && paimon::settings::smoothscroll::editorZoomEnabled();
    void const* const target = currentScrollTarget();
    if (!target) {
        stop();
        return false;
    }

    // never reinterpret old momentum across modes, nor deliver to a different delegate.
    if (m_filter.active() &&
        (m_editorZoomMode != editorZoom || m_scrollTarget != target)) {
        stop();
    }

    m_editorZoomMode = editorZoom;
    m_scrollTarget = target;

    // the dispatcher hands out a fixed amount of units per notch, so the
    // input is normalized once here and every consumer works in notches.
    return m_filter.queue(
        static_cast<double>(wheelY) / kInputUnitsPerStep,
        static_cast<double>(wheelX) / kInputUnitsPerStep
    );
}

void SmoothScrollController::tick(float dt, ScrollDispatchFn const& dispatch) {
    if (!dispatch) {
        stop();
        return;
    }

    if (!m_filter.active()) {
        stop();
        return;
    }

    if (m_scrollTarget != currentScrollTarget()) {
        stop();
        return;
    }

    // drop momentum when the zoom gesture flips mid-flight so it never bleeds into plain scroll.
    if (paimon::settings::smoothscroll::fixEditorScroll()) {
        bool const zoomGestureNow = isEditorZoomGesture()
            && paimon::settings::smoothscroll::editorZoomEnabled();
        if (m_editorZoomMode != zoomGestureNow) {
            stop();
            return;
        }
    }

    if (shouldBypassSmoothScroll()) {
        stop();
        return;
    }

    double const response = m_editorZoomMode ? currentEditorZoomResponse() : currentResponse();
    ScrollVector const frame = m_filter.step(dt, response);

    // discrete consumers (zooms, button steps) work in notch fractions.
    double const actionGain = m_editorZoomMode ? currentEditorZoomSensitivity() : 1.0;
    m_replayActions = {
        std::clamp(frame.y * actionGain, -kMaxActionSteps, kMaxActionSteps),
        std::clamp(frame.x * actionGain, -kMaxActionSteps, kMaxActionSteps)
    };

    // content consumers (lists) work in pixels, the same value everywhere.
    double const pixels = static_cast<double>(pixelsPerNotch());
    ScrollVector const content = { frame.y * pixels, frame.x * pixels };

    bool const hasOutput = content.y != 0.0 || content.x != 0.0 ||
                           m_replayActions.y != 0.0 || m_replayActions.x != 0.0;
    if (!hasOutput) return;

    m_replaying = true;
    m_driving = true;
    dispatch(static_cast<float>(content.y), static_cast<float>(content.x));
    m_replaying = false;
    m_replayActions = {};
}

bool SmoothScrollController::hasMomentum() const {
    return m_filter.active();
}

float SmoothScrollController::replaySteps(bool negate) const {
    if (!m_replaying) return 0.f;
    if (std::abs(m_replayActions.y) >= 0.000001) {
        // cocos' vertical wheel sign is opposite to the visual zoom direction.
        float const y = static_cast<float>(m_replayActions.y);
        return negate ? -y : y;
    }
    return static_cast<float>(m_replayActions.x);
}

float SmoothScrollController::replayedZoomSteps() const {
    return replaySteps(true);
}

float SmoothScrollController::filteredSteps(float wheelY, float wheelX, bool negate) const {
    if (m_replaying) return replaySteps(negate);
    if (paimon::compat::ModCompat::isPrevterSmoothScrollLoaded()) {
        float const y = externalSmoothStep(wheelY);
        return y != 0.f ? (negate ? -y : y) : externalSmoothStep(wheelX);
    }
    float const y = signUnit(wheelY);
    return y != 0.f ? (negate ? -y : y) : signUnit(wheelX);
}

float SmoothScrollController::filteredWheelSteps(float wheelY, float wheelX) const {
    return filteredSteps(wheelY, wheelX, false);
}

float SmoothScrollController::filteredZoomSteps(float wheelY, float wheelX) const {
    return filteredSteps(wheelY, wheelX, true);
}

void SmoothScrollController::stop() {
    m_filter.reset();
    m_replayActions = {};
    m_scrollTarget = nullptr;
    m_driving = false;
    m_editorZoomMode = false;
}

void SmoothScrollController::reset() {
    stop();
    m_replaying = false;
}

} // namespace paimon::smoothscroll
