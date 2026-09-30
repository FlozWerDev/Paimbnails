#pragma once

#include <algorithm>
#include <cmath>

namespace paimon::smoothscroll {

struct ScrollVector {
    double y = 0.0;
    double x = 0.0;
};

struct FilteredScrollFrame {
    // continuous distance consumed by lists and other magnitude-aware views.
    ScrollVector content;
    // normalized wheel steps consumed by zooms and other sign-only actions.
    ScrollVector actions;
};

// momentum and discrete intent stay separate: one wheel step always tops up to one action.
class ScrollInputFilter {
public:
    static constexpr double kMaxRawInput = 60.0;
    static constexpr double kMaxContentMomentum = 600.0;
    static constexpr double kMaxActionMomentum = 8.0;
    static constexpr double kContentStopEpsilon = 0.05;
    static constexpr double kActionStopEpsilon = 0.002;
    static constexpr double kMaxFrameTime = 0.05;

    bool queue(
        double wheelY,
        double wheelX,
        double contentGain,
        double actionGain,
        double inputUnitsPerStep
    ) {
        wheelY = sanitize(wheelY);
        wheelX = sanitize(wheelX);
        if (wheelY == 0.0 && wheelX == 0.0) return active();

        contentGain = finiteOr(contentGain, 0.0);
        actionGain = finiteOr(actionGain, 0.0);
        inputUnitsPerStep = std::abs(finiteOr(inputUnitsPerStep, 1.0));
        if (inputUnitsPerStep < 0.001) inputUnitsPerStep = 1.0;

        queueAxis(wheelY, contentGain, actionGain, inputUnitsPerStep,
                  m_content.y, m_actions.y);
        queueAxis(wheelX, contentGain, actionGain, inputUnitsPerStep,
                  m_content.x, m_actions.x);
        return active();
    }

    FilteredScrollFrame step(double dt, double decayRate) {
        FilteredScrollFrame frame;
        if (!active() || !std::isfinite(dt) || dt <= 0.0) return frame;

        dt = std::min(dt, kMaxFrameTime);
        decayRate = std::clamp(finiteOr(decayRate, 12.0), 0.01, 100.0);
        double const blend = 1.0 - std::exp(-decayRate * dt);

        frame.content.y = drain(m_content.y, blend, kContentStopEpsilon);
        frame.content.x = drain(m_content.x, blend, kContentStopEpsilon);
        frame.actions.y = drain(m_actions.y, blend, kActionStopEpsilon);
        frame.actions.x = drain(m_actions.x, blend, kActionStopEpsilon);
        return frame;
    }

    bool active() const {
        return m_content.y != 0.0 || m_content.x != 0.0 ||
               m_actions.y != 0.0 || m_actions.x != 0.0;
    }

    void reset() {
        m_content = {};
        m_actions = {};
    }

private:
    ScrollVector m_content;
    ScrollVector m_actions;

    static double finiteOr(double value, double fallback) {
        return std::isfinite(value) ? value : fallback;
    }

    static double sanitize(double value) {
        if (!std::isfinite(value) || std::abs(value) < 0.0001) return 0.0;
        return std::clamp(value, -kMaxRawInput, kMaxRawInput);
    }

    static void queueAxis(
        double input,
        double contentGain,
        double actionGain,
        double inputUnitsPerStep,
        double& content,
        double& actions
    ) {
        if (input == 0.0) return;

        // wheel reversal reacts at once instead of repaying opposite momentum first.
        bool const reversesContent =
            (content > 0.0 && input < 0.0) || (content < 0.0 && input > 0.0);
        bool const reversesActions =
            (actions > 0.0 && input < 0.0) || (actions < 0.0 && input > 0.0);
        if (reversesContent || reversesActions) {
            content = 0.0;
            actions = 0.0;
        }

        content = std::clamp(
            content + input * contentGain,
            -kMaxContentMomentum,
            kMaxContentMomentum
        );
        actions = std::clamp(
            actions + input / inputUnitsPerStep * actionGain,
            -kMaxActionMomentum,
            kMaxActionMomentum
        );
    }

    static double drain(double& pending, double blend, double epsilon) {
        if (pending == 0.0) return 0.0;

        double output = pending * blend;
        pending -= output;
        if (std::abs(pending) <= epsilon) {
            // flush the tail: discrete consumers accumulate exactly one step.
            output += pending;
            pending = 0.0;
        }
        return output;
    }
};

} // namespace paimon::smoothscroll
