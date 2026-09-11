#include "../src/features/editor-physics/NativeTriggerCatalog.hpp"
#include "../src/features/editor-physics/PhysicsNative.hpp"
#include "../src/features/editor-physics/PhysicsNative.cpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

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

bool containsKind(TriggerGraph const& graph, NativeNodeKind kind, std::size_t atLeast) {
    return static_cast<std::size_t>(std::count_if(
        graph.nodes.begin(), graph.nodes.end(),
        [&](NativeNode const& node) { return node.kind == kind; }
    )) >= atLeast;
}

bool containsDirection(
    TriggerGraph const& graph,
    NativeNodeKind kind,
    float direction,
    float epsilon = 0.001f
) {
    return std::ranges::any_of(graph.nodes, [&](NativeNode const& node) {
        return node.kind == kind && std::abs(node.startDirection - direction) <= epsilon;
    });
}

bool pushableBuildsPlayerAndWorldSensors() {
    NativeBodyInput dynamic;
    dynamic.settings.backend = PhysicsBackend::Reactive;
    dynamic.spec = box(Motion::Dynamic, {0.f, 80.f}, {15.f, 15.f});
    dynamic.objectCount = 1;
    dynamic.settings.preset = NativePreset::Pushable;

    NativeBodyInput floor;
    floor.settings.backend = PhysicsBackend::Reactive;
    floor.spec = box(Motion::Static, {0.f, 0.f}, {150.f, 10.f});
    floor.objectCount = 1;

    std::vector<NativeBodyInput> bodies{dynamic, floor};
    NativeLayout layout;
    layout.manifestGroup = 99;
    layout.staticWorldBlockID = 50;
    layout.triggerOrigin = {-180.f, -60.f};
    layout.bodies.resize(2);
    auto& ids = layout.bodies[0];
    ids.rootGroup = 1;
    ids.controlID = 70;
    ids.gravityGroup = 30;
    ids.sensorBlockIDs = {51, 52, 53, 54};
    ids.playerActionGroups = {10, 11, 12, 13};
    ids.worldActionGroups = {20, 21, 22, 23};

    auto requirements = estimateNativeRequirements(bodies, -900.f);
    auto graph = buildNativeTriggerGraph(bodies, layout, -900.f);
    auto validation = validateNativeTriggerGraph(graph, bodies);
    bool const synchronized = std::ranges::all_of(graph.nodes, [&](NativeNode const& node) {
        return node.spawnTriggered || node.kind == NativeNodeKind::CollisionBlock ||
            node.position.x == layout.triggerOrigin.x;
    });
    bool const pass = synchronized && validation.ok() &&
        requirements.groups == 11 && requirements.blocks == 5 &&
        requirements.controls == 1 &&
        requirements.estimatedObjects == graph.nodes.size() &&
        containsKind(graph, NativeNodeKind::AdvancedFollow, 1) &&
        containsKind(graph, NativeNodeKind::AdvancedFollowEdit, 9) &&
        containsKind(graph, NativeNodeKind::CollisionBlock, 5) &&
        containsKind(graph, NativeNodeKind::CollisionTrigger, 12) &&
        containsKind(graph, NativeNodeKind::Spawn, 2) &&
        containsDirection(graph, NativeNodeKind::AdvancedFollowEdit, 180.f);
    std::cout << "native-pushable: nodes=" << graph.nodes.size()
              << " errors=" << validation.errors.size() << '\n';
    return pass;
}

bool explosionCreatesIndependentFragmentGroups() {
    NativeBodyInput debris;
    debris.settings.backend = PhysicsBackend::Reactive;
    debris.spec = box(Motion::Dynamic, {40.f, 60.f}, {30.f, 20.f});
    debris.objectCount = 3;
    debris.objectOffsets = {{-20.f, 0.f}, {20.f, 0.f}, {0.f, 15.f}};
    debris.settings.preset = NativePreset::Explosion;

    NativeLayout layout;
    layout.manifestGroup = 99;
    layout.triggerOrigin = {-120.f, -30.f};
    layout.bodies.resize(1);
    auto& ids = layout.bodies[0];
    ids.rootGroup = 1;
    ids.fragmentGroups = {2, 3, 4};
    ids.fragmentControlIDs = {80, 81, 82};
    ids.gravityGroup = 5;

    std::vector<NativeBodyInput> bodies{debris};
    auto requirements = estimateNativeRequirements(bodies, -900.f);
    auto graph = buildNativeTriggerGraph(bodies, layout, -900.f);
    auto validation = validateNativeTriggerGraph(graph, bodies);
    std::size_t fragmentBindings = static_cast<std::size_t>(std::count_if(
        graph.bindings.begin(), graph.bindings.end(),
        [](NativeBinding const& binding) { return binding.object != kNativeAllObjects; }
    ));
    bool const pass = validation.ok() && fragmentBindings == 3 &&
        requirements.groups == 5 && requirements.blocks == 0 &&
        requirements.controls == 3 &&
        requirements.estimatedObjects == graph.nodes.size() &&
        containsKind(graph, NativeNodeKind::AdvancedFollow, 3) &&
        containsKind(graph, NativeNodeKind::AdvancedFollowEdit, 3) &&
        containsKind(graph, NativeNodeKind::Spawn, 2);
    std::cout << "native-explosion: nodes=" << graph.nodes.size()
              << " fragments=" << fragmentBindings << '\n';
    return pass;
}

