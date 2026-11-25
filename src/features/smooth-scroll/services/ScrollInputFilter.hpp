#pragma once

#include <algorithm>
#include <cmath>

namespace paimon::smoothscroll {

struct ScrollVector {
    double y = 0.0;
    double x = 0.0;
};

// momentum as a critically damped spring.
//
// the old model drained the pending travel with an exponential decay, which
// front-loads the motion: the first frames move at 12x the average speed and
// the tail crawls. A spring spreads the same travel over a smooth ramp, never
// overshoots and always delivers exactly the queued distance, so every scroll
// zone ends up at the same speed.
//
// the unit is the notch (one wheel click): 1.0 == one notch.
class ScrollInputFilter {
public:
    static constexpr double kMaxNotchesPerEvent = 12.0;
    static constexpr double kMaxPendingNotches = 48.0;
    static constexpr double kSettleEpsilon = 0.001;
    static constexpr double kMaxFrameTime = 0.05;

    // notchesY/notchesX are expressed in notches.
    bool queue(double notchesY, double notchesX) {
        notchesY = sanitize(notchesY);
        notchesX = sanitize(notchesX);
        if (notchesY == 0.0 && notchesX == 0.0) return active();

        queueAxis(notchesY, m_y);
        queueAxis(notchesX, m_x);
        return active();
    }

    // travel delivered this frame, in notches.
    ScrollVector step(double dt, double response) {
        ScrollVector frame;
        if (!active() || !std::isfinite(dt) || dt <= 0.0) return frame;

        dt = std::min(dt, kMaxFrameTime);
        double const omega = std::clamp(finiteOr(response, 12.0), 1.0, 60.0);
        frame.y = advance(m_y, dt, omega);
        frame.x = advance(m_x, dt, omega);
        return frame;
    }

    bool active() const {
        return !m_y.settled() || !m_x.settled();
    }

    void reset() {
        m_y = {};
        m_x = {};
    }

private:
    struct Axis {
        // pending travel, in notches
        double target = 0.0;
        // where the spring currently sits
        double pos = 0.0;
        // notches per second
        double vel = 0.0;
        // what has already been handed to the game
        double delivered = 0.0;

        bool settled() const {
            return std::abs(target - pos) <= kSettleEpsilon &&
                   std::abs(vel) <= kSettleEpsilon;
        }
    };

    Axis m_y;
    Axis m_x;

    static double finiteOr(double value, double fallback) {
        return std::isfinite(value) ? value : fallback;
    }

    static double sanitize(double value) {
        if (!std::isfinite(value) || std::abs(value) < 0.0001) return 0.0;
        return std::clamp(value, -kMaxNotchesPerEvent, kMaxNotchesPerEvent);
    }

    static bool opposes(double value, double notches) {
        return (value > 0.0 && notches < 0.0) || (value < 0.0 && notches > 0.0);
    }

    static void queueAxis(double notches, Axis& axis) {
        if (notches == 0.0) return;

        // target and pos are measured from where the gesture started, so only
        // the travel still pending (and the current motion) tell the direction.
        // comparing the target itself kept every upward notch after a downward
        // scroll flagged as a reversal, clipping the travel to one notch.
        double pending = axis.target - axis.pos;

        // a reversal drops the pending travel and the motion instead of paying
        // them back first, so the list turns around on the same frame.
        if (opposes(pending, notches) || opposes(axis.vel, notches)) {
            pending = 0.0;
            axis.vel = 0.0;
        }

        // the cap limits the travel still pending, not the distance covered,
        // otherwise a long continuous scroll stalls once it reaches the cap.
        axis.target = axis.pos + std::clamp(
            pending + notches,
            -kMaxPendingNotches,
            kMaxPendingNotches
        );
    }

    // closed form of the critically damped spring: stable for any dt and
    // independent of the frame rate.
    static double advance(Axis& axis, double dt, double omega) {
        double const error = axis.pos - axis.target;
        double const slope = axis.vel + omega * error;
        double const decay = std::exp(-omega * dt);

        axis.pos = axis.target + (error + slope * dt) * decay;
        axis.vel = (axis.vel - omega * slope * dt) * decay;

        // snap the tail so the travel lands exactly on the target
        if (std::abs(axis.target - axis.pos) <= kSettleEpsilon &&
            std::abs(axis.vel) <= kSettleEpsilon) {
            axis.pos = axis.target;
            axis.vel = 0.0;
        }

        double const delta = axis.pos - axis.delivered;
        axis.delivered = axis.pos;
        return delta;
    }
};

} // namespace paimon::smoothscroll
