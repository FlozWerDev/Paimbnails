#pragma once

#include "../PhysicsNative.hpp"

#include <vector>

namespace paimon::editorphysics {

// every body is traced on the backend it will compile to, and only the baked
// ones keep the solver, so the preview and the level agree.
SimulationTrace simulateWorkspace(
    std::vector<BodySpec> const& bodies,
    std::vector<NativeBodySettings> const& settings,
    SimulationOptions const& options
);

// true when the graph cannot move the body without the player being there, so
// the lab can say why it is standing still instead of falling.
bool nativeNeedsPlayer(NativeBodySettings const& settings, BodySpec const& body, float gravity);

} // namespace paimon::editorphysics
