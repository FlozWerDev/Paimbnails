#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace paimon::editorphysics {

// Four corners cover a rotated block and three a slope, but a silhouette hull
// keeps the diagonal faces it traced, so it needs the extra room.
constexpr int kMaxVertices = 8;

struct Vec2 {
    float x = 0.f;
    float y = 0.f;
};

// `radius` selects a circle, `vertices` a counter-clockwise polygon relative to
// `offset`; negative friction/restitution falls back to the body's own value.
struct Fixture {
    Vec2 offset;
    Vec2 halfSize;
    float radius = 0.f;
    int vertexCount = 0;
    Vec2 vertices[kMaxVertices]{};
    float friction = -1.f;
    float restitution = -1.f;
    // Conveyor speed along the contact tangent: friction drags whatever touches
    // the face towards this speed instead of towards a standstill.
    float surfaceVelocity = 0.f;
    // A sensor reports the overlap and lets the other body pass through.
    bool sensor = false;
};

enum class Motion {
    Dynamic,
    Static,
    // Moves exactly where its velocity says and pushes everyone else without
    // ever being pushed back: lifts, saws and moving platforms.
    Kinematic,
};

constexpr std::uint32_t kDefaultCategory = 0x0001u;
constexpr std::uint32_t kAllCategories = 0xFFFFFFFFu;

// Two bodies only touch when each one's category is in the other's mask, so a
// one-sided filter never silently drops half a contact.
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
    // Negative hands the decision back to the world, so a body that was never
    // touched damps like every other one.
    float linearDamping = -1.f;
    float angularDamping = -1.f;
    // Zero leaves the speed uncapped.
    float maxSpeed = 0.f;
    float maxAngularSpeed = 0.f;
    bool fixedRotation = false;
    bool allowSleep = true;
    std::uint32_t category = kDefaultCategory;
    std::uint32_t mask = kAllCategories;
    std::vector<Fixture> fixtures;
};

constexpr std::size_t kWorldBody = static_cast<std::size_t>(-1);

enum class JointKind {
    Pin,    // both anchors are held on the same point
    Rope,   // the anchors may come closer but never stretch past `length`
    Spring, // soft pull back to `length`
    Weld,   // pin plus a locked relative angle
    Motor,  // drives the relative angle at `motorSpeed`
};

// `bodyB` may be `kWorldBody`, making `anchorB` a world point to hang from. A
// negative `length` measures from the starting pose: anchoring in one tap.
struct Joint {
    JointKind kind = JointKind::Pin;
    std::size_t bodyA = 0;
    std::size_t bodyB = kWorldBody;
    Vec2 anchorA;
    Vec2 anchorB;
    float length = -1.f;
    float stiffness = 0.5f;
    float damping = 0.2f;
    float motorSpeed = 0.f;
    float maxMotorTorque = 0.f;
    bool collideConnected = false;
};

enum class FieldKind {
    Wind,     // constant push inside the region
    Radial,   // blast away from `position`, or towards it when the strength is negative
    Vortex,   // spins around `position`
    Buoyancy, // the region behaves like water
};

// A region of `halfSize` zero covers the whole world, which is what a plain
// wind field wants.
struct ForceField {
    FieldKind kind = FieldKind::Wind;
    Vec2 position;
    Vec2 halfSize;
    Vec2 direction{1.f, 0.f};
    float strength = 0.f;
    float radius = 0.f;
    float drag = 0.f;
};

struct SimulationOptions {
    Vec2 gravity{0.f, -900.f};
    float duration = 3.f;
    float airDrag = 0.08f;
    float angularDrag = 0.25f;
    int fixedRate = 120;
    int sampleRate = 20;
    int solverIterations = 5;
    float maxSpeed = 0.f;
    bool allowSleep = true;
    bool warmStarting = true;
    // The lab solves on the main thread, so a slow capture reads as a hang;
    // zero leaves the run uncapped.
    float timeBudget = 0.f;
    std::vector<ForceField> fields;
};

struct Pose {
    Vec2 position;
    float angle = 0.f;
};

struct Frame {
    float time = 0.f;
    std::vector<Pose> poses;
};

enum class ContactPhase {
    Begin,
    End,
};

// Enough to drive a sound, a particle or a trigger off the moment two things
// met, which the impact counter alone could never do.
struct ContactEvent {
    float time = 0.f;
    std::size_t bodyA = 0;
    std::size_t bodyB = 0;
    Vec2 point;
    Vec2 normal;
    float impulse = 0.f;
    float approachSpeed = 0.f;
    bool sensor = false;
    ContactPhase phase = ContactPhase::Begin;
};

struct SimulationTrace {
    std::vector<Frame> frames;
    std::size_t impacts = 0;
    float peakImpulse = 0.f;
    // When every dynamic body fell asleep, or negative if some never did. A
    // baked trajectory can be cut here without losing any movement.
    float settleTime = -1.f;
    // The run hit `timeBudget` and the frames stop short of the duration.
    bool exhausted = false;
};

struct WorldData;

// The stateful side of the solver: step by hand, read bodies back, push them
// around. `simulate` below just runs this class to the end in one call.
class PhysicsWorld {
public:
    PhysicsWorld(
        std::vector<BodySpec> const& bodies,
        SimulationOptions const& options,
        std::vector<Joint> const& joints = {}
    );
    ~PhysicsWorld();
    PhysicsWorld(PhysicsWorld&&) noexcept;
    PhysicsWorld& operator=(PhysicsWorld&&) noexcept;

    void step(float dt);

    float time() const;
    bool settled() const;
    Frame snapshot() const;

    std::size_t impacts() const;
    float peakImpulse() const;

private:
    std::unique_ptr<WorldData> m_data;
};

SimulationTrace simulate(
    std::vector<BodySpec> const& bodies,
    SimulationOptions const& options,
    std::vector<Joint> const& joints = {}
);

} // namespace paimon::editorphysics
