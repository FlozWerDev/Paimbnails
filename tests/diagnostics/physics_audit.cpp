// Standalone audit reproductions. Not registered in the passing regression suite.
// A failed check demonstrates an outstanding defect, not an applied fix.
#include "../../src/features/editor-physics/PhysicsNative.hpp"
#include "../../src/features/editor-physics/services/NativePreview.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <string_view>
#include <vector>

using namespace paimon::editorphysics;

namespace {

BodySpec box(Motion motion, Vec2 position, Vec2 halfSize) {
    BodySpec body;
    body.motion = motion;
    body.position = position;
    body.friction = 0.f;
    body.restitution = 0.f;
    Fixture fixture;
    fixture.halfSize = halfSize;
    body.fixtures.push_back(fixture);
    return body;
}

SimulationOptions options(float duration) {
    SimulationOptions value;
    value.duration = duration;
    value.fixedRate = 120;
    value.sampleRate = 40;
    value.airDrag = 0.f;
    value.angularDrag = 0.f;
    return value;
}

bool unsupportedBodyKeepsFalling() {
    auto const body = box(Motion::Dynamic, {0.f, 100.f}, {10.f, 10.f});
    auto config = options(3.f);
    config.gravity = {0.f, -10.f};
    auto const sleeping = simulate({body}, config);
    config.allowSleep = false;
    auto const awake = simulate({body}, config);
    float const actual = sleeping.frames.back().poses[0].position.y;
    float const reference = awake.frames.back().poses[0].position.y;
    std::cout << "  y=" << actual << " reference=" << reference
              << " settleTime=" << sleeping.settleTime << '\n';
    return std::abs(actual - reference) < 0.1f;
}

bool overlappingFixturesDoNotMultiplySeparation() {
    auto body = box(Motion::Dynamic, {0.f, 19.f}, {10.f, 10.f});
    auto const floor = box(Motion::Static, {}, {200.f, 10.f});
    auto config = options(1.f);
    config.allowSleep = false;
    PhysicsWorld single({body, floor}, config);
    single.step(1.f / 120.f);
    body.fixtures.resize(20, body.fixtures.front());
    PhysicsWorld layered({body, floor}, config);
    layered.step(1.f / 120.f);
    float const reference = single.snapshot().poses[0].position.y;
    float const actual = layered.snapshot().poses[0].position.y;
    std::cout << "  oneFixtureY=" << reference << " twentyFixturesY=" << actual << '\n';
    return std::abs(actual - reference) < 0.2f;
}

bool rotatingBarDetectsItsSweptContact() {
    auto bar = box(Motion::Dynamic, {}, {100.f, 1.f});
    bar.angularVelocity = 12.5663706f;
    // The peg lies on the bar's tip arc halfway through the first step.
    auto const peg = box(Motion::Static, {99.86295f, 5.233596f}, {0.5f, 0.5f});
    auto config = options(1.f);
    config.gravity = {};
    PhysicsWorld world({bar, peg}, config);
    world.step(1.f / 120.f);
    std::cout << "  impacts=" << world.impacts()
              << " finalAngle=" << world.snapshot().poses[0].angle << '\n';
    return world.impacts() > 0;
}

bool fixedRotationRejectsInitialSpin() {
    auto body = box(Motion::Dynamic, {}, {10.f, 10.f});
    body.fixedRotation = true;
    body.angularVelocity = 1.f;
    auto config = options(1.f);
    config.gravity = {};
    auto const trace = simulate({body}, config);
    float const angle = trace.frames.back().poses[0].angle;
    std::cout << "  angle=" << angle << " expected=0\n";
    return std::abs(angle) < 0.001f;
}

bool sleeperFallsAfterLosingSupport() {
    auto body = box(Motion::Dynamic, {0.f, 20.f}, {10.f, 10.f});
    body.fixedRotation = true;
    auto platform = box(Motion::Kinematic, {}, {30.f, 10.f});
    platform.velocity = {20.f, 0.f};
    auto const trace = simulate({body, platform}, options(4.f));
    auto const& poses = trace.frames.back().poses;
    std::cout << "  bodyY=" << poses[0].position.y
              << " platformX=" << poses[1].position.x << '\n';
    return poses[0].position.y < 0.f;
}

NativeLayout layoutFor(std::size_t bodies) {
    NativeLayout layout;
    layout.manifestGroup = 99;
    layout.bodies.resize(bodies);
    auto& ids = layout.bodies[0];
    ids.rootGroup = 1;
    ids.controlID = 1;
    ids.gravityGroup = 2;
    ids.anchorGroup = 3;
    ids.sensorBlockIDs = {10, 11, 12, 13};
    ids.playerActionGroups = {20, 21, 22, 23};
    ids.worldActionGroups = {30, 31, 32, 33};
    ids.fragmentGroups = {40, 41};
    ids.fragmentControlIDs = {40, 41};
    return layout;
}

NativeBodyInput reactiveBody() {
    NativeBodyInput body;
    body.spec = box(Motion::Dynamic, {0.f, 100.f}, {15.f, 15.f});
    body.settings.backend = PhysicsBackend::Reactive;
    body.objectCount = 2;
    body.objectOffsets = {{-10.f, 0.f}, {10.f, 0.f}};
    return body;
}

bool autonomousPresetDoesNotWaitForPlayer(NativePreset preset) {
    auto body = reactiveBody();
    body.settings.preset = preset;
    auto const graph = buildNativeTriggerGraph({body}, layoutFor(1), -900.f);
    bool const graphUsesPlayer = std::ranges::any_of(graph.nodes, [](auto const& node) {
        return node.followPlayer1 || node.followPlayer2 ||
            (node.kind == NativeNodeKind::CollisionTrigger &&
                node.collisionPeer != CollisionPeer::Block);
    });
    bool const hasAutomaticMotion = std::ranges::any_of(graph.nodes, [](auto const& node) {
        return node.kind == NativeNodeKind::AdvancedFollow && !node.spawnTriggered &&
            (node.startSpeed > 0.f || node.acceleration > 0.f);
    });
    bool const waits = nativeNeedsPlayer(body.settings, body.spec, -900.f);
    std::cout << "  preset=" << presetName(preset) << " graphUsesPlayer=" << graphUsesPlayer
              << " automaticMotion=" << hasAutomaticMotion << " previewWaits=" << waits << '\n';
    return !graphUsesPlayer && hasAutomaticMotion && !waits;
}

bool pendulumDoesNotWaitForPlayer() {
    return autonomousPresetDoesNotWaitForPlayer(NativePreset::Pendulum);
}

bool explosionDoesNotWaitForPlayer() {
    return autonomousPresetDoesNotWaitForPlayer(NativePreset::Explosion);
}

bool reactiveOutputUsesFixtureMaterials() {
    auto body = reactiveBody();
    body.settings.targetPlayer1 = false;
    body.settings.targetPlayer2 = false;
    NativeBodyInput floor;
    floor.spec = box(Motion::Static, {}, {200.f, 10.f});
    floor.objectCount = 1;
    auto layout = layoutFor(2);
    layout.staticWorldBlockID = 50;
    auto const before = buildNativeTriggerGraph({body, floor}, layout, -900.f);
    body.spec.fixtures[0].friction = 1.f;
    body.spec.fixtures[0].restitution = 1.f;
    auto const after = buildNativeTriggerGraph({body, floor}, layout, -900.f);
    bool changed = before.nodes.size() != after.nodes.size();
    for (std::size_t i = 0; !changed && i < before.nodes.size(); ++i) {
        auto const& a = before.nodes[i];
        auto const& b = after.nodes[i];
        changed = a.friction != b.friction || a.modX != b.modX ||
            a.modY != b.modY || a.startSpeed != b.startSpeed;
    }
    std::cout << "  nativeMotionChanged=" << changed << '\n';
    return changed;
}

void describeKnownNativeLimit() {
    auto body = box(Motion::Dynamic, {0.f, 200.f}, {15.f, 15.f});
    body.velocity = {0.f, -1500.f};
    auto const floor = box(Motion::Static, {}, {200.f, 1.f});
    NativeBodySettings settings;
    settings.backend = PhysicsBackend::Reactive;
    settings.targetPlayer1 = false;
    settings.targetPlayer2 = false;
    auto const trace = simulateWorkspace({body, floor}, {settings, {}}, options(1.f));
    std::cout << "[LIMIT] reactive thin floor: finalY="
              << trace.frames.back().poses[0].position.y << " impacts=" << trace.impacts << '\n';
}

void measureStepBudget() {
    auto body = box(Motion::Dynamic, {0.f, 100.f}, {15.f, 15.f});
    body.fixtures.resize(500, body.fixtures.front());
    auto config = options(3.f);
    config.timeBudget = 0.01f;
    config.allowSleep = false;
    auto const started = std::chrono::steady_clock::now();
    auto const trace = simulate({body, body}, config);
    float const elapsed = std::chrono::duration<float>(
        std::chrono::steady_clock::now() - started
    ).count();
    // Wall time is reported, not asserted: it depends on the machine/build.
    std::cout << "[MEASURE] budget=" << config.timeBudget << " wall=" << elapsed
              << " exhausted=" << trace.exhausted << '\n';
}

} // namespace

