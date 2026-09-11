#include "../PhysicsTypes.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace paimon::editorphysics {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kSlop = 0.05f;
constexpr float kCorrection = 0.7f;
constexpr float kRestingSpeed = 45.f;
constexpr int kMaxSubsteps = 16;
constexpr float kSleepSpeed = 8.f;
constexpr float kSleepSpin = 0.12f;
constexpr float kSleepDelay = 0.5f;
constexpr float kWakeSpeed = 24.f;
constexpr float kJointCorrection = 0.35f;
constexpr std::size_t kMaxContactEvents = 4096;

Vec2 operator+(Vec2 a, Vec2 b) {
    return {a.x + b.x, a.y + b.y};
}

Vec2 operator-(Vec2 a, Vec2 b) {
    return {a.x - b.x, a.y - b.y};
}

Vec2 operator*(Vec2 value, float scalar) {
    return {value.x * scalar, value.y * scalar};
}

Vec2 operator/(Vec2 value, float scalar) {
    return {value.x / scalar, value.y / scalar};
}

Vec2& operator+=(Vec2& a, Vec2 b) {
    a.x += b.x;
    a.y += b.y;
    return a;
}

Vec2& operator-=(Vec2& a, Vec2 b) {
    a.x -= b.x;
    a.y -= b.y;
    return a;
}

float dot(Vec2 a, Vec2 b) {
    return a.x * b.x + a.y * b.y;
}

float cross(Vec2 a, Vec2 b) {
    return a.x * b.y - a.y * b.x;
}

Vec2 angularVelocityAt(float angularVelocity, Vec2 radius) {
    return {-angularVelocity * radius.y, angularVelocity * radius.x};
}

Vec2 rotate(Vec2 point, float angle) {
    float const cosine = std::cos(angle);
    float const sine = std::sin(angle);
    return {
        point.x * cosine - point.y * sine,
        point.x * sine + point.y * cosine,
    };
}

float lengthSquared(Vec2 value) {
    return dot(value, value);
}

float length(Vec2 value) {
    return std::sqrt(lengthSquared(value));
}

Vec2 normalized(Vec2 value) {
    float const size = length(value);
    return size > 0.00001f ? value / size : Vec2{};
}

struct State {
    Vec2 position;
    Vec2 velocity;
    float angle = 0.f;
    float angularVelocity = 0.f;
    float inverseMass = 0.f;
    float inverseInertia = 0.f;
    float boundingRadius = 0.f;
    float linearDamping = 0.f;
    float angularDamping = 0.f;
    float maxSpeed = 0.f;
    float maxAngularSpeed = 0.f;
    Motion motion = Motion::Static;
    bool allowSleep = true;
    bool asleep = false;
    float sleepTimer = 0.f;
};

bool isDynamic(State const& state) {
    return state.motion == Motion::Dynamic;
}

bool isMovable(State const& state) {
    return state.motion != Motion::Static;
}

// A fixture placed in the world: a convex polygon of `count` vertices, or a
// circle when `count` is zero. The solver never looks at the axis-aligned size
// again from here on, so a rotated block collides on its real corners and a
// slope on its real hypotenuse.
struct Shape {
    Vec2 center;
    Vec2 points[kMaxVertices];
    int count = 0;
    float radius = 0.f;
};

struct Manifold {
    Vec2 normal;
    Vec2 points[2];
    float penetration[2];
    int count = 0;
};

Vec2 edgeNormal(Vec2 from, Vec2 to) {
    Vec2 const edge = to - from;
    return normalized({edge.y, -edge.x});
}

Vec2 faceNormalOf(Shape const& shape, int face) {
    return edgeNormal(shape.points[face], shape.points[(face + 1) % shape.count]);
}

Shape worldFixture(State const& state, Fixture const& fixture) {
    Shape shape;
    shape.center = state.position + rotate(fixture.offset, state.angle);
    if (fixture.radius > 0.f) {
        shape.radius = fixture.radius;
        return shape;
    }
    if (fixture.vertexCount >= 3) {
        shape.count = std::min(fixture.vertexCount, kMaxVertices);
        for (int i = 0; i < shape.count; ++i) {
            shape.points[i] = shape.center + rotate(fixture.vertices[i], state.angle);
        }
        return shape;
    }
    Vec2 const ex = rotate({fixture.halfSize.x, 0.f}, state.angle);
    Vec2 const ey = rotate({0.f, fixture.halfSize.y}, state.angle);
    shape.count = 4;
    shape.points[0] = shape.center - ex - ey;
    shape.points[1] = shape.center + ex - ey;
    shape.points[2] = shape.center + ex + ey;
    shape.points[3] = shape.center - ex + ey;
    return shape;
}

// Deepest overlap along the edge normals of `shape`, in the separating-axis
// sense: positive means the two are apart along that axis.
struct FaceQuery {
    float separation = -std::numeric_limits<float>::max();
    int face = 0;
};

FaceQuery deepestFace(Shape const& shape, Shape const& other) {
    FaceQuery best;
    for (int i = 0; i < shape.count; ++i) {
        Vec2 const normal = faceNormalOf(shape, i);
        float separation = std::numeric_limits<float>::max();
        for (int j = 0; j < other.count; ++j) {
            separation = std::min(separation, dot(normal, other.points[j] - shape.points[i]));
        }
        if (separation > best.separation) {
            best.separation = separation;
            best.face = i;
        }
    }
    return best;
}

int incidentFace(Shape const& shape, Vec2 referenceNormal) {
    int best = 0;
    float bestDot = std::numeric_limits<float>::max();
    for (int i = 0; i < shape.count; ++i) {
        float const value = dot(faceNormalOf(shape, i), referenceNormal);
        if (value < bestDot) {
            bestDot = value;
            best = i;
        }
    }
    return best;
}

Vec2 closestPointOnSegment(Vec2 from, Vec2 to, Vec2 point) {
    Vec2 const edge = to - from;
    float const lengthSq = lengthSquared(edge);
    if (lengthSq < 0.00001f) return from;
    float const along = std::clamp(dot(point - from, edge) / lengthSq, 0.f, 1.f);
    return from + edge * along;
}

int clipSegment(Vec2 const in[2], Vec2 normal, float limit, Vec2 out[2]) {
    float const first = dot(in[0], normal) - limit;
    float const second = dot(in[1], normal) - limit;
    int count = 0;
    if (first <= 0.f) out[count++] = in[0];
    if (second <= 0.f) out[count++] = in[1];
    if (first * second < 0.f && count < 2) {
        out[count++] = in[0] + (in[1] - in[0]) * (first / (first - second));
    }
    return count;
}

// Separating axis test followed by reference/incident face clipping, so a box
// resting flat reports both of its corners instead of rocking on a single point.
// The manifold normal always points from `a` towards `b`.
bool collidePolygons(Shape const& a, Shape const& b, Manifold& manifold) {
    FaceQuery const queryA = deepestFace(a, b);
    if (queryA.separation > 0.f) return false;
    FaceQuery const queryB = deepestFace(b, a);
    if (queryB.separation > 0.f) return false;

    // Ties go to A so a body sliding along a flat floor keeps the same reference
    // face frame by frame instead of flickering between the two.
    bool const referenceIsB = queryB.separation > queryA.separation + 0.001f;
    Shape const& reference = referenceIsB ? b : a;
    Shape const& incident = referenceIsB ? a : b;
    int const referenceFace = referenceIsB ? queryB.face : queryA.face;

    Vec2 const start = reference.points[referenceFace];
    Vec2 const end = reference.points[(referenceFace + 1) % reference.count];
    Vec2 const faceNormal = edgeNormal(start, end);
    Vec2 const tangent = normalized(end - start);

    int const clipFace = incidentFace(incident, faceNormal);
    Vec2 segment[2] = {
        incident.points[clipFace],
        incident.points[(clipFace + 1) % incident.count],
    };
    Vec2 clipped[2];
    if (clipSegment(segment, tangent * -1.f, dot(start, tangent * -1.f), clipped) < 2) return false;
    segment[0] = clipped[0];
    segment[1] = clipped[1];
    if (clipSegment(segment, tangent, dot(end, tangent), clipped) < 2) return false;

    manifold.normal = referenceIsB ? faceNormal * -1.f : faceNormal;
    manifold.count = 0;
    float const plane = dot(start, faceNormal);
    for (int i = 0; i < 2; ++i) {
        float const separation = dot(clipped[i], faceNormal) - plane;
        if (separation > 0.f) continue;
        manifold.points[manifold.count] = clipped[i];
        manifold.penetration[manifold.count] = -separation;
        ++manifold.count;
    }
    if (manifold.count == 0) {
        manifold.points[0] = (a.center + b.center) * 0.5f;
        manifold.penetration[0] = -std::max(queryA.separation, queryB.separation);
        manifold.count = 1;
    }
    return true;
}

