#include <cmath>
#include <iostream>
#include <vector>

#include "../src/features/editor-physics/PhysicsNative.hpp"
#include "../src/features/editor-physics/services/NativePreview.hpp"

using namespace paimon::editorphysics;

namespace {

BodySpec box(Motion motion, Vec2 position, Vec2 halfSize) {
    BodySpec body;
    body.motion = motion;
    body.position = position;
    Fixture fixture;
    fixture.halfSize = halfSize;
    body.fixtures.push_back(fixture);
    return body;
}

SimulationOptions options(float duration = 2.f) {
    SimulationOptions value;
    value.duration = duration;
    value.fixedRate = 240;
    value.sampleRate = 30;
    value.solverIterations = 6;
    return value;
}

NativeBodySettings backend(PhysicsBackend value) {
    NativeBodySettings settings;
    settings.backend = value;
    return settings;
}

std::vector<BodySpec> fallingOntoFloor() {
    auto body = box(Motion::Dynamic, {0.f, 200.f}, {15.f, 15.f});
    body.restitution = 0.5f;
    body.friction = 0.5f;
    auto floor = box(Motion::Static, {0.f, 0.f}, {200.f, 10.f});
    return {body, floor};
}

bool bakedBodyKeepsTheSolverPath() {
    auto const bodies = fallingOntoFloor();
    auto const solver = simulate(bodies, options());
    auto const mixed = simulateWorkspace(
        bodies, {{}, {}}, options()
    );
    if (NativeBodySettings{}.backend != PhysicsBackend::Baked ||
        solver.frames.size() != mixed.frames.size()) return false;
    for (std::size_t f = 0; f < solver.frames.size(); ++f) {
        if (solver.frames[f].time != mixed.frames[f].time) return false;
        for (std::size_t b = 0; b < bodies.size(); ++b) {
            auto const& a = solver.frames[f].poses[b];
            auto const& c = mixed.frames[f].poses[b];
            if (a.position.x != c.position.x || a.position.y != c.position.y ||
                a.angle != c.angle) return false;
        }
    }
    float const drift = std::abs(
        mixed.frames.back().poses.front().position.y -
        solver.frames.back().poses.front().position.y
    );
    std::cout << "baked-path: drift=" << drift << '\n';
    return drift < 0.0001f;
}

bool reactiveBodyLeavesTheSolverPath() {
    auto const bodies = fallingOntoFloor();
    auto const solver = simulate(bodies, options());
    auto const mixed = simulateWorkspace(
        bodies, {backend(PhysicsBackend::Reactive), {}}, options()
    );
    auto const& pose = mixed.frames.back().poses.front();
    float const drift = std::abs(
        pose.position.y - solver.frames.back().poses.front().position.y
    );
    // The sensors stop it over the floor like the graph does, but the discrete
    // gravity loop and the entry-only rebound never land on the solver's curve.
    bool const stopped = pose.position.y > 10.f && pose.position.y < 90.f;
    std::cout << "reactive-path: y=" << pose.position.y << " drift=" << drift << '\n';
    return stopped && drift > 1.f;
}

bool reactiveBodyDoesNotPushABakedOne() {
    auto reactive = box(Motion::Dynamic, {0.f, 120.f}, {15.f, 15.f});
    auto baked = box(Motion::Dynamic, {0.f, 40.f}, {15.f, 15.f});
    auto floor = box(Motion::Static, {0.f, 0.f}, {200.f, 10.f});
    auto const mixed = simulateWorkspace(
        {reactive, baked, floor},
        {backend(PhysicsBackend::Reactive), backend(PhysicsBackend::Baked), {}},
        options()
    );
    auto const alone = simulate({baked, floor}, options());
    float const drift = std::abs(
        mixed.frames.back().poses[1].position.y -
        alone.frames.back().poses.front().position.y
    );
    std::cout << "no-cross-talk: drift=" << drift << '\n';
    return drift < 0.0001f;
}

bool magnetHoldsStillWithoutAPlayer() {
    auto body = box(Motion::Dynamic, {0.f, 200.f}, {15.f, 15.f});
    NativeBodySettings settings = backend(PhysicsBackend::Reactive);
    settings.preset = NativePreset::Magnet;
    auto const mixed = simulateWorkspace({body}, {settings}, options());
    auto const& pose = mixed.frames.back().poses.front();
    std::cout << "magnet-idle: y=" << pose.position.y << '\n';
    return std::abs(pose.position.y - 200.f) < 0.0001f &&
        nativeNeedsPlayer(settings, body, -900.f);
}

bool launchSpeedReachesFollowUnits() {
    NativeBodyInput body;
    body.settings.backend = PhysicsBackend::Reactive;
    body.spec = box(Motion::Dynamic, {0.f, 0.f}, {15.f, 15.f});
    body.spec.velocity = {900.f, 0.f};
    body.objectCount = 1;
    body.settings.targetPlayer1 = false;
    body.settings.targetPlayer2 = false;

    NativeLayout layout;
    layout.manifestGroup = 99;
    layout.bodies.resize(1);
    layout.bodies[0].rootGroup = 1;
    layout.bodies[0].controlID = 1;
    layout.bodies[0].gravityGroup = 2;
    auto const graph = buildNativeTriggerGraph({body}, layout, -900.f);

    float startSpeed = -1.f;
    for (auto const& node : graph.nodes) {
        if (node.kind == NativeNodeKind::AdvancedFollow) startSpeed = node.startSpeed;
    }
    std::cout << "launch-units: startSpeed=" << startSpeed << '\n';
    return std::abs(startSpeed - 900.f / kPixelsPerSpeedUnit) < 0.001f;
}

} // namespace

int main() {
    struct Case {
        char const* name;
        bool (*run)();
    };
    Case const cases[] = {
        {"baked body keeps the solver path", bakedBodyKeepsTheSolverPath},
        {"reactive body runs the native model", reactiveBodyLeavesTheSolverPath},
        {"reactive body stays out of the baked one", reactiveBodyDoesNotPushABakedOne},
        {"magnet waits for the player", magnetHoldsStillWithoutAPlayer},
        {"launch speed in follow units", launchSpeedReachesFollowUnits},
    };
    bool passed = true;
    for (auto const& test : cases) {
        bool const result = test.run();
        std::cout << (result ? "[PASS] " : "[FAIL] ") << test.name << '\n';
        passed &= result;
    }
    return passed ? 0 : 1;
}