int main(int argc, char** argv) {
    struct Check {
        char const* name;
        bool (*run)();
    };
    Check const checks[] = {
        {"unsupported body keeps falling", unsupportedBodyKeepsFalling},
        {"compound separation is not multiplied", overlappingFixturesDoNotMultiplySeparation},
        {"angular sweep detects a peg", rotatingBarDetectsItsSweptContact},
        {"fixed rotation rejects initial spin (API)", fixedRotationRejectsInitialSpin},
        {"sleeper falls after support moves (API)", sleeperFallsAfterLosingSupport},
        {"pendulum preview does not require a player", pendulumDoesNotWaitForPlayer},
        {"explosion preview does not require a player", explosionDoesNotWaitForPlayer},
        {"reactive output uses object materials", reactiveOutputUsesFixtureMaterials},
    };
    int failures = 0;
    for (auto const& check : checks) {
        bool const passed = check.run();
        std::cout << (passed ? "[PASS] " : "[FAIL] ") << check.name << '\n';
        failures += !passed;
    }
    describeKnownNativeLimit();
    if (argc > 1 && std::string_view(argv[1]) == "--measure-budget") measureStepBudget();
    std::cout << "Outstanding checks: " << failures << '/' << std::size(checks) << '\n';
    return failures == 0 ? 0 : 1;
}