bool collideCirclePolygon(
    Shape const& circle,
    Shape const& polygon,
    bool circleIsFirst,
    Manifold& manifold
) {
    int deepest = 0;
    float separation = -std::numeric_limits<float>::max();
    for (int i = 0; i < polygon.count; ++i) {
        float const value = dot(faceNormalOf(polygon, i), circle.center - polygon.points[i]);
        if (value > separation) {
            separation = value;
            deepest = i;
        }
    }
    if (separation > circle.radius) return false;

    Vec2 const from = polygon.points[deepest];
    Vec2 const to = polygon.points[(deepest + 1) % polygon.count];
    Vec2 const contact = closestPointOnSegment(from, to, circle.center);
    // Once the centre is inside the polygon the closest edge point is the only
    // stable direction left; pushing along the face normal keeps a sunken orb
    // from popping out of the wrong side.
    Vec2 normal = faceNormalOf(polygon, deepest);
    float penetration = circle.radius - separation;
    if (separation >= 0.f) {
        Vec2 const away = circle.center - contact;
        float const distance = length(away);
        if (distance > circle.radius) return false;
        if (distance > 0.00001f) normal = away / distance;
        penetration = circle.radius - distance;
    }

    manifold.normal = circleIsFirst ? normal * -1.f : normal;
    manifold.points[0] = contact;
    manifold.penetration[0] = std::max(penetration, 0.f);
    manifold.count = 1;
    return true;
}

bool collideCircles(Shape const& a, Shape const& b, Manifold& manifold) {
    Vec2 const delta = b.center - a.center;
    float const distance = length(delta);
    float const reach = a.radius + b.radius;
    if (distance >= reach) return false;
    Vec2 const normal = distance > 0.00001f ? delta / distance : Vec2{0.f, 1.f};
    manifold.normal = normal;
    manifold.points[0] = a.center + normal * (a.radius - (reach - distance) * 0.5f);
    manifold.penetration[0] = reach - distance;
    manifold.count = 1;
    return true;
}

bool buildManifold(Shape const& a, Shape const& b, Manifold& manifold) {
    bool const circleA = a.count == 0;
    bool const circleB = b.count == 0;
    if (circleA && circleB) return collideCircles(a, b, manifold);
    if (circleA) return collideCirclePolygon(a, b, true, manifold);
    if (circleB) return collideCirclePolygon(b, a, false, manifold);
    return collidePolygons(a, b, manifold);
}

// A fixture only overrides its body when it was given a value of its own.
float frictionOf(BodySpec const& body, Fixture const& fixture) {
    return std::max(0.f, fixture.friction >= 0.f ? fixture.friction : body.friction);
}

float restitutionOf(BodySpec const& body, Fixture const& fixture) {
    return std::max(0.f, fixture.restitution >= 0.f ? fixture.restitution : body.restitution);
}

// What the fixture weighs and how hard it is to spin, taken from the shape the
// solver actually collides with: a disc resists half of what its bounding box
// would, and a slope holds its mass in the corner it fills.
struct MassShape {
    float area = 0.f;
    Vec2 centroid;
    float inertia = 0.f;
};

MassShape massShapeOf(Fixture const& fixture) {
    MassShape result;
    if (fixture.radius > 0.f) {
        result.area = kPi * fixture.radius * fixture.radius;
        result.inertia = fixture.radius * fixture.radius * 0.5f;
        return result;
    }
    int const count = std::min(fixture.vertexCount, kMaxVertices);
    if (count >= 3) {
        float twiceArea = 0.f;
        float moment = 0.f;
        Vec2 weighted{};
        for (int i = 0; i < count; ++i) {
            Vec2 const current = fixture.vertices[i];
            Vec2 const next = fixture.vertices[(i + 1) % count];
            float const step = cross(current, next);
            twiceArea += step;
            weighted += (current + next) * step;
            moment += step *
                (lengthSquared(current) + dot(current, next) + lengthSquared(next));
        }
        if (std::abs(twiceArea) > 0.0001f) {
            result.area = std::abs(twiceArea) * 0.5f;
            result.centroid = weighted / (3.f * twiceArea);
            result.inertia = std::max(
                moment / (6.f * twiceArea) - lengthSquared(result.centroid), 0.f
            );
            return result;
        }
    }
    float const width = fixture.halfSize.x * 2.f;
    float const height = fixture.halfSize.y * 2.f;
    result.area = width * height;
    result.inertia = (width * width + height * height) / 12.f;
    return result;
}

float bodyInertia(BodySpec const& body, float mass) {
    float totalArea = 0.f;
    for (auto const& fixture : body.fixtures) {
        totalArea += std::max(1.f, massShapeOf(fixture).area);
    }
    if (totalArea <= 0.f) return mass;

    float inertia = 0.f;
    for (auto const& fixture : body.fixtures) {
        auto const shape = massShapeOf(fixture);
        float const partMass = mass * std::max(1.f, shape.area) / totalArea;
        Vec2 const arm = fixture.offset + shape.centroid;
        inertia += partMass * (shape.inertia + lengthSquared(arm));
    }
    return std::max(inertia, 0.001f);
}

float fixtureReach(Fixture const& fixture) {
    if (fixture.radius > 0.f) return fixture.radius;
    // A silhouette hull carries up to eight corners; stopping at four used to
    // leave the last ones outside the bounding radius and drop their contacts.
    if (fixture.vertexCount < 3) return length(fixture.halfSize);
    float reach = 0.f;
    for (int i = 0; i < std::min(fixture.vertexCount, kMaxVertices); ++i) {
        reach = std::max(reach, length(fixture.vertices[i]));
    }
    return reach;
}

float boundingRadius(BodySpec const& body) {
    float radius = 0.f;
    for (auto const& fixture : body.fixtures) {
        radius = std::max(radius, length(fixture.offset) + fixtureReach(fixture));
    }
    return radius;
}

// Rotation-invariant, so a body that spins never needs its bounds rebuilt.
struct Bounds {
    Vec2 min;
    Vec2 max;
};

Bounds fixtureBounds(State const& state, Fixture const& fixture) {
    Vec2 const center = state.position + rotate(fixture.offset, state.angle);
    float const reach = fixtureReach(fixture);
    return {{center.x - reach, center.y - reach}, {center.x + reach, center.y + reach}};
}

bool boundsOverlap(Bounds const& a, Bounds const& b, float margin) {
    return a.min.x - margin <= b.max.x && a.max.x + margin >= b.min.x &&
        a.min.y - margin <= b.max.y && a.max.y + margin >= b.min.y;
}

bool filtersMatch(BodySpec const& a, BodySpec const& b) {
    return (a.category & b.mask) != 0u && (b.category & a.mask) != 0u;
}

void applyImpulseTo(State& state, Vec2 impulse, Vec2 radius, float direction) {
    if (state.inverseMass <= 0.f) return;
    state.velocity += impulse * (state.inverseMass * direction);
    state.angularVelocity += cross(radius, impulse) * state.inverseInertia * direction;
}

struct ContactPoint {
    Vec2 point;
    float penetration = 0.f;
    float bias = 0.f;
    float normalImpulse = 0.f;
    float tangentImpulse = 0.f;
    std::uint64_t key = 0;
};

// One fixture-pair contact, built once per substep and then relaxed over several
// iterations. Rebuilding it inside the iteration loop, as an earlier solver did,
// made restitution decay against its own output and left bounces far too weak.
struct Constraint {
    std::size_t a = 0;
    std::size_t b = 0;
    Vec2 normal;
    ContactPoint points[2];
    int count = 0;
    float restitution = 0.f;
    float friction = 0.f;
    float surfaceVelocity = 0.f;
};

// Carrying last substep's impulses into the next one is what lets a stack stand
// still instead of sinking a little further on every rebuild.
struct CachedImpulse {
    float normalImpulse = 0.f;
    float tangentImpulse = 0.f;
};

std::uint64_t pairKey(std::size_t a, std::size_t b) {
    return (static_cast<std::uint64_t>(a) << 32) | static_cast<std::uint64_t>(b);
}

std::uint64_t contactKey(
    std::size_t bodyA,
    std::size_t fixtureA,
    std::size_t bodyB,
    std::size_t fixtureB,
    int point
) {
    return (static_cast<std::uint64_t>(bodyA & 0xFFFFu) << 48) |
        (static_cast<std::uint64_t>(fixtureA & 0x3FFFu) << 34) |
        (static_cast<std::uint64_t>(bodyB & 0xFFFFu) << 18) |
        (static_cast<std::uint64_t>(fixtureB & 0x3FFFu) << 4) |
        static_cast<std::uint64_t>(point & 0x3);
}

