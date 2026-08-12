#pragma once

#include "PhysicsTypes.hpp"

namespace paimon::editorphysics {

struct LabConfig {
    float gravity = -900.f;
    float restitution = 0.35f;
    float friction = 0.5f;
    float airDrag = 0.08f;
    float duration = 3.f;
    float velocityX = 0.f;
    float velocityY = 0.f;
    float spinDegrees = 0.f;
    int sampleRate = 20;
};

LabConfig loadConfig();
void saveConfig(LabConfig const& config);
SimulationOptions simulationOptions(LabConfig const& config);

} // namespace paimon::editorphysics
