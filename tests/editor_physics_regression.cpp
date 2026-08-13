#include <cmath>
#include <iostream>
#include <vector>

#include "../src/features/editor-physics/PhysicsTypes.hpp"
#include "../src/features/editor-physics/services/PhysicsSolver.cpp"

using namespace paimon::editorphysics;

namespace {

BodySpec box(Motion motion, Vec2 position, Vec2 halfSize) {
    BodySpec body;
    body.motion = motion;
    body.position = position;
    body.fixtures.push_back({{}, halfSize});
    return body;
}

BodySpec circle(Motion motion, Vec2 position, float radius) {
    BodySpec body;
    body.motion = motion;
    body.position = position;
    Fixture fixture;
    fixture.halfSize = {radius, radius};
    fixture.radius = radius;
    body.fixtures.push_back(fixture);
    return body;
}

// A right triangle filling the given box, with the walkable surface running from
// the bottom-left up to the top-right, like GD's slope 289.
BodySpec ramp(Motion motion, Vec2 position, Vec2 halfSize) {
    BodySpec body;
    body.motion = motion;
    body.position = position;
    Fixture fixture;
    fixture.halfSize = halfSize;
    fixture.vertexCount = 3;
    fixture.vertices[0] = {-halfSize.x, -halfSize.y};
    fixture.vertices[1] = {halfSize.x, -halfSize.y};
    fixture.vertices[2] = {halfSize.x, halfSize.y};
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

bool gravityMovesDynamicBody() {
    auto trace = simulate({box(Motion::Dynamic, {0.f, 100.f}, {10.f, 10.f})}, options(1.f));
    bool const pass = trace.frames.size() == 31 &&
        trace.frames.back().poses.front().position.y < -300.f;
    std::cout << "gravity: y=" << trace.frames.back().poses.front().position.y << '\n';
    return pass;
}

bool staticFloorStopsFall() {
    auto falling = box(Motion::Dynamic, {0.f, 90.f}, {10.f, 10.f});
    falling.restitution = 0.f;
    falling.friction = 0.8f;
    auto floor = box(Motion::Static, {0.f, 0.f}, {120.f, 10.f});
    floor.restitution = 0.f;
    floor.friction = 0.8f;
    auto trace = simulate({falling, floor}, options(2.f));
    float const y = trace.frames.back().poses.front().position.y;
    bool const pass = y > 18.f && y < 24.f && trace.impacts > 0;
    std::cout << "floor: y=" << y << " impacts=" << trace.impacts << '\n';
    return pass;
}

bool restitutionBounces() {
    auto falling = box(Motion::Dynamic, {0.f, 70.f}, {10.f, 10.f});
    falling.restitution = 0.85f;
    auto floor = box(Motion::Static, {0.f, 0.f}, {120.f, 10.f});
    floor.restitution = 0.85f;
    auto trace = simulate({falling, floor}, options(0.8f));
    float highestAfterImpact = -10000.f;
    bool hit = false;
    for (auto const& frame : trace.frames) {
        float const y = frame.poses.front().position.y;
        if (y < 24.f) hit = true;
        if (hit) highestAfterImpact = std::max(highestAfterImpact, y);
    }
    bool const pass = hit && highestAfterImpact > 35.f;
    std::cout << "bounce: peak=" << highestAfterImpact << '\n';
    return pass;
}

bool compoundBodyMovesAsOne() {
    auto compound = box(Motion::Dynamic, {0.f, 100.f}, {10.f, 10.f});
    compound.fixtures = {
        {{-15.f, 0.f}, {10.f, 10.f}},
        {{15.f, 0.f}, {10.f, 10.f}},
    };
    auto floor = box(Motion::Static, {0.f, 0.f}, {120.f, 10.f});
    auto trace = simulate({compound, floor}, options(1.f));
    bool const pass = trace.frames.front().poses.size() == 2 &&
        trace.frames.back().poses.front().position.y > 18.f &&
        std::isfinite(trace.frames.back().poses.front().angle);
    std::cout << "compound: y=" << trace.frames.back().poses.front().position.y << '\n';
    return pass;
}

bool dynamicBodiesExchangeMomentum() {
    auto left = box(Motion::Dynamic, {-50.f, 30.f}, {10.f, 10.f});
    left.velocity = {120.f, 0.f};
    left.restitution = 1.f;
    auto right = box(Motion::Dynamic, {0.f, 30.f}, {10.f, 10.f});
    right.restitution = 1.f;
    auto config = options(0.5f);
    config.gravity = {};
    config.airDrag = 0.f;
    auto trace = simulate({left, right}, config);
    float const rightX = trace.frames.back().poses[1].position.x;
    bool const pass = rightX > 20.f && trace.impacts > 0;
    std::cout << "multiple: right-x=" << rightX << '\n';
    return pass;
}

bool gravityCanBePerBody() {
    auto falling = box(Motion::Dynamic, {0.f, 100.f}, {10.f, 10.f});
    auto reactive = box(Motion::Dynamic, {100.f, 100.f}, {10.f, 10.f});
    reactive.gravityScale = 0.f;
    auto trace = simulate({falling, reactive}, options(0.5f));
    float const fallingY = trace.frames.back().poses[0].position.y;
    float const reactiveY = trace.frames.back().poses[1].position.y;
    bool const pass = fallingY < 10.f && std::abs(reactiveY - 100.f) < 0.1f;
    std::cout << "gravity-scale: falling=" << fallingY << " reactive=" << reactiveY << '\n';
    return pass;
}

bool reactiveBodyReceivesImpact() {
    auto falling = box(Motion::Dynamic, {0.f, 80.f}, {10.f, 10.f});
    falling.restitution = 0.2f;
    auto reactive = box(Motion::Dynamic, {0.f, 20.f}, {10.f, 10.f});
    reactive.gravityScale = 0.f;
    reactive.restitution = 0.2f;
    auto trace = simulate({falling, reactive}, options(0.7f));
    float const reactiveY = trace.frames.back().poses[1].position.y;
    bool const pass = trace.impacts > 0 && reactiveY < 10.f;
    std::cout << "reactive: y=" << reactiveY << " impacts=" << trace.impacts << '\n';
    return pass;
}

bool fastBodyDoesNotTunnel() {
    auto bullet = box(Motion::Dynamic, {0.f, 400.f}, {6.f, 6.f});
    bullet.velocity = {0.f, -1500.f};
    bullet.restitution = 0.f;
    auto floor = box(Motion::Static, {0.f, 0.f}, {200.f, 5.f});
    floor.restitution = 0.f;
    auto config = options(1.f);
    config.gravity = {0.f, -2000.f};
    auto trace = simulate({bullet, floor}, config);
    float const y = trace.frames.back().poses.front().position.y;
    bool const pass = y > 0.f && trace.impacts > 0;
    std::cout << "tunnel: y=" << y << " impacts=" << trace.impacts << '\n';
    return pass;
}

bool tiltedBoxTipsFlat() {
    auto tilted = box(Motion::Dynamic, {0.f, 60.f}, {10.f, 10.f});
    tilted.angle = 0.35f;
    tilted.restitution = 0.f;
    tilted.friction = 0.9f;
    auto floor = box(Motion::Static, {0.f, 0.f}, {200.f, 10.f});
    floor.restitution = 0.f;
    floor.friction = 0.9f;
    auto trace = simulate({tilted, floor}, options(3.f));
    auto const& pose = trace.frames.back().poses.front();
    // An inflated AABB holds a 20-degree box at ~22.8 and never lets it tip; a real
    // corner contact rotates it flat and settles the centre at ~20.
    bool const pass = pose.position.y > 18.f && pose.position.y < 21.5f &&
        std::abs(pose.angle) < 0.1f;
    std::cout << "tilted: y=" << pose.position.y << " angle=" << pose.angle << '\n';
    return pass;
}

bool restingBodyStopsBouncing() {
    auto falling = box(Motion::Dynamic, {0.f, 120.f}, {10.f, 10.f});
    falling.restitution = 0.6f;
    auto floor = box(Motion::Static, {0.f, 0.f}, {200.f, 10.f});
    floor.restitution = 0.6f;
    auto trace = simulate({falling, floor}, options(6.f));
    float lastY = trace.frames.back().poses.front().position.y;
    float drift = 0.f;
    for (std::size_t i = trace.frames.size() - 20; i < trace.frames.size(); ++i) {
        drift = std::max(drift, std::abs(trace.frames[i].poses.front().position.y - lastY));
    }
    bool const pass = drift < 1.f && lastY > 18.f && lastY < 22.f;
    std::cout << "settle: y=" << lastY << " drift=" << drift << '\n';
    return pass;
}

bool spinKeepsAccumulating() {
    auto spinner = box(Motion::Dynamic, {0.f, 500.f}, {10.f, 10.f});
    spinner.angularVelocity = 12.f;
    auto floor = box(Motion::Static, {0.f, -4000.f}, {200.f, 10.f});
    auto config = options(3.f);
    config.angularDrag = 0.f;
    auto trace = simulate({spinner, floor}, config);
    float previous = trace.frames.front().poses.front().angle;
    float biggestJump = 0.f;
    for (auto const& frame : trace.frames) {
        float const angle = frame.poses.front().angle;
        biggestJump = std::max(biggestJump, std::abs(angle - previous));
        previous = angle;
    }
    // Wrapping into -pi..pi used to produce ~6.28 jumps, which snap a baked rotation.
    bool const pass = biggestJump < 1.f && std::abs(previous) > 30.f;
    std::cout << "spin: angle=" << previous << " jump=" << biggestJump << '\n';
    return pass;
}

bool rampSlidesInsteadOfBlocking() {
    auto falling = box(Motion::Dynamic, {0.f, 120.f}, {8.f, 8.f});
    falling.restitution = 0.f;
    falling.friction = 0.05f;
    auto slope = ramp(Motion::Static, {0.f, 0.f}, {120.f, 60.f});
    slope.restitution = 0.f;
    slope.friction = 0.05f;
    auto trace = simulate({falling, slope}, options(0.7f));
    auto const& pose = trace.frames.back().poses.front();
    // The hypotenuse runs y = x/2, so a body riding it stays near that line and
    // drifts to the left. An axis-aligned box would have parked it at y=68.
    float const surface = pose.position.x * 0.5f;
    bool const pass = trace.impacts > 0 && pose.position.x < -10.f &&
        std::abs(pose.position.y - surface) < 20.f;
    std::cout << "ramp: x=" << pose.position.x << " y=" << pose.position.y
              << " surface=" << surface << '\n';
    return pass;
}

bool rampKeepsItsEmptyCornerEmpty() {
    // Sitting inside the missing corner of the triangle: a box hitbox would have
    // pushed this body out, a triangle leaves it where it is.
    auto inside = box(Motion::Dynamic, {-40.f, 40.f}, {6.f, 6.f});
    inside.gravityScale = 0.f;
    auto slope = ramp(Motion::Static, {0.f, 0.f}, {60.f, 60.f});
    auto config = options(0.5f);
    config.airDrag = 0.f;
    auto trace = simulate({inside, slope}, config);
    auto const& pose = trace.frames.back().poses.front();
    bool const pass = trace.impacts == 0 &&
        std::abs(pose.position.x + 40.f) < 0.01f && std::abs(pose.position.y - 40.f) < 0.01f;
    std::cout << "ramp-gap: x=" << pose.position.x << " y=" << pose.position.y
              << " impacts=" << trace.impacts << '\n';
    return pass;
}

bool circleRestsOnItsRadius() {
    auto ball = circle(Motion::Dynamic, {0.f, 90.f}, 12.f);
    ball.restitution = 0.f;
    ball.friction = 0.5f;
    auto floor = box(Motion::Static, {0.f, 0.f}, {120.f, 10.f});
    floor.restitution = 0.f;
    floor.friction = 0.5f;
    auto trace = simulate({ball, floor}, options(2.f));
    float const y = trace.frames.back().poses.front().position.y;
    bool const pass = trace.impacts > 0 && y > 20.f && y < 24.f;
    std::cout << "circle: y=" << y << '\n';
    return pass;
}

bool circlesPushEachOtherApart() {
    auto left = circle(Motion::Dynamic, {-30.f, 0.f}, 12.f);
    left.velocity = {150.f, 0.f};
    left.restitution = 1.f;
    auto right = circle(Motion::Dynamic, {0.f, 0.f}, 12.f);
    right.restitution = 1.f;
    auto config = options(0.5f);
    config.gravity = {};
    config.airDrag = 0.f;
    auto trace = simulate({left, right}, config);
    float const rightX = trace.frames.back().poses[1].position.x;
    bool const pass = trace.impacts > 0 && rightX > 25.f;
    std::cout << "circle-pair: right-x=" << rightX << '\n';
    return pass;
}

bool circleRollsOffAnOrb() {
    // Dropped just off-centre onto a static orb: a round hitbox deflects it
    // sideways, a square one would have balanced it on a flat top.
    auto ball = circle(Motion::Dynamic, {4.f, 80.f}, 10.f);
    ball.restitution = 0.2f;
    ball.friction = 0.2f;
    auto orb = circle(Motion::Static, {0.f, 0.f}, 15.f);
    orb.restitution = 0.2f;
    orb.friction = 0.2f;
    auto trace = simulate({ball, orb}, options(1.5f));
    auto const& pose = trace.frames.back().poses.front();
    bool const pass = trace.impacts > 0 && pose.position.x > 20.f && pose.position.y < 0.f;
    std::cout << "orb-roll: x=" << pose.position.x << " y=" << pose.position.y << '\n';
    return pass;
}

} // namespace

int main() {
    bool const gravity = gravityMovesDynamicBody();
    bool const floor = staticFloorStopsFall();
    bool const bounce = restitutionBounces();
    bool const compound = compoundBodyMovesAsOne();
    bool const multiple = dynamicBodiesExchangeMomentum();
    bool const gravityScale = gravityCanBePerBody();
    bool const reactive = reactiveBodyReceivesImpact();
    bool const tunnel = fastBodyDoesNotTunnel();
    bool const rotated = tiltedBoxTipsFlat();
    bool const settle = restingBodyStopsBouncing();
    bool const spin = spinKeepsAccumulating();
    bool const rampSlide = rampSlidesInsteadOfBlocking();
    bool const rampGap = rampKeepsItsEmptyCornerEmpty();
    bool const ball = circleRestsOnItsRadius();
    bool const ballPair = circlesPushEachOtherApart();
    bool const ballRoll = circleRollsOffAnOrb();

    if (!rampSlide) std::cerr << "FAIL: slope does not act as a ramp\n";
    if (!rampGap) std::cerr << "FAIL: slope collides inside its empty corner\n";
    if (!ball) std::cerr << "FAIL: circle resting height\n";
    if (!ballPair) std::cerr << "FAIL: circle against circle\n";
    if (!ballRoll) std::cerr << "FAIL: circle does not roll off a round hitbox\n";
    if (!tunnel) std::cerr << "FAIL: fast body tunnels through static geometry\n";
    if (!rotated) std::cerr << "FAIL: tilted box does not tip flat\n";
    if (!settle) std::cerr << "FAIL: resting body never settles\n";
    if (!spin) std::cerr << "FAIL: accumulated spin angle\n";
    if (!gravity) std::cerr << "FAIL: gravity integration\n";
    if (!floor) std::cerr << "FAIL: static floor collision\n";
    if (!bounce) std::cerr << "FAIL: restitution\n";
    if (!compound) std::cerr << "FAIL: compound body\n";
    if (!multiple) std::cerr << "FAIL: dynamic body collision\n";
    if (!gravityScale) std::cerr << "FAIL: per-body gravity\n";
    if (!reactive) std::cerr << "FAIL: reactive body impact\n";
    return gravity && floor && bounce && compound && multiple && gravityScale && reactive &&
        tunnel && rotated && settle && spin && rampSlide && rampGap && ball && ballPair &&
        ballRoll ? 0 : 1;
}