Vec2 relativeVelocityAt(State const& a, State const& b, Vec2 radiusA, Vec2 radiusB) {
    return b.velocity + angularVelocityAt(b.angularVelocity, radiusB) -
        a.velocity - angularVelocityAt(a.angularVelocity, radiusA);
}

void prepareConstraint(Constraint& constraint, State const& a, State const& b) {
    for (int i = 0; i < constraint.count; ++i) {
        auto& contact = constraint.points[i];
        Vec2 const radiusA = contact.point - a.position;
        Vec2 const radiusB = contact.point - b.position;
        float const approach =
            dot(relativeVelocityAt(a, b, radiusA, radiusB), constraint.normal);
        // Restitution is locked in from the approach speed before any impulse lands,
        // and a body that is merely settling gets none so it can come to rest.
        contact.bias = -approach > kRestingSpeed ? -constraint.restitution * approach : 0.f;
    }
}

void warmStartConstraint(Constraint const& constraint, State& a, State& b) {
    Vec2 const tangent{-constraint.normal.y, constraint.normal.x};
    for (int i = 0; i < constraint.count; ++i) {
        auto const& contact = constraint.points[i];
        Vec2 const impulse =
            constraint.normal * contact.normalImpulse + tangent * contact.tangentImpulse;
        applyImpulseTo(a, impulse, contact.point - a.position, -1.f);
        applyImpulseTo(b, impulse, contact.point - b.position, 1.f);
    }
}

float solveConstraint(Constraint& constraint, State& a, State& b) {
    float const inverseMassSum = a.inverseMass + b.inverseMass;
    if (inverseMassSum <= 0.f) return 0.f;
    Vec2 const tangent{-constraint.normal.y, constraint.normal.x};
    float peak = 0.f;

    for (int i = 0; i < constraint.count; ++i) {
        auto& contact = constraint.points[i];
        Vec2 const radiusA = contact.point - a.position;
        Vec2 const radiusB = contact.point - b.position;

        float const radiusNormalA = cross(radiusA, constraint.normal);
        float const radiusNormalB = cross(radiusB, constraint.normal);
        float const normalDenominator = inverseMassSum +
            radiusNormalA * radiusNormalA * a.inverseInertia +
            radiusNormalB * radiusNormalB * b.inverseInertia;
        if (normalDenominator > 0.00001f) {
            float const speed =
                dot(relativeVelocityAt(a, b, radiusA, radiusB), constraint.normal);
            float const wanted = -(speed - contact.bias) / normalDenominator;
            float const previous = contact.normalImpulse;
            contact.normalImpulse = std::max(previous + wanted, 0.f);
            float const applied = contact.normalImpulse - previous;
            Vec2 const impulse = constraint.normal * applied;
            applyImpulseTo(a, impulse, radiusA, -1.f);
            applyImpulseTo(b, impulse, radiusB, 1.f);
            peak = std::max(peak, contact.normalImpulse);
        }

        float const radiusTangentA = cross(radiusA, tangent);
        float const radiusTangentB = cross(radiusB, tangent);
        float const tangentDenominator = inverseMassSum +
            radiusTangentA * radiusTangentA * a.inverseInertia +
            radiusTangentB * radiusTangentB * b.inverseInertia;
        if (tangentDenominator <= 0.00001f) continue;

        // A conveyor face does not brake what it touches, it drags it towards the
        // belt speed, so the target the friction aims at is shifted instead of zero.
        float const slide = dot(relativeVelocityAt(a, b, radiusA, radiusB), tangent) -
            constraint.surfaceVelocity;
        float const limit = constraint.friction * contact.normalImpulse;
        float const previous = contact.tangentImpulse;
        contact.tangentImpulse =
            std::clamp(previous - slide / tangentDenominator, -limit, limit);
        Vec2 const impulse = tangent * (contact.tangentImpulse - previous);
        applyImpulseTo(a, impulse, radiusA, -1.f);
        applyImpulseTo(b, impulse, radiusB, 1.f);
    }
    return peak;
}

void separateConstraint(Constraint const& constraint, State& a, State& b) {
    float const inverseMassSum = a.inverseMass + b.inverseMass;
    if (inverseMassSum <= 0.f) return;
    float deepest = 0.f;
    for (int i = 0; i < constraint.count; ++i) {
        deepest = std::max(deepest, constraint.points[i].penetration);
    }
    Vec2 const correction =
        constraint.normal * (std::max(deepest - kSlop, 0.f) * kCorrection / inverseMassSum);
    a.position -= correction * a.inverseMass;
    b.position += correction * b.inverseMass;
}

// Where a joint end sits right now, and the arm from the body centre to it. A
// world anchor has no body, so its arm is zero and it never moves.
struct JointEnd {
    State* state = nullptr;
    Vec2 point;
    Vec2 arm;
};

JointEnd jointEnd(std::vector<State>& states, std::size_t body, Vec2 anchor) {
    JointEnd end;
    if (body >= states.size()) {
        end.point = anchor;
        return end;
    }
    end.state = &states[body];
    end.arm = rotate(anchor, end.state->angle);
    end.point = end.state->position + end.arm;
    return end;
}

void applyJointImpulse(JointEnd& end, Vec2 impulse, float direction) {
    if (!end.state) return;
    applyImpulseTo(*end.state, impulse, end.arm, direction);
}

Vec2 jointVelocity(JointEnd const& end) {
    if (!end.state) return {};
    return end.state->velocity + angularVelocityAt(end.state->angularVelocity, end.arm);
}

// Both ends pulled onto one point: a 2x2 effective mass, because the two axes of
// a pin are coupled through each body's inertia.
void solvePinAxes(JointEnd& a, JointEnd& b, Vec2 target) {
    float const inverseMassA = a.state ? a.state->inverseMass : 0.f;
    float const inverseMassB = b.state ? b.state->inverseMass : 0.f;
    float const inertiaA = a.state ? a.state->inverseInertia : 0.f;
    float const inertiaB = b.state ? b.state->inverseInertia : 0.f;
    float const massSum = inverseMassA + inverseMassB;
    if (massSum <= 0.f) return;

    float const k11 = massSum + inertiaA * a.arm.y * a.arm.y + inertiaB * b.arm.y * b.arm.y;
    float const k12 = -inertiaA * a.arm.x * a.arm.y - inertiaB * b.arm.x * b.arm.y;
    float const k22 = massSum + inertiaA * a.arm.x * a.arm.x + inertiaB * b.arm.x * b.arm.x;
    float const determinant = k11 * k22 - k12 * k12;
    if (std::abs(determinant) < 0.000001f) return;

    Vec2 const relative = jointVelocity(b) - jointVelocity(a) - target;
    Vec2 const impulse{
        -(k22 * relative.x - k12 * relative.y) / determinant,
        -(k11 * relative.y - k12 * relative.x) / determinant,
    };
    applyJointImpulse(a, impulse, -1.f);
    applyJointImpulse(b, impulse, 1.f);
}

void solveAngularAxis(State* a, State* b, float targetSpeed, float limit) {
    float const inertiaA = a ? a->inverseInertia : 0.f;
    float const inertiaB = b ? b->inverseInertia : 0.f;
    float const sum = inertiaA + inertiaB;
    if (sum <= 0.000001f) return;
    float const relative =
        (b ? b->angularVelocity : 0.f) - (a ? a->angularVelocity : 0.f) - targetSpeed;
    float impulse = -relative / sum;
    if (limit > 0.f) impulse = std::clamp(impulse, -limit, limit);
    if (a) a->angularVelocity -= impulse * inertiaA;
    if (b) b->angularVelocity += impulse * inertiaB;
}

// Along the line between the two ends only, which is what separates a rope that
// may go slack from a pin that never does.
void solveDistanceAxis(JointEnd& a, JointEnd& b, float bias, bool pullOnly) {
    Vec2 const axis = normalized(b.point - a.point);
    if (axis.x == 0.f && axis.y == 0.f) return;
    float const inverseMassA = a.state ? a.state->inverseMass : 0.f;
    float const inverseMassB = b.state ? b.state->inverseMass : 0.f;
    float const inertiaA = a.state ? a.state->inverseInertia : 0.f;
    float const inertiaB = b.state ? b.state->inverseInertia : 0.f;
    float const armA = cross(a.arm, axis);
    float const armB = cross(b.arm, axis);
    float const denominator = inverseMassA + inverseMassB +
        armA * armA * inertiaA + armB * armB * inertiaB;
    if (denominator <= 0.000001f) return;

    float const speed = dot(jointVelocity(b) - jointVelocity(a), axis);
    float impulse = -(speed + bias) / denominator;
    if (pullOnly) impulse = std::min(impulse, 0.f);
    Vec2 const applied = axis * impulse;
    applyJointImpulse(a, applied, -1.f);
    applyJointImpulse(b, applied, 1.f);
}

} // namespace

