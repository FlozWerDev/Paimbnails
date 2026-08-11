#include <array>
#include <cmath>
#include <iostream>

#define private public
#include "../src/features/menu-physics/services/MenuPhysicsWorld.hpp"
#undef private

#include "../src/features/menu-physics/services/MenuPhysicsWorld.cpp"

using namespace cocos2d;
using namespace paimon::menuphysics;

namespace {
    constexpr float kFrameTime = 1.f / 60.f;
    constexpr float kDegToRad = 0.01745329252f;

    PhysicsWorld makeWorld(float gravity = -32.f) {
        PhysicsWorld world;
        PhysicsConfig config;
        config.gravity = gravity;
        config.bounciness = 0.35f;
        config.friction = 0.45f;
        config.airDrag = 0.08f;
        config.angularDrag = 0.35f;
        world.configure(config);
        world.setBounds({0.f, 0.f, 320.f, 240.f});
        return world;
    }

    bool restingBodySleeps() {
        auto world = makeWorld();
        CCNode node;
        world.addBody(&node, {160.f, 18.f}, {40.f, 40.f}, {0.f, 0.f}, 360.f);

        for (int i = 0; i < 8 * 60; ++i) {
            world.step(kFrameTime);
        }

        auto const& body = world.m_bodies.front();
        std::cout << "rest: asleep=" << body.asleep
                  << " angularVel=" << body.angularVel
                  << " speed=" << std::hypot(body.vel.x, body.vel.y)
                  << " sleepTimer=" << body.sleepTimer << '\n';
        return body.asleep && body.angularVel == 0.f;
    }

    bool groundContactDoesNotAddSpin() {
        auto world = makeWorld();
        CCNode node;
        world.addBody(&node, {160.f, 18.f}, {40.f, 40.f}, {-113.097f, 0.f}, 360.f);

        float const before = std::abs(world.m_bodies.front().angularVel);
        world.step(kFrameTime);
        float const after = std::abs(world.m_bodies.front().angularVel);

        std::cout << "contact: angularVel " << before << " -> " << after << '\n';
        return after <= before;
    }

    float angularVelocityAfterContact(CCPoint position, CCPoint velocity) {
        auto world = makeWorld();
        CCNode node;
        world.addBody(&node, position, {40.f, 40.f}, velocity, 360.f);
        world.collideWalls(world.m_bodies.front());
        return world.m_bodies.front().angularVel;
    }

    bool noSlipContactsPreserveSpin() {
        float const rollingSpeed = 360.f * kDegToRad * 18.f;
        float const left = angularVelocityAfterContact({17.f, 120.f}, {-1.f, rollingSpeed});
        float const right = angularVelocityAfterContact({303.f, 120.f}, {1.f, -rollingSpeed});
        float const floor = angularVelocityAfterContact({160.f, 17.f}, {-rollingSpeed, -1.f});
        float const ceiling = angularVelocityAfterContact({160.f, 223.f}, {rollingSpeed, 1.f});

        std::cout << "no-slip: left=" << left
                  << " right=" << right
                  << " floor=" << floor
                  << " ceiling=" << ceiling << '\n';

        auto unchanged = [](float value) { return std::abs(value - 360.f) < 0.1f; };
        return unchanged(left) && unchanged(right) && unchanged(floor) && unchanged(ceiling);
    }

    bool settledPileSleeps() {
        auto world = makeWorld();
        std::array<CCNode, 12> nodes;

        for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
            float const x = 105.f + static_cast<float>(i % 4) * 36.f;
            float const y = 70.f + static_cast<float>(i / 4) * 36.f;
            float const vx = static_cast<float>((i % 3) - 1) * 45.f;
            float const spin = (i % 2 == 0) ? 420.f : -360.f;
            world.addBody(&nodes[i], {x, y}, {40.f, 40.f}, {vx, 0.f}, spin);
        }

        for (int i = 0; i < 20 * 60; ++i) {
            world.step(kFrameTime);
        }

        int awake = 0;
        float maxSpin = 0.f;
        for (int i = 0; i < static_cast<int>(world.m_bodies.size()); ++i) {
            auto const& body = world.m_bodies[i];
            if (!body.asleep) ++awake;
            maxSpin = std::max(maxSpin, std::abs(body.angularVel));
            if (!body.asleep) {
                std::cout << "  body " << i
                          << " pos=(" << body.pos.x << ',' << body.pos.y << ')'
                          << " vel=(" << body.vel.x << ',' << body.vel.y << ')'
                          << " angularVel=" << body.angularVel
                          << " supportY=" << body.supportOffsetY
                          << " sleepTimer=" << body.sleepTimer << '\n';
            }
        }

