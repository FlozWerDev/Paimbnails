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

// An oriented box. The previous solver approximated rotated fixtures with an
// enlarged axis-aligned box, which made spinning bodies collide with thin air.
struct Obb {
    Vec2 center;
    Vec2 axisX;
    Vec2 axisY;
    Vec2 halfSize;
};

struct Manifold {
    Vec2 normal;
    Vec2 points[2];
    float penetration[2];
    int count = 0;
};

Obb worldFixture(State const& state, Fixture const& fixture) {
    float const cosine = std::cos(state.angle);
    float const sine = std::sin(state.angle);
    return {
        state.position + rotate(fixture.offset, state.angle),
        {cosine, sine},
        {-sine, cosine},
        fixture.halfSize,
    };
}

float projectedRadius(Obb const& box, Vec2 axis) {
    return std::abs(dot(box.axisX, axis)) * box.halfSize.x +
        std::abs(dot(box.axisY, axis)) * box.halfSize.y;
}

void cornersOf(Obb const& box, Vec2 out[4]) {
    Vec2 const ex = box.axisX * box.halfSize.x;
    Vec2 const ey = box.axisY * box.halfSize.y;
    out[0] = box.center - ex - ey;
    out[1] = box.center + ex - ey;
    out[2] = box.center + ex + ey;
    out[3] = box.center - ex + ey;
}

Vec2 edgeNormal(Vec2 from, Vec2 to) {
    Vec2 const edge = to - from;
    return normalized({edge.y, -edge.x});
}

int faceMatching(Vec2 const corners[4], Vec2 direction, bool maximize) {
    int best = 0;
    float bestDot = maximize ? -std::numeric_limits<float>::max()
                             : std::numeric_limits<float>::max();
    for (int i = 0; i < 4; ++i) {
        float const value = dot(edgeNormal(corners[i], corners[(i + 1) % 4]), direction);
        if (maximize ? value > bestDot : value < bestDot) {
            bestDot = value;
            best = i;
        }
    }
    return best;
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
bool buildManifold(Obb const& a, Obb const& b, Manifold& manifold) {
    Vec2 const delta = b.center - a.center;
    Vec2 const axes[4] = {a.axisX, a.axisY, b.axisX, b.axisY};
    float bestDepth = std::numeric_limits<float>::max();
    int bestAxis = 0;
    for (int i = 0; i < 4; ++i) {
        float const depth = projectedRadius(a, axes[i]) + projectedRadius(b, axes[i]) -
            std::abs(dot(delta, axes[i]));
        if (depth <= 0.f) return false;
        if (depth < bestDepth) {
            bestDepth = depth;
            bestAxis = i;
        }
    }

    Vec2 normal = axes[bestAxis];
    if (dot(delta, normal) < 0.f) normal = normal * -1.f;

    bool const referenceIsA = bestAxis < 2;
    Obb const& reference = referenceIsA ? a : b;
    Obb const& incident = referenceIsA ? b : a;
    Vec2 const referenceNormal = referenceIsA ? normal : normal * -1.f;

    Vec2 referenceCorners[4];
    Vec2 incidentCorners[4];
    cornersOf(reference, referenceCorners);
    cornersOf(incident, incidentCorners);

    int const referenceFace = faceMatching(referenceCorners, referenceNormal, true);
    int const incidentFace = faceMatching(incidentCorners, referenceNormal, false);

    Vec2 const start = referenceCorners[referenceFace];
    Vec2 const end = referenceCorners[(referenceFace + 1) % 4];
    Vec2 const faceNormal = edgeNormal(start, end);
    Vec2 const tangent = normalized(end - start);

    Vec2 segment[2] = {incidentCorners[incidentFace], incidentCorners[(incidentFace + 1) % 4]};
    Vec2 clipped[2];
    if (clipSegment(segment, tangent * -1.f, dot(start, tangent * -1.f), clipped) < 2) return false;
    segment[0] = clipped[0];
    segment[1] = clipped[1];
    if (clipSegment(segment, tangent, dot(end, tangent), clipped) < 2) return false;

    manifold.normal = normal;
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
        manifold.penetration[0] = bestDepth;
        manifold.count = 1;
    }
    return true;
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

float boundingRadius(BodySpec const& body) {
    float radius = 0.f;
    for (auto const& fixture : body.fixtures) {
        radius = std::max(radius, length(fixture.offset) + length(fixture.halfSize));
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
            thinnest = std::min(thinnest, std::min(fixture.halfSize.x, fixture.halfSize.y));
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
                        Obb const boxA = worldFixture(states[i], fixtureA);
                        for (auto const& fixtureB : bodies[j].fixtures) {
                            Obb const boxB = worldFixture(states[j], fixtureB);
                            Manifold manifold;
                            if (!buildManifold(boxA, boxB, manifold)) continue;

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