bool fixtureContains(Fixture const& fixture, Vec2 point, float slack) {
    Vec2 const local{point.x - fixture.offset.x, point.y - fixture.offset.y};
    if (fixture.radius > 0.f) {
        float const reach = fixture.radius + slack;
        return lengthSquared(local) <= reach * reach;
    }
    if (fixture.vertexCount >= 3) {
        int const count = std::min(fixture.vertexCount, kMaxVertices);
        for (int i = 0; i < count; ++i) {
            Vec2 const from = fixture.vertices[i];
            Vec2 const to = fixture.vertices[(i + 1) % count];
            if (dot(edgeNormal(from, to), local - from) > slack) return false;
        }
        return true;
    }
    return std::abs(local.x) <= fixture.halfSize.x + slack &&
        std::abs(local.y) <= fixture.halfSize.y + slack;
}

// A hash grid over everything that never moves, built once. Without it a body
// falling through a captured level walked all of its fixtures on every substep.
struct StaticGrid {
    float cellSize = 120.f;
    std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> cells;
    std::vector<std::pair<std::size_t, std::size_t>> entries;

    static std::uint64_t cellKey(int x, int y) {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) |
            static_cast<std::uint32_t>(y);
    }

    void insert(std::size_t body, std::size_t fixture, Bounds const& bounds) {
        auto const slot = static_cast<std::uint32_t>(entries.size());
        entries.emplace_back(body, fixture);
        int const minX = static_cast<int>(std::floor(bounds.min.x / cellSize));
        int const maxX = static_cast<int>(std::floor(bounds.max.x / cellSize));
        int const minY = static_cast<int>(std::floor(bounds.min.y / cellSize));
        int const maxY = static_cast<int>(std::floor(bounds.max.y / cellSize));
        for (int x = minX; x <= maxX; ++x) {
            for (int y = minY; y <= maxY; ++y) cells[cellKey(x, y)].push_back(slot);
        }
    }

    void query(Bounds const& bounds, std::vector<std::uint32_t>& out) const {
        out.clear();
        if (cells.empty()) return;
        int const minX = static_cast<int>(std::floor(bounds.min.x / cellSize));
        int const maxX = static_cast<int>(std::floor(bounds.max.x / cellSize));
        int const minY = static_cast<int>(std::floor(bounds.min.y / cellSize));
        int const maxY = static_cast<int>(std::floor(bounds.max.y / cellSize));
        for (int x = minX; x <= maxX; ++x) {
            for (int y = minY; y <= maxY; ++y) {
                auto const cell = cells.find(cellKey(x, y));
                if (cell == cells.end()) continue;
                out.insert(out.end(), cell->second.begin(), cell->second.end());
            }
        }
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
    }
};

struct WorldData {
    SimulationOptions options;
    std::vector<BodySpec> bodies;
    std::vector<State> states;
    std::vector<Joint> joints;
    std::vector<float> jointLengths;
    std::vector<float> jointAngles;
    std::vector<Constraint> constraints;
    std::unordered_map<std::uint64_t, CachedImpulse> cache;
    std::unordered_map<std::uint64_t, CachedImpulse> nextCache;
    std::unordered_set<std::uint64_t> activePairs;
    std::unordered_map<std::uint64_t, ContactEvent> activeContacts;
    std::vector<ContactEvent> events;
    std::vector<std::uint32_t> candidates;
    std::vector<Pose> safePoses;
    StaticGrid grid;
    float time = 0.f;
    float thinnest = 1.f;
    std::size_t impacts = 0;
    float peakImpulse = 0.f;
};

