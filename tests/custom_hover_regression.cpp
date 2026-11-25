#include "../src/features/custom-hover/HoverConfig.hpp"

#include <cassert>
#include <cmath>
#include <limits>

int main() {
    using namespace paimon::hover;

    float const nan = std::numeric_limits<float>::quiet_NaN();
    float const inf = std::numeric_limits<float>::infinity();

    // Non-finite config values must sanitize cleanly to safe bounds.
    Config bad;
    bad.scale = nan;
    bad.stretch = inf;
    bad.lift = -inf;
    bad.slide = 100.f;
    bad.rotation = -999.f;
    bad.enter = 0.f;
    bad.exit = nan;
    bad.delay = 5.f;
    bad.amplitude = -1.f;
    bad.frequency = 0.f;
    bad.easing = 99;
    bad.loop = -3;

    Config clean = sanitize(bad);
    assert(clean.scale == 1.1f);
    assert(clean.stretch == 1.f);
    assert(clean.lift == 0.f);
    assert(clean.slide == 25.f);
    assert(clean.rotation == -180.f);
    assert(clean.enter >= 0.05f);
    assert(clean.exit >= 0.05f);
    assert(clean.delay == 1.f);
    assert(clean.amplitude == 0.f);
    assert(clean.frequency == 0.2f);
    assert(clean.easing == 4);
    assert(clean.loop == 0);

    // Easing curves must be finite and exact at 0 and 1.
    for (int mode = 0; mode <= 4; ++mode) {
        assert(std::abs(ease(0.f, mode) - 0.f) < 1e-5f);
        assert(std::abs(ease(1.f, mode) - 1.f) < 1e-5f);
        assert(ease(nan, mode) == 0.f);
        assert(std::isfinite(ease(0.5f, mode)));
    }

    // Pose math must stay finite and keep scale positive even on extreme loops.
    Config extreme;
    extreme.scale = 0.5f;
    extreme.stretch = 1.5f;
    extreme.amplitude = 0.25f;
    extreme.loop = 4;
    for (int i = 0; i < 40; ++i) {
        Pose p = pose(extreme, 1.f, static_cast<float>(i) * 0.05f);
        assert(isFinitePose(p));
        assert(p.sx >= 0.1f && p.sy >= 0.1f);
    }

    // Zero amount returns identity pose.
    assert(isIdentityPose(pose(extreme, 0.f, 1.23f)));

    // HoverTrackState: delay honored from rest, exit from rest finishes immediately,
    // and mid-exit re-entry skips delay and finishes cleanly.
    Config delayed;
    delayed.scale = 1.2f;
    delayed.enter = 0.2f;
    delayed.exit = 0.2f;
    delayed.delay = 0.15f;

    HoverTrackState track;
    // Unhovered at rest -> immediately finished.
    Pose p0 = track.step(delayed, 0.016f, false);
    assert(isIdentityPose(p0));
    assert(track.isFinishedExit());

    // Hover starts -> waits for delay before moving.
    for (int i = 0; i < 3; ++i) {
        Pose p = track.step(delayed, 0.03f, true);
        assert(isIdentityPose(p));
    }
    // Abort hover during delay -> finishes exit immediately.
    Pose abortP = track.step(delayed, 0.016f, false);
    assert(isIdentityPose(abortP));
    assert(track.isFinishedExit());

    // Hover past delay -> reaches full amount.
    track.reset();
    for (int i = 0; i < 20; ++i) {
        (void)track.step(delayed, 0.025f, true);
    }
    assert(track.amount > 0.99f);

    // Begin exit, then re-enter mid-exit -> should NOT stall at delay.
    (void)track.step(delayed, 0.05f, false);
    float const midExitAmount = track.amount;
    assert(midExitAmount > 0.1f && midExitAmount < 0.99f);
    (void)track.step(delayed, 0.05f, true);
    assert(track.amount > midExitAmount);

    // Complete exit -> settles to identity and marks finished.
    for (int i = 0; i < 20; ++i) {
        (void)track.step(delayed, 0.025f, false);
    }
    assert(track.isFinishedExit());
    assert(track.amount == 0.f);

    // Presets sanity check.
    assert(!presets().empty());
    assert(configsMatch(presets()[0].config, presets()[0].config));
    assert(!configsMatch(presets()[0].config, presets()[1].config));

    return 0;
}
