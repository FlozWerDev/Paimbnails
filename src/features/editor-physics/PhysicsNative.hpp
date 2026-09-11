#pragma once

#include "PhysicsTypes.hpp"

#include <array>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace paimon::editorphysics {

// Physics Studio is only an authoring tool. Both backends below compile to
// vanilla Geometry Dash objects: Baked uses keyframes, while Reactive builds a
// trigger graph that keeps running after the player reaches it.
enum class PhysicsBackend {
    Baked,
    Reactive,
};

// Advanced Follow counts speed in blocks per second while the solver works in
// pixels, so every speed the graph writes goes through this.
constexpr float kPixelsPerSpeedUnit = 30.f;

enum class NativePreset {
    Pushable,
    Bouncy,
    Heavy,
    Floating,
    Magnet,
    Pendulum,
    Explosion,
};

struct NativeBodySettings {
    PhysicsBackend backend = PhysicsBackend::Baked;
    NativePreset preset = NativePreset::Pushable;
    float strength = 1.f;
    float sensorPadding = 6.f;
    bool targetPlayer1 = true;
    bool targetPlayer2 = true;
};

char const* backendName(PhysicsBackend backend);
char const* presetName(NativePreset preset);
NativePreset cyclePreset(NativePreset preset, int direction);

// The axis-aligned box the graph builds its Collision Blocks and its four
// sensors around.
struct NativeBounds {
    float minX = 0.f;
    float maxX = 0.f;
    float minY = 0.f;
    float maxY = 0.f;
    bool valid = false;
};

NativeBounds nativeBounds(BodySpec const& body);

struct NativeProfile {
    float gravityImpulse = 0.f;
    float pushImpulse = 0.f;
    float bounceImpulse = 0.f;
    float explosionImpulse = 0.f;
    float maxSpeed = 0.f;
    float acceleration = 0.f;
    float friction = 0.f;
    float tick = 0.05f;
    float anchorDistance = 120.f;
    bool collideWithPlayer = false;
    bool collideWithWorld = false;
    bool followPlayer = false;
    bool useAnchor = false;
    bool fragmentObjects = false;
    bool rotateToDirection = false;
};

NativeProfile nativeProfile(
    NativeBodySettings const& settings,
    BodySpec const& body,
    float worldGravity,
    float airDrag = 0.f
);

enum class SensorSide : std::size_t {
    Left = 0,
    Right = 1,
    Bottom = 2,
    Top = 3,
};

enum class NativeNodeKind {
    AdvancedFollow,
    AdvancedFollowEdit,
    CollisionBlock,
    CollisionTrigger,
    Spawn,
    Rotate,
};

enum class CollisionPeer {
    Block,
    Player1,
    Player2,
};

constexpr std::size_t kNativeAllObjects = std::numeric_limits<std::size_t>::max();

// This is deliberately a small, serializable intermediate representation. It
// contains only fields that are present in GD 2.2081's native save format; the
// emitter never needs a PlayLayer hook or a custom runtime object.
struct NativeNode {
    NativeNodeKind kind = NativeNodeKind::Spawn;
    Vec2 position;
    Vec2 size{30.f, 30.f};
    std::vector<int> groups;

    int targetGroup = 0;
    int centerGroup = 0;
    int blockID = 0;
    int blockA = 0;
    int blockB = 0;
    int controlID = 0;
    CollisionPeer collisionPeer = CollisionPeer::Block;

    float delay = 0.f;
    float startSpeed = 0.f;
    float startDirection = 0.f;
    float maxSpeed = 0.f;
    float maxRange = 0.f;
    float acceleration = 0.f;
    float friction = 0.f;
    float modX = 1.f;
    float modY = 1.f;
    float duration = 0.f;
    float rotationDegrees = 0.f;
    int times360 = 0;
    // Advanced Follow mode 2 exposes velocity, acceleration and friction.
    // Start mode follows GD's Init(0), Set(1), Add(2) selector.
    int followMode = 1;
    int startMode = 1;

    bool dynamicBlock = false;
    bool activateGroup = true;
    bool triggerOnExit = false;
    bool spawnTriggered = false;
    bool multiTriggered = false;
    bool followCenter = false;
    bool followPlayer1 = false;
    bool followPlayer2 = false;
    bool redirectDirection = false;
    bool rotateToDirection = false;
};

struct NativeBinding {
    std::size_t body = 0;
    std::size_t object = kNativeAllObjects;
    int group = 0;
    bool makeGroupParent = false;
};

struct NativeBodyInput {
    BodySpec spec;
    NativeBodySettings settings;
    std::size_t objectCount = 0;
    std::vector<Vec2> objectOffsets;
};

struct NativeBodyLayout {
    int rootGroup = 0;
    std::vector<int> fragmentGroups;
    std::array<int, 4> sensorBlockIDs{};
    std::array<int, 4> playerActionGroups{};
    std::array<int, 4> worldActionGroups{};
    int gravityGroup = 0;
    int anchorGroup = 0;
    int controlID = 0;
    std::vector<int> fragmentControlIDs;
};

struct NativeLayout {
    int manifestGroup = 0;
    int staticWorldBlockID = 0;
    Vec2 triggerOrigin;
    std::vector<NativeBodyLayout> bodies;
};

struct NativeRequirements {
    std::size_t groups = 1; // one manifest group for the generated graph
    std::size_t blocks = 0;
    std::size_t controls = 0;
    std::size_t estimatedObjects = 0;
    bool needsStaticWorld = false;
};

struct TriggerGraph {
    std::vector<NativeNode> nodes;
    std::vector<NativeBinding> bindings;
};

struct GraphValidation {
    std::vector<std::string> errors;
    std::vector<std::string> warnings;

    bool ok() const { return errors.empty(); }
};

NativeRequirements estimateNativeRequirements(
    std::vector<NativeBodyInput> const& bodies,
    float worldGravity,
    float airDrag = 0.f
);

TriggerGraph buildNativeTriggerGraph(
    std::vector<NativeBodyInput> const& bodies,
    NativeLayout const& layout,
    float worldGravity,
    float airDrag = 0.f
);

GraphValidation validateNativeTriggerGraph(
    TriggerGraph const& graph,
    std::vector<NativeBodyInput> const& bodies
);

} // namespace paimon::editorphysics