namespace {

// Wind, blasts, whirlpools and water all end up here as an acceleration plus an
// extra damping, which is all the integrator needs to know about them.
struct FieldSample {
    Vec2 acceleration;
    float drag = 0.f;
};

bool insideField(ForceField const& field, Vec2 point) {
    if (field.halfSize.x <= 0.f && field.halfSize.y <= 0.f) return true;
    return std::abs(point.x - field.position.x) <= field.halfSize.x &&
        std::abs(point.y - field.position.y) <= field.halfSize.y;
}

FieldSample sampleField(
    ForceField const& field,
    State const& state,
    Vec2 gravity
) {
    FieldSample sample;
    switch (field.kind) {
        case FieldKind::Wind: {
            if (!insideField(field, state.position)) return sample;
            sample.acceleration = normalized(field.direction) * field.strength;
            sample.drag = field.drag;
            return sample;
        }
        case FieldKind::Radial:
        case FieldKind::Vortex: {
            Vec2 const away = state.position - field.position;
            float const distance = length(away);
            if (field.radius > 0.f && distance > field.radius) return sample;
            float const falloff =
                field.radius > 0.f ? std::max(0.f, 1.f - distance / field.radius) : 1.f;
            Vec2 const direction = distance > 0.0001f ? away / distance : Vec2{0.f, 1.f};
            sample.acceleration = field.kind == FieldKind::Radial
                ? direction * (field.strength * falloff)
                : Vec2{-direction.y, direction.x} * (field.strength * falloff);
            sample.drag = field.drag * falloff;
            return sample;
        }
        case FieldKind::Buoyancy: {
            // How much of the body's bounding box is under the surface, which is
            // enough to float a crate without integrating over its real outline.
            float const top = field.position.y + field.halfSize.y;
            float const bottom = field.position.y - field.halfSize.y;
            float const reach = std::max(state.boundingRadius, 0.001f);
            if (field.halfSize.x > 0.f &&
                std::abs(state.position.x - field.position.x) > field.halfSize.x + reach) {
                return sample;
            }
            float const submerged = std::clamp(
                (std::min(state.position.y + reach, top) -
                    std::max(state.position.y - reach, bottom)) / (reach * 2.f),
                0.f, 1.f
            );
            if (submerged <= 0.f) return sample;
            sample.acceleration = gravity * (-field.strength * submerged);
            sample.drag = field.drag * submerged;
            return sample;
        }
    }
    return sample;
}

void clampSpeed(State& state, float worldMaxSpeed) {
    float const limit = state.maxSpeed > 0.f
        ? (worldMaxSpeed > 0.f ? std::min(state.maxSpeed, worldMaxSpeed) : state.maxSpeed)
        : worldMaxSpeed;
    if (limit > 0.f) {
        float const speed = length(state.velocity);
        if (speed > limit) state.velocity = state.velocity * (limit / speed);
    }
    if (state.maxAngularSpeed > 0.f) {
        state.angularVelocity =
            std::clamp(state.angularVelocity, -state.maxAngularSpeed, state.maxAngularSpeed);
    }
}

bool belowSleepThreshold(State const& state) {
    return lengthSquared(state.velocity) < kSleepSpeed * kSleepSpeed &&
        std::abs(state.angularVelocity) < kSleepSpin;
}

bool movingEnoughToWake(State const& state) {
    return lengthSquared(state.velocity) > kWakeSpeed * kWakeSpeed ||
        std::abs(state.angularVelocity) > kSleepSpin * 3.f;
}

void wakeState(State& state) {
    if (!isDynamic(state)) return;
    state.asleep = false;
    state.sleepTimer = 0.f;
}

// Rare, but a body whose impulses overflowed hands the preview a pose cocos
// cannot draw, and every contact it takes part in inherits the NaN.
bool diverged(State const& state) {
    return !std::isfinite(state.position.x) || !std::isfinite(state.position.y) ||
        !std::isfinite(state.angle) || !std::isfinite(state.velocity.x) ||
        !std::isfinite(state.velocity.y) || !std::isfinite(state.angularVelocity);
}

// `fastest` reaches infinity before the divergence guard parks a body, and
// casting that to int is undefined; the comparison order keeps NaN out too.
int substepsFor(float travel) {
    if (!(travel > 1.f)) return 1;
    if (!(travel < static_cast<float>(kMaxSubsteps))) return kMaxSubsteps;
    return static_cast<int>(std::ceil(travel));
}

bool jointAllowsCollision(WorldData const& data, std::size_t a, std::size_t b) {
    for (auto const& joint : data.joints) {
        bool const connects =
            (joint.bodyA == a && joint.bodyB == b) ||
            (joint.bodyA == b && joint.bodyB == a);
        if (connects && !joint.collideConnected) return false;
    }
    return true;
}

void rememberContact(
    std::unordered_map<std::uint64_t, ContactEvent>& contacts,
    std::size_t bodyA,
    std::size_t bodyB,
    Manifold const& manifold,
    float approachSpeed,
    bool sensor
) {
    std::size_t const first = std::min(bodyA, bodyB);
    std::size_t const second = std::max(bodyA, bodyB);
    std::uint64_t const key = pairKey(first, second);

    ContactEvent candidate;
    candidate.bodyA = first;
    candidate.bodyB = second;
    candidate.point = manifold.points[0];
    candidate.normal = bodyA == first ? manifold.normal : manifold.normal * -1.f;
    candidate.approachSpeed = approachSpeed;
    candidate.sensor = sensor;

    auto [slot, inserted] = contacts.emplace(key, candidate);
    if (inserted) return;

    auto& current = slot->second;
    bool const replaceGeometry = (current.sensor && !sensor) ||
        (current.sensor == sensor && approachSpeed > current.approachSpeed);
    if (replaceGeometry) {
        float const impulse = current.impulse;
        current = candidate;
        current.impulse = impulse;
    }
    current.sensor = current.sensor && sensor;
}

void collectContact(
    WorldData& data,
    std::size_t bodyA,
    std::size_t fixtureA,
    std::size_t bodyB,
    std::size_t fixtureB,
    std::unordered_set<std::uint64_t>& touchingPairs,
    std::unordered_map<std::uint64_t, ContactEvent>& touchingContacts
) {
    if (bodyA >= data.bodies.size() || bodyB >= data.bodies.size() || bodyA == bodyB) return;
    if (fixtureA >= data.bodies[bodyA].fixtures.size() ||
        fixtureB >= data.bodies[bodyB].fixtures.size()) return;
    if (!jointAllowsCollision(data, bodyA, bodyB)) return;

    auto const& specA = data.bodies[bodyA];
    auto const& specB = data.bodies[bodyB];
    auto const& fixtureSpecA = specA.fixtures[fixtureA];
    auto const& fixtureSpecB = specB.fixtures[fixtureB];
    auto& stateA = data.states[bodyA];
    auto& stateB = data.states[bodyB];

    if (!boundsOverlap(
            fixtureBounds(stateA, fixtureSpecA),
            fixtureBounds(stateB, fixtureSpecB),
            0.f
        )) return;

    Manifold manifold;
    if (!buildManifold(
            worldFixture(stateA, fixtureSpecA),
            worldFixture(stateB, fixtureSpecB),
            manifold
        )) return;

    Vec2 const point = manifold.points[0];
    float const approachSpeed = std::max(
        0.f,
        -dot(
            relativeVelocityAt(
                stateA,
                stateB,
                point - stateA.position,
                point - stateB.position
            ),
            manifold.normal
        )
    );
    bool const sensor = fixtureSpecA.sensor || fixtureSpecB.sensor;
    std::uint64_t const pair = pairKey(std::min(bodyA, bodyB), std::max(bodyA, bodyB));
    touchingPairs.insert(pair);
    rememberContact(
        touchingContacts,
        bodyA,
        bodyB,
        manifold,
        approachSpeed,
        sensor
    );

    if (approachSpeed > kWakeSpeed || movingEnoughToWake(stateA) || movingEnoughToWake(stateB)) {
        wakeState(stateA);
        wakeState(stateB);
    }
    if (sensor) return;

    Constraint constraint;
    constraint.a = bodyA;
    constraint.b = bodyB;
    constraint.normal = manifold.normal;
    constraint.count = manifold.count;
    constraint.restitution = std::min(
        restitutionOf(specA, fixtureSpecA),
        restitutionOf(specB, fixtureSpecB)
    );
    constraint.friction = std::sqrt(
        frictionOf(specA, fixtureSpecA) * frictionOf(specB, fixtureSpecB)
    );
    constraint.surfaceVelocity =
        fixtureSpecA.surfaceVelocity - fixtureSpecB.surfaceVelocity;
    for (int pointIndex = 0; pointIndex < manifold.count; ++pointIndex) {
        auto& contact = constraint.points[pointIndex];
        contact.point = manifold.points[pointIndex];
        contact.penetration = manifold.penetration[pointIndex];
        contact.key = contactKey(bodyA, fixtureA, bodyB, fixtureB, pointIndex);
        if (data.options.warmStarting) {
            auto const cached = data.cache.find(contact.key);
            if (cached != data.cache.end()) {
                contact.normalImpulse = cached->second.normalImpulse;
                contact.tangentImpulse = cached->second.tangentImpulse;
            }
        }
    }
    data.constraints.push_back(constraint);
}

void solveSpringAxis(
    JointEnd& a,
    JointEnd& b,
    float distanceError,
    float stiffness,
    float damping,
    float dt
) {
    Vec2 const axis = normalized(b.point - a.point);
    if (axis.x == 0.f && axis.y == 0.f) return;
    float const inverseMassA = a.state ? a.state->inverseMass : 0.f;
    float const inverseMassB = b.state ? b.state->inverseMass : 0.f;
    float const inertiaA = a.state ? a.state->inverseInertia : 0.f;
    float const inertiaB = b.state ? b.state->inverseInertia : 0.f;
    float const armA = cross(a.arm, axis);
    float const armB = cross(b.arm, axis);
    float const denominator = inverseMassA + inverseMassB +
        armA * armA * inertiaA + armB * armB * inertiaB;
    if (denominator <= 0.000001f) return;

    float const speed = dot(jointVelocity(b) - jointVelocity(a), axis);
    float const bias = distanceError * std::clamp(stiffness, 0.f, 1.f) /
        std::max(dt, 0.000001f);
    // Relax towards the spring's corrective velocity. Applying the damping to
    // the whole error keeps repeated solver iterations convergent instead of
    // adding the positional bias again on every pass.
    float const relaxation = std::clamp(damping, 0.05f, 1.f);
    float const impulse = -(speed + bias) * relaxation / denominator;
    Vec2 const applied = axis * impulse;
    applyJointImpulse(a, applied, -1.f);
    applyJointImpulse(b, applied, 1.f);
}

bool validJoint(WorldData const& data, Joint const& joint) {
    return joint.bodyA < data.states.size() &&
        (joint.bodyB == kWorldBody || joint.bodyB < data.states.size()) &&
        joint.bodyA != joint.bodyB;
}

void solveJoints(WorldData& data, float dt) {
    for (std::size_t index = 0; index < data.joints.size(); ++index) {
        auto const& joint = data.joints[index];
        if (!validJoint(data, joint)) continue;
        auto endA = jointEnd(data.states, joint.bodyA, joint.anchorA);
        auto endB = jointEnd(data.states, joint.bodyB, joint.anchorB);
        Vec2 const error = endB.point - endA.point;
        float const distanceError = length(error) - data.jointLengths[index];
        float const stiffness = std::clamp(joint.stiffness, 0.f, 1.f);
        float const safeStep = std::max(dt, 0.000001f);

        switch (joint.kind) {
            case JointKind::Pin:
                solvePinAxes(endA, endB, error * (-stiffness / safeStep));
                break;
            case JointKind::Rope:
                if (distanceError > 0.f) {
                    solveDistanceAxis(endA, endB, distanceError * stiffness / safeStep, true);
                }
                break;
            case JointKind::Spring:
                solveSpringAxis(
                    endA,
                    endB,
                    distanceError,
                    joint.stiffness,
                    joint.damping,
                    dt
                );
                break;
            case JointKind::Weld: {
                solvePinAxes(endA, endB, error * (-stiffness / safeStep));
                State* stateB = joint.bodyB < data.states.size()
                    ? &data.states[joint.bodyB]
                    : nullptr;
                float const angleB = stateB ? stateB->angle : 0.f;
                float const angleError = angleB - endA.state->angle - data.jointAngles[index];
                solveAngularAxis(
                    endA.state,
                    stateB,
                    -angleError * stiffness / safeStep,
                    0.f
                );
                break;
            }
            case JointKind::Motor: {
                if (joint.maxMotorTorque <= 0.f) break;
                State* stateB = joint.bodyB < data.states.size()
                    ? &data.states[joint.bodyB]
                    : nullptr;
                float const limit = joint.maxMotorTorque * dt /
                    static_cast<float>(data.options.solverIterations);
                solveAngularAxis(endA.state, stateB, joint.motorSpeed, limit);
                break;
            }
        }
    }
}

void translateJointEnds(JointEnd& a, JointEnd& b, Vec2 correction) {
    float const inverseMassA = a.state ? a.state->inverseMass : 0.f;
    float const inverseMassB = b.state ? b.state->inverseMass : 0.f;
    float const sum = inverseMassA + inverseMassB;
    if (sum <= 0.000001f) return;
    if (a.state) a.state->position += correction * (inverseMassA / sum);
    if (b.state) b.state->position -= correction * (inverseMassB / sum);
}

void correctJoints(WorldData& data) {
    for (std::size_t index = 0; index < data.joints.size(); ++index) {
        auto const& joint = data.joints[index];
        if (!validJoint(data, joint) || joint.kind == JointKind::Motor) continue;
        auto endA = jointEnd(data.states, joint.bodyA, joint.anchorA);
        auto endB = jointEnd(data.states, joint.bodyB, joint.anchorB);
        Vec2 const delta = endB.point - endA.point;
        float const distance = length(delta);
        float const stiffness = std::clamp(joint.stiffness, 0.f, 1.f);

        if (joint.kind == JointKind::Pin || joint.kind == JointKind::Weld) {
            translateJointEnds(endA, endB, delta * (kJointCorrection * stiffness));
        } else if (distance > 0.000001f) {
            float const error = distance - data.jointLengths[index];
            if (joint.kind != JointKind::Rope || error > 0.f) {
                float const softness = joint.kind == JointKind::Spring ? 0.2f : 1.f;
                translateJointEnds(
                    endA,
                    endB,
                    delta * (error / distance * kJointCorrection * stiffness * softness)
                );
            }
        }

        if (joint.kind != JointKind::Weld) continue;
        State* stateB = joint.bodyB < data.states.size()
            ? &data.states[joint.bodyB]
            : nullptr;
        float const inverseA = endA.state ? endA.state->inverseInertia : 0.f;
        float const inverseB = stateB ? stateB->inverseInertia : 0.f;
        float const sum = inverseA + inverseB;
        if (sum <= 0.000001f) continue;
        float const angleB = stateB ? stateB->angle : 0.f;
        float const error = angleB - endA.state->angle - data.jointAngles[index];
        float const correction = error * kJointCorrection * stiffness;
        endA.state->angle += correction * (inverseA / sum);
        if (stateB) stateB->angle -= correction * (inverseB / sum);
    }
}

void updateSleep(WorldData& data, float dt) {
    for (auto& state : data.states) {
        if (!isDynamic(state)) continue;
        if (!data.options.allowSleep || !state.allowSleep) {
            wakeState(state);
            continue;
        }
        if (state.asleep) {
            if (movingEnoughToWake(state)) {
                wakeState(state);
            } else {
                state.velocity = {};
                state.angularVelocity = 0.f;
                continue;
            }
        }
        if (!belowSleepThreshold(state)) {
            state.sleepTimer = 0.f;
            continue;
        }
        state.sleepTimer += dt;
        if (state.sleepTimer < kSleepDelay) continue;
        state.asleep = true;
        state.velocity = {};
        state.angularVelocity = 0.f;
    }
}

void preserveSleepingContacts(
    WorldData const& data,
    std::unordered_set<std::uint64_t>& touchingPairs,
    std::unordered_map<std::uint64_t, ContactEvent>& touchingContacts
) {
    for (auto const& [key, contact] : data.activeContacts) {
        if (contact.bodyA >= data.states.size() || contact.bodyB >= data.states.size()) continue;
        auto const frozen = [](State const& state) {
            return state.motion == Motion::Static ||
                (state.motion == Motion::Dynamic && state.asleep);
        };
        if (!frozen(data.states[contact.bodyA]) || !frozen(data.states[contact.bodyB])) continue;
        touchingPairs.insert(key);
        touchingContacts.emplace(key, contact);
    }
}

void reportContacts(
    WorldData& data,
    std::unordered_set<std::uint64_t> const& touchingPairs,
    std::unordered_map<std::uint64_t, ContactEvent> const& touchingContacts
) {
    std::vector<std::uint64_t> began;
    std::vector<std::uint64_t> ended;
    for (auto const& [key, contact] : touchingContacts) {
        if (!data.activePairs.contains(key)) began.push_back(key);
    }
    for (auto const key : data.activePairs) {
        if (!touchingPairs.contains(key)) ended.push_back(key);
    }
    std::sort(began.begin(), began.end());
    std::sort(ended.begin(), ended.end());

    for (auto const key : began) {
        if (data.events.size() >= kMaxContactEvents) break;
        ContactEvent event = touchingContacts.at(key);
        event.time = data.time;
        event.phase = ContactPhase::Begin;
        data.events.push_back(event);
        if (!event.sensor) ++data.impacts;
    }
    for (auto const key : ended) {
        if (data.events.size() >= kMaxContactEvents) break;
        auto const previous = data.activeContacts.find(key);
        if (previous == data.activeContacts.end()) continue;
        ContactEvent event = previous->second;
        event.time = data.time;
        event.impulse = 0.f;
        event.approachSpeed = 0.f;
        event.phase = ContactPhase::End;
        data.events.push_back(event);
    }

    data.activePairs = touchingPairs;
    data.activeContacts = touchingContacts;
}

// Segment against one placed fixture, reported as a fraction of the segment.
bool raycastShape(Shape const& shape, Vec2 from, Vec2 to, float& fraction, Vec2& normal) {
    Vec2 const delta = to - from;
    if (shape.count == 0) {
        Vec2 const offset = from - shape.center;
        float const a = dot(delta, delta);
        if (a < 0.000001f) return false;
        float const b = 2.f * dot(offset, delta);
        float const c = dot(offset, offset) - shape.radius * shape.radius;
        float const discriminant = b * b - 4.f * a * c;
        if (discriminant < 0.f) return false;
        float const root = (-b - std::sqrt(discriminant)) / (2.f * a);
        if (root < 0.f || root > 1.f) return false;
        fraction = root;
        normal = normalized(from + delta * root - shape.center);
        return true;
    }

    float lower = 0.f;
    float upper = 1.f;
    int face = -1;
    for (int i = 0; i < shape.count; ++i) {
        Vec2 const faceNormal = faceNormalOf(shape, i);
        float const numerator = dot(faceNormal, shape.points[i] - from);
        float const denominator = dot(faceNormal, delta);
        if (std::abs(denominator) < 0.000001f) {
            if (numerator < 0.f) return false;
            continue;
        }
        if (denominator < 0.f && numerator < lower * denominator) {
            lower = numerator / denominator;
            face = i;
        } else if (denominator > 0.f && numerator < upper * denominator) {
            upper = numerator / denominator;
        }
        if (upper < lower) return false;
    }
    if (face < 0) return false;
    fraction = lower;
    normal = faceNormalOf(shape, face);
    return true;
}

} // namespace

