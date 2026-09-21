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
    // User-scaled base values: gain = base * sensitivity, decay = base / smoothness
    constexpr double kBaseWheelGain = 3.5;
    constexpr double kBaseDecayRate = 12.0;

    double boundedSetting(double value, double minimum, double maximum, double fallback) {
        if (!std::isfinite(value)) return fallback;
        return std::clamp(value, minimum, maximum);
    }

    void const* currentScrollTarget() {
        auto* director = CCDirector::get();
        auto* dispatcher = director ? director->getMouseDispatcher() : nullptr;
        auto* handlers = dispatcher ? dispatcher->m_pMouseHandlers : nullptr;
        if (!handlers) return nullptr;

        // Match the dispatcher's first-live-delegate order so momentum cannot
        // leak into a view that never received the original wheel event.
        for (unsigned i = 0; i < handlers->count(); ++i) {
            auto* handler = static_cast<CCMouseHandler*>(handlers->objectAtIndex(i));
            if (handler && handler->m_pDelegate) return handler->m_pDelegate;
        }
        return nullptr;
    }

    float signUnit(float value) {
        if (!std::isfinite(value) || std::abs(value) < 0.0001f) return 0.f;
        return value > 0.f ? 1.f : -1.f;
    }

    float externalSmoothStep(float value) {
        if (!std::isfinite(value) || std::abs(value) < 0.0001f) return 0.f;
        // Prevter spreads one wheel event over frames: keep the fractions
        // instead of replaying a full discrete action per frame.
        constexpr float kExternalUnitsPerStep = 12.f;
        return std::clamp(value / kExternalUnitsPerStep, -8.f, 8.f);
    }

    double currentWheelGain() {
        double const s = boundedSetting(
            paimon::settings::smoothscroll::sensitivity(),
            paimon::settings::smoothscroll::kSensitivityMin,
            paimon::settings::smoothscroll::kSensitivityMax,
            paimon::settings::smoothscroll::kSensitivityDefault
        );
        return kBaseWheelGain * s;
    }

    double currentDecayRate() {
        double const m = boundedSetting(
            paimon::settings::smoothscroll::smoothness(),
            paimon::settings::smoothscroll::kSmoothnessMin,
            paimon::settings::smoothscroll::kSmoothnessMax,
            paimon::settings::smoothscroll::kSmoothnessDefault
        );
        return kBaseDecayRate / m;
    }

    double currentEditorZoomSensitivity() {
        return boundedSetting(
            paimon::settings::smoothscroll::editorZoomSensitivity(),
            paimon::settings::smoothscroll::kEditorZoomSensitivityMin,
            paimon::settings::smoothscroll::kEditorZoomSensitivityMax,
            paimon::settings::smoothscroll::kEditorZoomSensitivityDefault
        );
    }

    double currentEditorZoomGain() {
        return kBaseWheelGain * currentEditorZoomSensitivity();
    }

    double currentEditorZoomDecayRate() {
        double const m = boundedSetting(
            paimon::settings::smoothscroll::editorZoomSmoothness(),
            paimon::settings::smoothscroll::kEditorZoomSmoothnessMin,
            paimon::settings::smoothscroll::kEditorZoomSmoothnessMax,
            paimon::settings::smoothscroll::kEditorZoomSmoothnessDefault
        );
        return kBaseDecayRate / m;
    }
}

SmoothScrollController& SmoothScrollController::get() {
    static SmoothScrollController instance;
    return instance;
}

bool SmoothScrollController::isActive() const {
    if (!paimon::settings::general::smoothScroll()) return false;
    if (paimon::settings::smoothui::reducedMotion()) return false;
    if (paimon::compat::ModCompat::isPrevterSmoothScrollLoaded()) return false;
    return true;
}

bool shouldBypassSmoothScroll() {
    // Volume gestures bypass smoothing: the volume hook applies 5% per step, so
    // replayed momentum would jump straight to the top with one tiny scroll.
    if (paimon::volscroll::isVolumeGestureActive()) {
        return true;
    }

    if (paimon::compat::ModCompat::isQuickVolumeControlsLoaded()) {
        if (auto* kb = CCKeyboardDispatcher::get(); kb && kb->getAltKeyPressed()) {
            return true;
        }
    }

    if (isEditorZoomGesture() && !paimon::settings::smoothscroll::editorZoomEnabled()) {
        return true;
    }

    return false;
}