bool invalidMagnetNeedsPlayerTarget() {
    NativeBodyInput magnet;
    magnet.settings.backend = PhysicsBackend::Reactive;
    magnet.spec = box(Motion::Dynamic, {0.f, 0.f}, {15.f, 15.f});
    magnet.objectCount = 1;
    magnet.settings.preset = NativePreset::Magnet;
    magnet.settings.targetPlayer1 = false;
    magnet.settings.targetPlayer2 = false;

    NativeLayout layout;
    layout.manifestGroup = 99;
    layout.triggerOrigin = {-90.f, -30.f};
    layout.bodies.resize(1);
    layout.bodies[0].rootGroup = 1;
    layout.bodies[0].controlID = 70;

    std::vector<NativeBodyInput> bodies{magnet};
    auto graph = buildNativeTriggerGraph(bodies, layout, -900.f);
    auto validation = validateNativeTriggerGraph(graph, bodies);
    std::cout << "native-invalid-magnet: errors=" << validation.errors.size() << '\n';
    return !validation.ok();
}

bool pendulumBuildsNativeAnchor() {
    NativeBodyInput pendulum;
    pendulum.settings.backend = PhysicsBackend::Reactive;
    pendulum.spec = box(Motion::Dynamic, {30.f, 40.f}, {15.f, 15.f});
    pendulum.objectCount = 1;
    pendulum.settings.preset = NativePreset::Pendulum;

    NativeLayout layout;
    layout.manifestGroup = 99;
    layout.triggerOrigin = {-90.f, -30.f};
    layout.bodies.resize(1);
    auto& ids = layout.bodies[0];
    ids.rootGroup = 1;
    ids.controlID = 70;
    ids.anchorGroup = 2;
    ids.sensorBlockIDs[0] = 50;

    std::vector<NativeBodyInput> bodies{pendulum};
    auto requirements = estimateNativeRequirements(bodies, -900.f);
    auto graph = buildNativeTriggerGraph(bodies, layout, -900.f);
    auto validation = validateNativeTriggerGraph(graph, bodies);
    bool const targetsAnchor = std::ranges::any_of(graph.nodes, [](NativeNode const& node) {
        return node.kind == NativeNodeKind::AdvancedFollow &&
            node.centerGroup == 2 && node.maxRange > 0.f;
    });
    return validation.ok() && targetsAnchor && requirements.groups == 3 &&
        requirements.blocks == 1 && requirements.controls == 1 &&
        containsKind(graph, NativeNodeKind::CollisionBlock, 1);
}

bool disabledPlayersDoNotAllocateSensors() {
    NativeBodyInput body;
    body.settings.backend = PhysicsBackend::Reactive;
    body.spec = box(Motion::Dynamic, {0.f, 0.f}, {15.f, 15.f});
    body.objectCount = 1;
    body.settings.preset = NativePreset::Pushable;
    body.settings.targetPlayer1 = false;
    body.settings.targetPlayer2 = false;

    std::vector<NativeBodyInput> bodies{body};
    auto requirements = estimateNativeRequirements(bodies, -900.f);
    return requirements.groups == 3 && requirements.blocks == 0 &&
        requirements.controls == 1 && requirements.estimatedObjects == 4;
}

bool materialParametersReachNativeProfile() {
    BodySpec light = box(Motion::Dynamic, {0.f, 0.f}, {15.f, 15.f});
    light.mass = 1.f;
    light.friction = 0.2f;
    BodySpec heavy = light;
    heavy.mass = 16.f;
    NativeBodySettings settings;
    auto const lightProfile = nativeProfile(settings, light, -900.f, 0.f);
    auto const heavyProfile = nativeProfile(settings, heavy, -900.f, 0.6f);
    return heavyProfile.pushImpulse < lightProfile.pushImpulse &&
        heavyProfile.friction > lightProfile.friction &&
        heavyProfile.gravityImpulse == lightProfile.gravityImpulse;
}