PhysicsWorld::PhysicsWorld(
    std::vector<BodySpec> const& bodies,
    SimulationOptions const& options,
    std::vector<Joint> const& joints
) : m_data(std::make_unique<WorldData>()) {
    auto& data = *m_data;
    data.bodies = bodies;
    data.options = options;
    data.options.fixedRate = std::clamp(data.options.fixedRate, 30, 480);
    data.options.solverIterations = std::clamp(data.options.solverIterations, 1, 12);
    data.options.airDrag = std::max(0.f, data.options.airDrag);
    data.options.angularDrag = std::max(0.f, data.options.angularDrag);

    float thinnest = std::numeric_limits<float>::max();
    float staticReach = 0.f;
    std::size_t staticFixtures = 0;
    data.states.reserve(bodies.size());
    for (auto const& body : bodies) {
        State state;
        state.position = body.position;
        state.velocity = body.velocity;
        state.angle = body.angle;
        state.angularVelocity = body.angularVelocity;
        state.boundingRadius = boundingRadius(body);
        state.motion = body.motion;
        state.allowSleep = body.allowSleep;
        state.linearDamping =
            body.linearDamping >= 0.f ? body.linearDamping : data.options.airDrag;
        state.angularDamping =
            body.angularDamping >= 0.f ? body.angularDamping : data.options.angularDrag;
        state.maxSpeed = std::max(0.f, body.maxSpeed);
        state.maxAngularSpeed = std::max(0.f, body.maxAngularSpeed);
        if (body.motion == Motion::Dynamic) {
            float const mass = std::max(body.mass, 0.01f);
            state.inverseMass = 1.f / mass;
            state.inverseInertia = body.fixedRotation ? 0.f : 1.f / bodyInertia(body, mass);
        }
        for (auto const& fixture : body.fixtures) {
            thinnest = std::min(thinnest, fixture.radius > 0.f
                ? fixture.radius
                : std::min(fixture.halfSize.x, fixture.halfSize.y));
            if (body.motion != Motion::Static) continue;
            staticReach += fixtureReach(fixture);
            ++staticFixtures;
        }
        data.safePoses.push_back({state.position, state.angle});
        data.states.push_back(state);
    }
    data.thinnest = std::max(thinnest, 1.f);

    if (staticFixtures > 0) {
        data.grid.cellSize =
            std::clamp(staticReach / static_cast<float>(staticFixtures) * 4.f, 30.f, 900.f);
        for (std::size_t i = 0; i < bodies.size(); ++i) {
            if (bodies[i].motion != Motion::Static) continue;
            for (std::size_t f = 0; f < bodies[i].fixtures.size(); ++f) {
                data.grid.insert(i, f, fixtureBounds(data.states[i], bodies[i].fixtures[f]));
            }
        }
    }

    data.joints = joints;
    data.jointLengths.reserve(joints.size());
    data.jointAngles.reserve(joints.size());
    for (auto const& joint : joints) {
        if (!validJoint(data, joint)) {
            data.jointLengths.push_back(0.f);
            data.jointAngles.push_back(0.f);
            continue;
        }
        auto const endA = jointEnd(data.states, joint.bodyA, joint.anchorA);
        auto const endB = jointEnd(data.states, joint.bodyB, joint.anchorB);
        data.jointLengths.push_back(
            joint.length >= 0.f ? joint.length : length(endB.point - endA.point)
        );
        float const angleB = joint.bodyB < data.states.size()
            ? data.states[joint.bodyB].angle
            : 0.f;
        data.jointAngles.push_back(angleB - data.states[joint.bodyA].angle);
    }
}