bool isEditorZoomGesture() {
    auto* editorLayer = LevelEditorLayer::get();
    if (!editorLayer || !editorLayer->m_editorUI) return false;
    auto* kb = CCKeyboardDispatcher::get();
    if (!kb || !kb->getControlKeyPressed()) return false;

    // Ctrl may be held while a popup or an embedded list is above the editor.
    // Only classify this as editor zoom when EditorUI is the actual wheel sink.
    auto* editorTarget = static_cast<CCMouseDelegate*>(editorLayer->m_editorUI);
    return currentScrollTarget() == editorTarget;
}

bool SmoothScrollController::queueInput(float wheelY, float wheelX) {
    bool const editorZoom = isEditorZoomGesture()
        && paimon::settings::smoothscroll::editorZoomEnabled();
    void const* const target = currentScrollTarget();
    if (!target) {
        stop();
        return false;
    }

    // Never reinterpret old list momentum as zoom (or old zoom as scrolling),
    // and never deliver momentum to a different mouse delegate.
    if (m_filter.active() &&
        (m_editorZoomMode != editorZoom || m_scrollTarget != target)) {
        stop();
    }

    m_editorZoomMode = editorZoom;
    m_scrollTarget = target;

    double const contentGain = editorZoom ? currentEditorZoomGain() : currentWheelGain();
    double const actionGain = editorZoom ? currentEditorZoomSensitivity() : 1.0;

    return m_filter.queue(
        static_cast<double>(wheelY),
        static_cast<double>(wheelX),
        contentGain,
        actionGain,
        kInputUnitsPerStep
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

    // Drop leftover momentum when the zoom gesture flips mid-flight (e.g. Ctrl
    // released), so it never bleeds into a plain vertical scroll.
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

    double const decayRate = m_editorZoomMode ? currentEditorZoomDecayRate() : currentDecayRate();
    auto const frame = m_filter.step(static_cast<double>(dt), decayRate);
    m_replayActions = frame.actions;

    bool const hasOutput = frame.content.y != 0.0 || frame.content.x != 0.0 ||
                           frame.actions.y != 0.0 || frame.actions.x != 0.0;
    if (!hasOutput) return;

    m_replaying = true;
    dispatch(static_cast<float>(frame.content.y), static_cast<float>(frame.content.x));
    m_replaying = false;
    m_replayActions = {};
}

bool SmoothScrollController::hasMomentum() const {
    return m_filter.active();
}

float SmoothScrollController::replayedWheelSteps() const {
    if (!m_replaying) return 0.f;
    if (std::abs(m_replayActions.y) >= 0.000001) {
        return static_cast<float>(m_replayActions.y);
    }
    return static_cast<float>(m_replayActions.x);
}

float SmoothScrollController::replayedZoomSteps() const {
    if (!m_replaying) return 0.f;
    if (std::abs(m_replayActions.y) >= 0.000001) {
        // Cocos' vertical wheel sign is opposite to the visual zoom direction.
        return static_cast<float>(-m_replayActions.y);
    }
    return static_cast<float>(m_replayActions.x);
}

float SmoothScrollController::filteredWheelSteps(float wheelY, float wheelX) const {
    if (m_replaying) return replayedWheelSteps();
    if (paimon::compat::ModCompat::isPrevterSmoothScrollLoaded()) {
        float const y = externalSmoothStep(wheelY);
        return y != 0.f ? y : externalSmoothStep(wheelX);
    }
    float const y = signUnit(wheelY);
    return y != 0.f ? y : signUnit(wheelX);
}

float SmoothScrollController::filteredZoomSteps(float wheelY, float wheelX) const {
    if (m_replaying) return replayedZoomSteps();
    if (paimon::compat::ModCompat::isPrevterSmoothScrollLoaded()) {
        float const y = externalSmoothStep(wheelY);
        return y != 0.f ? -y : externalSmoothStep(wheelX);
    }
    float const y = signUnit(wheelY);
    return y != 0.f ? -y : signUnit(wheelX);
}

void SmoothScrollController::stop() {
    m_filter.reset();
    m_replayActions = {};
    m_scrollTarget = nullptr;
    m_editorZoomMode = false;
}

void SmoothScrollController::reset() {
    stop();
    m_replaying = false;
}

} // namespace paimon::smoothscroll