        std::cout << "pile: awake=" << awake << '/' << world.m_bodies.size()
                  << " maxAngularVel=" << maxSpin << '\n';
        return awake == 0;
    }

    bool twoBodyStackSleeps() {
        auto world = makeWorld();
        std::array<CCNode, 2> nodes;
        world.addBody(&nodes[0], {160.f, 18.f}, {40.f, 40.f}, {0.f, 0.f}, 0.f);
        world.addBody(&nodes[1], {160.f, 54.f}, {40.f, 40.f}, {0.f, 0.f}, 0.f);

        for (int i = 0; i < 5 * 60; ++i) {
            world.step(kFrameTime);
        }

        auto const& lower = world.m_bodies[0];
        auto const& upper = world.m_bodies[1];
        std::cout << "stack: lowerAsleep=" << lower.asleep
                  << " upperAsleep=" << upper.asleep
                  << " upperVy=" << upper.vel.y << '\n';
        return lower.asleep && upper.asleep;
    }

    bool invertedGravitySleeps() {
        auto world = makeWorld(32.f);
        CCNode node;
        world.addBody(&node, {160.f, 222.f}, {40.f, 40.f}, {0.f, 0.f}, 360.f);

        for (int i = 0; i < 8 * 60; ++i) {
            world.step(kFrameTime);
        }

        auto const& body = world.m_bodies.front();
        std::cout << "inverted: asleep=" << body.asleep
                  << " angularVel=" << body.angularVel
                  << " sleepTimer=" << body.sleepTimer << '\n';
        return body.asleep;
    }

    bool zeroGravitySleeps() {
        auto world = makeWorld(0.f);
        CCNode node;
        world.addBody(&node, {160.f, 120.f}, {40.f, 40.f}, {0.f, 0.f}, 360.f);

        for (int i = 0; i < 12 * 60; ++i) {
            world.step(kFrameTime);
        }

        auto const& body = world.m_bodies.front();
        std::cout << "zero-g: asleep=" << body.asleep
                  << " angularVel=" << body.angularVel
                  << " sleepTimer=" << body.sleepTimer << '\n';
        return body.asleep;
    }

    bool liveGravityChangeWakesBodies() {
        auto world = makeWorld();
        CCNode node;
        world.addBody(&node, {160.f, 18.f}, {40.f, 40.f}, {0.f, 0.f}, 0.f);

        for (int i = 0; i < 2 * 60; ++i) {
            world.step(kFrameTime);
        }

        auto config = world.m_cfg;
        config.gravity = 32.f;
        world.configure(config);
        world.step(kFrameTime);

        auto const& body = world.m_bodies.front();
        std::cout << "live-gravity: asleep=" << body.asleep
                  << " vy=" << body.vel.y << '\n';
        return !body.asleep && body.vel.y > 0.f;
    }

    bool liveMassModeUpdatesBodies() {
        auto world = makeWorld();
        CCNode node;
        world.addBody(&node, {160.f, 120.f}, {80.f, 80.f}, {0.f, 0.f}, 0.f);
        float const sizedInvMass = world.m_bodies.front().invMass;

        auto config = world.m_cfg;
        config.massBySize = false;
        world.configure(config);
        float const uniformInvMass = world.m_bodies.front().invMass;

        std::cout << "live-mass: invMass " << sizedInvMass
                  << " -> " << uniformInvMass << '\n';
        return sizedInvMass != uniformInvMass && uniformInvMass == 1.f;
    }
}

int main() {
    bool const sleeps = restingBodySleeps();
    bool const dissipates = groundContactDoesNotAddSpin();
    bool const contactsPreserveSpin = noSlipContactsPreserveSpin();
    bool const stackSleeps = twoBodyStackSleeps();
    bool const pileSleeps = settledPileSleeps();
    bool const invertedSleeps = invertedGravitySleeps();
    bool const zeroGravitySettles = zeroGravitySleeps();
    bool const liveGravityWorks = liveGravityChangeWakesBodies();
    bool const liveMassWorks = liveMassModeUpdatesBodies();

    if (!sleeps) std::cerr << "FAIL: a resting button never entered sleep\n";
    if (!dissipates) std::cerr << "FAIL: ground contact added angular energy\n";
    if (!contactsPreserveSpin) std::cerr << "FAIL: a no-slip wall contact changed spin\n";
    if (!stackSleeps) std::cerr << "FAIL: a two-button stack never entered sleep\n";
    if (!pileSleeps) std::cerr << "FAIL: a settled button pile never entered sleep\n";
    if (!invertedSleeps) std::cerr << "FAIL: inverted gravity never entered sleep\n";
    if (!zeroGravitySettles) std::cerr << "FAIL: zero gravity never entered sleep\n";
    if (!liveGravityWorks) std::cerr << "FAIL: a live gravity change left bodies asleep\n";
    if (!liveMassWorks) std::cerr << "FAIL: live mass mode did not update existing bodies\n";
    return sleeps && dissipates && contactsPreserveSpin && stackSleeps && pileSleeps &&
           invertedSleeps && zeroGravitySettles && liveGravityWorks && liveMassWorks ? 0 : 1;
}