PhysicsWorld::~PhysicsWorld() = default;
PhysicsWorld::PhysicsWorld(PhysicsWorld&&) noexcept = default;
PhysicsWorld& PhysicsWorld::operator=(PhysicsWorld&&) noexcept = default;

void PhysicsWorld::step(float dt) {
    if (!m_data || !std::isfinite(dt) || !(dt > 0.f)) return;
    auto& data = *m_data;
    auto& states = data.states;
    auto const& bodies = data.bodies;
    data.events.clear();

    // Split the step so no body can travel past the thinnest fixture in one go,
    // which is what let fast bodies pass straight through static geometry.
    float fastest = 0.f;
    for (auto const& state : states) {
        if (isMovable(state) && !state.asleep) fastest = std::max(fastest, length(state.velocity));
    }
    int const substeps = substepsFor(fastest * dt / (data.thinnest * 0.5f));
    float const subStep = dt / static_cast<float>(substeps);

    std::unordered_set<std::uint64_t> touchingPairs;
    std::unordered_map<std::uint64_t, ContactEvent> touchingContacts;
    preserveSleepingContacts(data, touchingPairs, touchingContacts);

    for (int sub = 0; sub < substeps; ++sub) {
        for (std::size_t i = 0; i < states.size(); ++i) {
            auto& state = states[i];
            if (!isMovable(state) || state.asleep) continue;
            if (state.motion == Motion::Kinematic) {
                state.position += state.velocity * subStep;
                state.angle += state.angularVelocity * subStep;
                continue;
            }
            Vec2 acceleration = data.options.gravity * bodies[i].gravityScale;
            float drag = state.linearDamping;
            for (auto const& field : data.options.fields) {
                auto const sample = sampleField(field, state, data.options.gravity);
                acceleration += sample.acceleration;
                drag += sample.drag;
            }
            state.velocity += acceleration * subStep;
            state.velocity = state.velocity * std::exp(-drag * subStep);
            state.angularVelocity *= std::exp(-state.angularDamping * subStep);
            clampSpeed(state, data.options.maxSpeed);
            state.position += state.velocity * subStep;
            state.angle += state.angularVelocity * subStep;
        }

        data.constraints.clear();
        data.nextCache.clear();
        for (std::size_t i = 0; i < bodies.size(); ++i) {
            if (!isMovable(states[i])) continue;

            // Static geometry comes out of the grid; everything that moves is
            // paired the plain way, because there are never many of those.
            for (std::size_t j = i + 1; j < bodies.size(); ++j) {
                if (!isMovable(states[j])) continue;
                if (states[i].asleep && states[j].asleep) continue;
                if (!isDynamic(states[i]) && !isDynamic(states[j])) continue;
                if (!filtersMatch(bodies[i], bodies[j])) continue;
                float const reach = states[i].boundingRadius + states[j].boundingRadius;
                if (lengthSquared(states[j].position - states[i].position) > reach * reach) {
                    continue;
                }
                for (std::size_t fa = 0; fa < bodies[i].fixtures.size(); ++fa) {
                    for (std::size_t fb = 0; fb < bodies[j].fixtures.size(); ++fb) {
                        collectContact(
                            data,
                            i,
                            fa,
                            j,
                            fb,
                            touchingPairs,
                            touchingContacts
                        );
                    }
                }
            }

            if (!isDynamic(states[i]) || states[i].asleep) continue;
            for (std::size_t fa = 0; fa < bodies[i].fixtures.size(); ++fa) {
                auto const bounds = fixtureBounds(states[i], bodies[i].fixtures[fa]);
                data.grid.query(bounds, data.candidates);
                for (auto slot : data.candidates) {
                    auto const& entry = data.grid.entries[slot];
                    if (!filtersMatch(bodies[i], bodies[entry.first])) continue;
                    collectContact(
                        data,
                        i,
                        fa,
                        entry.first,
                        entry.second,
                        touchingPairs,
                        touchingContacts
                    );
                }
            }
        }

        for (auto& constraint : data.constraints) {
            prepareConstraint(constraint, states[constraint.a], states[constraint.b]);
            if (!data.options.warmStarting) continue;
            warmStartConstraint(constraint, states[constraint.a], states[constraint.b]);
        }

        for (int iteration = 0; iteration < data.options.solverIterations; ++iteration) {
            solveJoints(data, subStep);
            for (auto& constraint : data.constraints) {
                float const impulse =
                    solveConstraint(constraint, states[constraint.a], states[constraint.b]);
                if (iteration + 1 < data.options.solverIterations || impulse <= 0.01f) continue;
                data.peakImpulse = std::max(data.peakImpulse, impulse);
            }
        }

        for (auto const& constraint : data.constraints) {
            separateConstraint(constraint, states[constraint.a], states[constraint.b]);
            float contactImpulse = 0.f;
            for (int i = 0; i < constraint.count; ++i) {
                data.nextCache[constraint.points[i].key] = {
                    constraint.points[i].normalImpulse,
                    constraint.points[i].tangentImpulse,
                };
                contactImpulse = std::max(
                    contactImpulse,
                    constraint.points[i].normalImpulse
                );
            }
            std::uint64_t const pair = pairKey(
                std::min(constraint.a, constraint.b),
                std::max(constraint.a, constraint.b)
            );
            touchingPairs.insert(pair);
            auto const contact = touchingContacts.find(pair);
            if (contact != touchingContacts.end()) {
                contact->second.impulse = std::max(contact->second.impulse, contactImpulse);
            }
        }
        correctJoints(data);
        data.cache.swap(data.nextCache);
        updateSleep(data, subStep);
    }

    bool parked = false;
    for (std::size_t i = 0; i < states.size(); ++i) {
        auto& state = states[i];
        if (!diverged(state)) {
            data.safePoses[i] = {state.position, state.angle};
            continue;
        }
        state.position = data.safePoses[i].position;
        state.angle = data.safePoses[i].angle;
        state.velocity = {};
        state.angularVelocity = 0.f;
        state.asleep = isDynamic(state);
        parked = true;
    }
    // The warm start would feed the same overflowed impulses back in.
    if (parked) data.cache.clear();

    data.time += dt;
    reportContacts(data, touchingPairs, touchingContacts);
}

float PhysicsWorld::time() const {
    return m_data ? m_data->time : 0.f;
}

std::size_t PhysicsWorld::bodyCount() const {
    return m_data ? m_data->states.size() : 0;
}

Pose PhysicsWorld::pose(std::size_t body) const {
    if (!m_data || body >= m_data->states.size()) return {};
    auto const& state = m_data->states[body];
    return {state.position, state.angle};
}

Vec2 PhysicsWorld::velocity(std::size_t body) const {
    if (!m_data || body >= m_data->states.size()) return {};
    return m_data->states[body].velocity;
}

float PhysicsWorld::angularVelocity(std::size_t body) const {
    if (!m_data || body >= m_data->states.size()) return 0.f;
    return m_data->states[body].angularVelocity;
}

bool PhysicsWorld::asleep(std::size_t body) const {
    return m_data && body < m_data->states.size() && m_data->states[body].asleep;
}

bool PhysicsWorld::settled() const {
    if (!m_data) return true;
    for (auto const& state : m_data->states) {
        if (isDynamic(state) && !state.asleep) return false;
    }
    return true;
}

Frame PhysicsWorld::snapshot() const {
    Frame frame;
    if (!m_data) return frame;
    frame.time = m_data->time;
    frame.poses.reserve(m_data->states.size());
    for (auto const& state : m_data->states) {
        frame.poses.push_back({state.position, state.angle});
    }
    return frame;
}

void PhysicsWorld::setVelocity(std::size_t body, Vec2 value) {
    if (!m_data || body >= m_data->states.size()) return;
    auto& state = m_data->states[body];
    if (!isMovable(state)) return;
    state.velocity = value;
    wakeState(state);
}

