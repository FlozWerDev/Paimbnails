#include "../PhysicsTypes.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_set>

namespace paimon::editorphysics {

namespace {

constexpr float kSlop = 0.05f;
constexpr float kCorrection = 0.7f;
constexpr float kRestingSpeed = 45.f;
constexpr int kMaxSubsteps = 16;

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
};

// A fixture placed in the world: a convex polygon of `count` vertices, or a
// circle when `count` is zero. The solver never looks at the axis-aligned size
// again from here on, so a rotated block collides on its real corners and a
// slope on its real hypotenuse.
struct Shape {
    Vec2 center;
    Vec2 points[4];
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
        shape.count = std::min(fixture.vertexCount, 4);
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

float bodyInertia(BodySpec const& body, float mass) {
    float totalArea = 0.f;
    for (auto const& fixture : body.fixtures) {
        totalArea += std::max(1.f, fixture.halfSize.x * fixture.halfSize.y * 4.f);
    }
    if (totalArea <= 0.f) return mass;

    float inertia = 0.f;
    for (auto const& fixture : body.fixtures) {
        float const width = fixture.halfSize.x * 2.f;
        float const height = fixture.halfSize.y * 2.f;
        float const area = std::max(1.f, width * height);
        float const partMass = mass * area / totalArea;
        inertia += partMass * (
            (width * width + height * height) / 12.f + lengthSquared(fixture.offset)
        );
    }
    return std::max(inertia, 0.001f);
}

float fixtureReach(Fixture const& fixture) {
    if (fixture.radius > 0.f) return fixture.radius;
    if (fixture.vertexCount < 3) return length(fixture.halfSize);
    float reach = 0.f;
    for (int i = 0; i < std::min(fixture.vertexCount, 4); ++i) {
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

Frame snapshot(float time, std::vector<State> const& states) {
    Frame frame;
    frame.time = time;
    frame.poses.reserve(states.size());
    for (auto const& state : states) {
        frame.poses.push_back({state.position, state.angle});
    }
    return frame;
}

void applyImpulse(State& state, Vec2 impulse, Vec2 radius, float direction) {
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
};

// One fixture-pair contact, built once per substep and then relaxed over several
// iterations. Rebuilding it inside the iteration loop, as the previous solver did,
// made restitution decay against its own output and left bounces far too weak.
struct Constraint {
    std::size_t a = 0;
    std::size_t b = 0;
    Vec2 normal;
    ContactPoint points[2];
    int count = 0;
    float restitution = 0.f;
    float friction = 0.f;
};

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
        contact.normalImpulse = 0.f;
        contact.tangentImpulse = 0.f;
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
            applyImpulse(a, impulse, radiusA, -1.f);
            applyImpulse(b, impulse, radiusB, 1.f);
            peak = std::max(peak, contact.normalImpulse);
        }

        float const radiusTangentA = cross(radiusA, tangent);
        float const radiusTangentB = cross(radiusB, tangent);
        float const tangentDenominator = inverseMassSum +
            radiusTangentA * radiusTangentA * a.inverseInertia +
            radiusTangentB * radiusTangentB * b.inverseInertia;
        if (tangentDenominator <= 0.00001f) continue;

        float const slide = dot(relativeVelocityAt(a, b, radiusA, radiusB), tangent);
        float const limit = constraint.friction * contact.normalImpulse;
        float const previous = contact.tangentImpulse;
        contact.tangentImpulse =
            std::clamp(previous - slide / tangentDenominator, -limit, limit);
        Vec2 const impulse = tangent * (contact.tangentImpulse - previous);
        applyImpulse(a, impulse, radiusA, -1.f);
        applyImpulse(b, impulse, radiusB, 1.f);
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

} // namespace

SimulationTrace simulate(
    std::vector<BodySpec> const& bodies,
    SimulationOptions const& rawOptions
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

    std::vector<State> states;
    states.reserve(bodies.size());
    float thinnest = std::numeric_limits<float>::max();
    for (auto const& body : bodies) {
        State state;
        state.position = body.position;
        state.velocity = body.velocity;
        state.angle = body.angle;
        state.angularVelocity = body.angularVelocity;
        state.boundingRadius = boundingRadius(body);
        if (body.motion == Motion::Dynamic) {
            float const mass = std::max(body.mass, 0.01f);
            state.inverseMass = 1.f / mass;
            state.inverseInertia = 1.f / bodyInertia(body, mass);
        }
        for (auto const& fixture : body.fixtures) {
            thinnest = std::min(thinnest, fixture.radius > 0.f
                ? fixture.radius
                : std::min(fixture.halfSize.x, fixture.halfSize.y));
        }
        states.push_back(state);
    }
    thinnest = std::max(thinnest, 1.f);

    float const fixedStep = 1.f / static_cast<float>(options.fixedRate);
    float const sampleStep = 1.f / static_cast<float>(options.sampleRate);
    int const totalSteps = static_cast<int>(std::ceil(options.duration * options.fixedRate));
    float nextSample = sampleStep;
    std::vector<Constraint> constraints;
    std::unordered_set<std::uint64_t> activePairs;
    trace.frames.reserve(static_cast<std::size_t>(std::ceil(options.duration * options.sampleRate)) + 1);
    trace.frames.push_back(snapshot(0.f, states));

    for (int step = 0; step < totalSteps; ++step) {
        float const dt = std::min(fixedStep, options.duration - step * fixedStep);
        if (dt <= 0.f) break;

        // Split the step so no body can travel past the thinnest fixture in one go,
        // which is what let fast bodies pass straight through static geometry.
        float fastest = 0.f;
        for (auto const& state : states) {
            if (state.inverseMass > 0.f) fastest = std::max(fastest, length(state.velocity));
        }
        int const substeps = std::clamp(
            static_cast<int>(std::ceil(fastest * dt / (thinnest * 0.5f))), 1, kMaxSubsteps
        );
        float const subStep = dt / static_cast<float>(substeps);

        std::unordered_set<std::uint64_t> touchingPairs;
        std::unordered_set<std::uint64_t> countedPairs;

        for (int sub = 0; sub < substeps; ++sub) {
            float const linearDamping = std::exp(-options.airDrag * subStep);
            float const angularDamping = std::exp(-options.angularDrag * subStep);
            for (std::size_t i = 0; i < states.size(); ++i) {
                auto& state = states[i];
                if (state.inverseMass <= 0.f) continue;
                state.velocity += options.gravity * (subStep * bodies[i].gravityScale);
                state.velocity = state.velocity * linearDamping;
                state.angularVelocity *= angularDamping;
                state.position += state.velocity * subStep;
                state.angle += state.angularVelocity * subStep;
            }

            constraints.clear();
            for (std::size_t i = 0; i < bodies.size(); ++i) {
                for (std::size_t j = i + 1; j < bodies.size(); ++j) {
                    if (states[i].inverseMass <= 0.f && states[j].inverseMass <= 0.f) continue;
                    float const reach = states[i].boundingRadius + states[j].boundingRadius;
                    if (lengthSquared(states[j].position - states[i].position) > reach * reach) {
                        continue;
                    }

                    for (auto const& fixtureA : bodies[i].fixtures) {
                        Shape const shapeA = worldFixture(states[i], fixtureA);
                        for (auto const& fixtureB : bodies[j].fixtures) {
                            Shape const shapeB = worldFixture(states[j], fixtureB);
                            Manifold manifold;
                            if (!buildManifold(shapeA, shapeB, manifold)) continue;

                            Constraint constraint;
                            constraint.a = i;
                            constraint.b = j;
                            constraint.normal = manifold.normal;
                            constraint.count = manifold.count;
                            constraint.restitution =
                                std::min(bodies[i].restitution, bodies[j].restitution);
                            constraint.friction = std::sqrt(
                                std::max(0.f, bodies[i].friction) *
                                std::max(0.f, bodies[j].friction)
                            );
                            for (int point = 0; point < manifold.count; ++point) {
                                constraint.points[point].point = manifold.points[point];
                                constraint.points[point].penetration = manifold.penetration[point];
                            }
                            prepareConstraint(constraint, states[i], states[j]);
                            constraints.push_back(constraint);
                        }
                    }
                }
            }

            for (int iteration = 0; iteration < options.solverIterations; ++iteration) {
                for (auto& constraint : constraints) {
                    float const impulse =
                        solveConstraint(constraint, states[constraint.a], states[constraint.b]);
                    if (iteration + 1 < options.solverIterations || impulse <= 0.01f) continue;
                    trace.peakImpulse = std::max(trace.peakImpulse, impulse);
                }
            }

            for (auto const& constraint : constraints) {
                separateConstraint(constraint, states[constraint.a], states[constraint.b]);
                std::uint64_t const pair = (static_cast<std::uint64_t>(constraint.a) << 32) |
                    static_cast<std::uint64_t>(constraint.b);
                touchingPairs.insert(pair);
                if (!activePairs.contains(pair) && countedPairs.insert(pair).second) {
                    ++trace.impacts;
                }
            }
        }
        activePairs = std::move(touchingPairs);

        float const elapsed = std::min((step + 1) * fixedStep, options.duration);
        while (elapsed + 0.0001f >= nextSample && nextSample < options.duration) {
            trace.frames.push_back(snapshot(elapsed, states));
            nextSample += sampleStep;
        }
        if (elapsed >= options.duration - 0.0001f) break;
    }

    if (trace.frames.empty() || trace.frames.back().time < options.duration - 0.0001f) {
        trace.frames.push_back(snapshot(options.duration, states));
    } else {
        trace.frames.back().time = options.duration;
    }
    return trace;
}

} // namespace paimon::editorphysics
