#pragma once

#include <cstddef>
#include <vector>

namespace paimon::editorphysics {

struct Vec2 {
    float x = 0.f;
    float y = 0.f;
};

struct Fixture {
    Vec2 offset;
    Vec2 halfSize;
};

enum class Motion {
    Dynamic,
    Static,
};

struct BodySpec {
    Motion motion = Motion::Static;
    Vec2 position;
    Vec2 velocity;
    float angle = 0.f;
    float angularVelocity = 0.f;
    float mass = 1.f;
    float gravityScale = 1.f;
    float restitution = 0.35f;
    float friction = 0.5f;
    std::vector<Fixture> fixtures;
};

struct SimulationOptions {
    Vec2 gravity{0.f, -900.f};
    float duration = 3.f;
    float airDrag = 0.08f;
    float angularDrag = 0.25f;
    int fixedRate = 120;
    int sampleRate = 20;
    int solverIterations = 5;
};

struct Pose {
    Vec2 position;
    float angle = 0.f;
};

struct Frame {
    float time = 0.f;
    std::vector<Pose> poses;
};

struct SimulationTrace {
    std::vector<Frame> frames;
    std::size_t impacts = 0;
    float peakImpulse = 0.f;
};

SimulationTrace simulate(
    std::vector<BodySpec> const& bodies,
    SimulationOptions const& options
);

} // namespace paimon::editorphysics