bool negativeGravityScalePointsUp() {
    NativeBodyInput body;
    body.settings.backend = PhysicsBackend::Reactive;
    body.spec = box(Motion::Dynamic, {0.f, 0.f}, {15.f, 15.f});
    body.spec.gravityScale = -1.f;
    body.objectCount = 1;
    body.settings.targetPlayer1 = false;
    body.settings.targetPlayer2 = false;

    NativeLayout layout;
    layout.manifestGroup = 99;
    layout.triggerOrigin = {-90.f, -30.f};
    layout.bodies.resize(1);
    layout.bodies[0].rootGroup = 1;
    layout.bodies[0].controlID = 70;
    layout.bodies[0].gravityGroup = 2;

    std::vector<NativeBodyInput> bodies{body};
    auto graph = buildNativeTriggerGraph(bodies, layout, -900.f);
    auto validation = validateNativeTriggerGraph(graph, bodies);
    return validation.ok() &&
        containsDirection(graph, NativeNodeKind::AdvancedFollowEdit, 0.f);
}

bool nativeSpinMatchesCocosDirection() {
    NativeBodyInput body;
    body.settings.backend = PhysicsBackend::Reactive;
    body.spec = box(Motion::Dynamic, {0.f, 0.f}, {15.f, 15.f});
    body.spec.gravityScale = 0.f;
    body.spec.angularVelocity = 3.14159265358979323846f;
    body.objectCount = 1;
    body.settings.targetPlayer1 = false;
    body.settings.targetPlayer2 = false;

    NativeLayout layout;
    layout.manifestGroup = 99;
    layout.triggerOrigin = {-90.f, -30.f};
    layout.bodies.resize(1);
    layout.bodies[0].rootGroup = 1;
    layout.bodies[0].controlID = 70;

    std::vector<NativeBodyInput> bodies{body};
    auto graph = buildNativeTriggerGraph(bodies, layout, -900.f);
    auto validation = validateNativeTriggerGraph(graph, bodies);
    return validation.ok() && std::ranges::any_of(graph.nodes, [](NativeNode const& node) {
        return node.kind == NativeNodeKind::Rotate &&
            (node.times360 < 0 || node.rotationDegrees < 0.f);
    });
}

bool rejectsUnsafeSpawnCycle() {
    NativeBodyInput body;
    body.settings.backend = PhysicsBackend::Reactive;
    body.spec = box(Motion::Dynamic, {0.f, 0.f}, {15.f, 15.f});
    body.objectCount = 1;
    TriggerGraph graph;
    graph.bindings.push_back({0, kNativeAllObjects, 1, true});
    NativeNode follow;
    follow.kind = NativeNodeKind::AdvancedFollow;
    follow.targetGroup = 1;
    follow.controlID = 2;
    follow.followCenter = true;
    graph.nodes.push_back(follow);
    NativeNode loop;
    loop.kind = NativeNodeKind::Spawn;
    loop.groups.push_back(5);
    loop.targetGroup = 5;
    loop.delay = 0.f;
    loop.spawnTriggered = true;
    loop.multiTriggered = true;
    graph.nodes.push_back(loop);
    auto validation = validateNativeTriggerGraph(graph, {body});
    return !validation.ok();
}

bool catalogMatchesEmitterIDs() {
    auto const* follow = nativeTriggerInfo(nativeids::AdvancedFollow);
    auto const* edit = nativeTriggerInfo(nativeids::AdvancedFollowEdit);
    auto const* collision = nativeTriggerInfo(nativeids::Collision);
    auto const* block = nativeTriggerInfo(nativeids::CollisionBlock);
    return follow && edit && collision && block && follow->isTrigger && !block->isTrigger;
}

} // namespace

int main() {
    struct Case {
        char const* name;
        bool (*run)();
    };
    Case const cases[] = {
        {"pushable trigger graph", pushableBuildsPlayerAndWorldSensors},
        {"explosion fragment groups", explosionCreatesIndependentFragmentGroups},
        {"magnet target validation", invalidMagnetNeedsPlayerTarget},
        {"pendulum native anchor", pendulumBuildsNativeAnchor},
        {"disabled player sensors", disabledPlayersDoNotAllocateSensors},
        {"native material parameters", materialParametersReachNativeProfile},
        {"negative gravity direction", negativeGravityScalePointsUp},
        {"native spin direction", nativeSpinMatchesCocosDirection},
        {"spawn cycle validation", rejectsUnsafeSpawnCycle},
        {"2.2081 trigger catalog", catalogMatchesEmitterIDs},
    };
    bool passed = true;
    for (auto const& test : cases) {
        bool const result = test.run();
        std::cout << (result ? "[PASS] " : "[FAIL] ") << test.name << '\n';
        passed &= result;
    }
    return passed ? 0 : 1;
}