void PhysicsWorld::setAngularVelocity(std::size_t body, float value) {
    if (!m_data || body >= m_data->states.size()) return;
    auto& state = m_data->states[body];
    if (!isMovable(state)) return;
    state.angularVelocity = value;
    wakeState(state);
}

void PhysicsWorld::applyImpulse(std::size_t body, Vec2 impulse) {
    if (!m_data || body >= m_data->states.size()) return;
    auto& state = m_data->states[body];
    if (!isDynamic(state)) return;
    applyImpulseTo(state, impulse, {}, 1.f);
    wakeState(state);
}

void PhysicsWorld::applyImpulseAt(std::size_t body, Vec2 impulse, Vec2 point) {
    if (!m_data || body >= m_data->states.size()) return;
    auto& state = m_data->states[body];
    if (!isDynamic(state)) return;
    applyImpulseTo(state, impulse, point - state.position, 1.f);
    wakeState(state);
}

void PhysicsWorld::applyAngularImpulse(std::size_t body, float impulse) {
    if (!m_data || body >= m_data->states.size()) return;
    auto& state = m_data->states[body];
    if (!isDynamic(state)) return;
    state.angularVelocity += impulse * state.inverseInertia;
    wakeState(state);
}

void PhysicsWorld::explode(Vec2 center, float radius, float strength) {
    if (!m_data || radius <= 0.f || strength == 0.f) return;
    for (auto& state : m_data->states) {
        if (!isDynamic(state)) continue;
        Vec2 const offset = state.position - center;
        float const distance = length(offset);
        if (distance > radius) continue;
        Vec2 const direction = distance > 0.00001f ? offset / distance : Vec2{0.f, 1.f};
        float const falloff = std::max(0.f, 1.f - distance / radius);
        applyImpulseTo(state, direction * (strength * falloff), {}, 1.f);
        wakeState(state);
    }
}

void PhysicsWorld::wake(std::size_t body) {
    if (!m_data || body >= m_data->states.size()) return;
    wakeState(m_data->states[body]);
}

RayHit PhysicsWorld::raycast(Vec2 from, Vec2 to, std::uint32_t mask) const {
    RayHit best;
    if (!m_data) return best;
    for (std::size_t body = 0; body < m_data->bodies.size(); ++body) {
        if ((m_data->bodies[body].category & mask) == 0u) continue;
        auto const& state = m_data->states[body];
        for (std::size_t fixture = 0;
             fixture < m_data->bodies[body].fixtures.size();
             ++fixture) {
            auto const& fixtureSpec = m_data->bodies[body].fixtures[fixture];
            Vec2 const localFrom = rotate(from - state.position, -state.angle);
            float fraction = 1.f;
            Vec2 normal;
            if (fixtureContains(fixtureSpec, localFrom)) {
                fraction = 0.f;
                normal = normalized(from - to);
                if (normal.x == 0.f && normal.y == 0.f) normal = {0.f, 1.f};
            } else if (!raycastShape(
                           worldFixture(state, fixtureSpec),
                           from,
                           to,
                           fraction,
                           normal
                       )) {
                continue;
            }
            if (best.hit && fraction >= best.fraction) continue;
            best.hit = true;
            best.body = body;
            best.fixture = fixture;
            best.fraction = fraction;
            best.point = from + (to - from) * fraction;
            best.normal = normal;
        }
    }
    return best;
}

std::vector<Overlap> PhysicsWorld::overlapPoint(Vec2 point, float slack) const {
    std::vector<Overlap> result;
    if (!m_data) return result;
    for (std::size_t body = 0; body < m_data->bodies.size(); ++body) {
        auto const& state = m_data->states[body];
        Vec2 const localPoint = rotate(point - state.position, -state.angle);
        for (std::size_t fixture = 0;
             fixture < m_data->bodies[body].fixtures.size();
             ++fixture) {
            if (fixtureContains(m_data->bodies[body].fixtures[fixture], localPoint, slack)) {
                result.push_back({body, fixture});
            }
        }
    }
    return result;
}

std::vector<Overlap> PhysicsWorld::overlapCircle(Vec2 center, float radius) const {
    if (radius <= 0.f) return overlapPoint(center);
    std::vector<Overlap> result;
    if (!m_data) return result;
    Shape query;
    query.center = center;
    query.radius = radius;
    Bounds const queryBounds{
        {center.x - radius, center.y - radius},
        {center.x + radius, center.y + radius},
    };
    for (std::size_t body = 0; body < m_data->bodies.size(); ++body) {
        auto const& state = m_data->states[body];
        for (std::size_t fixture = 0;
             fixture < m_data->bodies[body].fixtures.size();
             ++fixture) {
            auto const& fixtureSpec = m_data->bodies[body].fixtures[fixture];
            if (!boundsOverlap(queryBounds, fixtureBounds(state, fixtureSpec), 0.f)) continue;
            Manifold manifold;
            if (buildManifold(query, worldFixture(state, fixtureSpec), manifold)) {
                result.push_back({body, fixture});
            }
        }
    }
    return result;
}

std::vector<ContactEvent> const& PhysicsWorld::contacts() const {
    static std::vector<ContactEvent> const empty;
    return m_data ? m_data->events : empty;
}

std::size_t PhysicsWorld::impacts() const {
    return m_data ? m_data->impacts : 0;
}

float PhysicsWorld::peakImpulse() const {
    return m_data ? m_data->peakImpulse : 0.f;
}

SimulationTrace simulate(
    std::vector<BodySpec> const& bodies,
    SimulationOptions const& rawOptions,
    std::vector<Joint> const& joints
) {
    SimulationTrace trace;
    if (bodies.empty()) return trace;

    SimulationOptions options = rawOptions;
    options.duration = std::clamp(options.duration, 0.1f, 30.f);
    options.fixedRate = std::clamp(options.fixedRate, 30, 480);
    options.sampleRate = std::clamp(options.sampleRate, 5, options.fixedRate);
    options.solverIterations = std::clamp(options.solverIterations, 1, 12);
    options.airDrag = std::max(0.f, options.airDrag);
    options.angularDrag = std::max(0.f, options.angularDrag);

    auto const deadline = options.timeBudget > 0.f
        ? std::chrono::steady_clock::now() +
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<float>(options.timeBudget)
            )
        : std::chrono::steady_clock::time_point::max();

    PhysicsWorld world(bodies, options, joints);
    float const fixedStep = 1.f / static_cast<float>(options.fixedRate);
    float const sampleStep = 1.f / static_cast<float>(options.sampleRate);
    int const totalSteps = static_cast<int>(std::ceil(options.duration * options.fixedRate));
    float nextSample = sampleStep;
    trace.frames.reserve(
        static_cast<std::size_t>(std::ceil(options.duration * options.sampleRate)) + 1
    );
    trace.frames.push_back(world.snapshot());
    if (world.settled()) trace.settleTime = 0.f;

    for (int stepIndex = 0; stepIndex < totalSteps; ++stepIndex) {
        float const elapsedBefore = stepIndex * fixedStep;
        float const dt = std::min(fixedStep, options.duration - elapsedBefore);
        if (!(dt > 0.f)) break;
        Frame const before = world.snapshot();
        world.step(dt);

        for (auto const& event : world.contacts()) {
            if (trace.contacts.size() >= kMaxContactEvents) break;
            trace.contacts.push_back(event);
        }
        if (trace.settleTime < 0.f && world.settled()) trace.settleTime = world.time();

        float const elapsed = std::min((stepIndex + 1) * fixedStep, options.duration);
        while (elapsed + 0.0001f >= nextSample && nextSample < options.duration) {
            Frame sample = world.snapshot();
            float const alpha = std::clamp((nextSample - elapsedBefore) / dt, 0.f, 1.f);
            sample.time = nextSample;
            for (std::size_t i = 0; i < sample.poses.size(); ++i) {
                auto& pose = sample.poses[i];
                auto const& previous = before.poses[i];
                pose.position = previous.position + (pose.position - previous.position) * alpha;
                pose.angle = previous.angle + (pose.angle - previous.angle) * alpha;
            }
            trace.frames.push_back(std::move(sample));
            nextSample += sampleStep;
        }
        if (elapsed >= options.duration - 0.0001f) break;
        if (std::chrono::steady_clock::now() >= deadline) {
            trace.exhausted = true;
            break;
        }
    }

    float const endTime = trace.exhausted ? world.time() : options.duration;
    if (trace.frames.empty() || trace.frames.back().time < endTime - 0.0001f) {
        Frame finalFrame = world.snapshot();
        finalFrame.time = endTime;
        trace.frames.push_back(finalFrame);
    } else {
        trace.frames.back().time = endTime;
    }
    trace.impacts = world.impacts();
    trace.peakImpulse = world.peakImpulse();
    return trace;
}

} // namespace paimon::editorphysics
