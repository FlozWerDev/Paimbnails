#include "SmoothScrollController.hpp"
#include "../../../core/Settings.hpp"
#include "../../../framework/compat/ModCompat.hpp"
#include "../../volume-scroll/services/VolumeScrollManager.hpp"
#include <Geode/Geode.hpp>
#include <Geode/cocos/robtop/keyboard_dispatcher/CCKeyboardDispatcher.h>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace paimon::smoothscroll {

namespace {
    // User-scaled base values: gain = base * sensitivity, decay = base / smoothness
    constexpr double kBaseWheelGain = 3.5;
    constexpr double kBaseDecayRate = 12.0;
    constexpr double kStopEpsilon = 0.05;

    double currentWheelGain() {
        double s = std::clamp(paimon::settings::smoothscroll::sensitivity(),
                              paimon::settings::smoothscroll::kSensitivityMin,
                              paimon::settings::smoothscroll::kSensitivityMax);
        return kBaseWheelGain * s;
    }

    double currentDecayRate() {
        double m = std::clamp(paimon::settings::smoothscroll::smoothness(),
                              paimon::settings::smoothscroll::kSmoothnessMin,
                              paimon::settings::smoothscroll::kSmoothnessMax);
        return kBaseDecayRate / m;
    }
}

SmoothScrollController& SmoothScrollController::get() {
    static SmoothScrollController instance;
    return instance;
}

bool SmoothScrollController::isActive() const {
    if (!paimon::settings::general::smoothScroll()) return false;
    if (paimon::compat::ModCompat::isPrevterSmoothScrollLoaded()) return false;
    return true;
}

bool shouldBypassSmoothScroll() {
    // A volume-scroll gesture (e.g. Ctrl/Shift + wheel) must reach the volume hook as a
    // single discrete step. If smooth scroll captured it, it would replay the one wheel
    // tick as many small momentum steps, and the volume hook applies a fixed 5% per step,
    // jumping the volume straight to the top with one tiny scroll. So bypass smoothing here.
    if (paimon::volscroll::isVolumeGestureActive()) {
        return true;
    }

    if (paimon::compat::ModCompat::isQuickVolumeControlsLoaded()) {
        if (auto* kb = CCKeyboardDispatcher::get(); kb && kb->getAltKeyPressed()) {
            return true;
        }
    }

    if (LevelEditorLayer::get()) {
        if (auto* kb = CCKeyboardDispatcher::get(); kb && kb->getControlKeyPressed()) {
            return true;
        }
    }

    return false;
}

bool shouldUseSmoothPauseZoom() {
    return paimon::compat::ModCompat::isPrevterSmoothScrollLoaded()
        || paimon::settings::general::smoothScroll();
}

bool SmoothScrollController::queueInput(float wheelY, float wheelX) {
    double const gain = currentWheelGain();
    m_momentumY += static_cast<double>(wheelY) * gain;
    m_momentumX += static_cast<double>(wheelX) * gain;
    return std::abs(m_momentumY) > kStopEpsilon || std::abs(m_momentumX) > kStopEpsilon;
}

void SmoothScrollController::tick(float dt, ScrollDispatchFn const& dispatch) {
    if (!dispatch) {
        stop();
        return;
    }

    if (std::abs(m_momentumY) < kStopEpsilon && std::abs(m_momentumX) < kStopEpsilon) {
        stop();
        return;
    }

    if (shouldBypassSmoothScroll()) {
        stop();
        return;
    }

    double const blend = 1.0 - std::exp(-currentDecayRate() * static_cast<double>(dt));

    float const stepY = static_cast<float>(m_momentumY * blend);
    float const stepX = static_cast<float>(m_momentumX * blend);

    m_momentumY -= static_cast<double>(stepY);
    m_momentumX -= static_cast<double>(stepX);

    if (std::abs(m_momentumY) < kStopEpsilon) m_momentumY = 0.0;
    if (std::abs(m_momentumX) < kStopEpsilon) m_momentumX = 0.0;

    m_replaying = true;
    dispatch(stepY, stepX);
    m_replaying = false;
}

bool SmoothScrollController::hasMomentum() const {
    return std::abs(m_momentumY) >= kStopEpsilon || std::abs(m_momentumX) >= kStopEpsilon;
}

void SmoothScrollController::stop() {
    m_momentumY = 0.0;
    m_momentumX = 0.0;
}

void SmoothScrollController::reset() {
    stop();
    m_replaying = false;
}

} // namespace paimon::smoothscroll