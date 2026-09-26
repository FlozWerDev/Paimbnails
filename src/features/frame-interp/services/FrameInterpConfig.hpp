#pragma once

// Frame interpolation config. Own JSON (frame_interp.json), like RTX.

namespace paimon::frameinterp {

// Draw lag behind simulation, in physics steps: a full step never
// extrapolates; zero draws the present by guessing the last stretch.
enum class Latency : int {
    Smooth   = 0,
    Balanced = 1,
    Instant  = 2,
};

struct FrameInterpConfig {
    bool  enabled       = false;
    bool  camera        = true;
    bool  scenery       = true;
    bool  players       = true;
    bool  movingObjects = false;

    int   latency       = static_cast<int>(Latency::Smooth);
    float strength      = 1.00f;
    int   objectLimit   = 400;

    bool  inGameplay    = true;
    bool  inEditor      = true;
};

// Lag fraction kept per mode.
double latencyLag(int latency);

} // namespace paimon::frameinterp
