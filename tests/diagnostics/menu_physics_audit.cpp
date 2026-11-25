#include <array>
#include <cmath>
#include <iostream>

// Same inspection technique as the existing menu regression harness.
#define private public
#include "../../src/features/menu-physics/services/MenuPhysicsWorld.hpp"
#undef private
#include "../../src/features/menu-physics/services/MenuPhysicsWorld.cpp"

using namespace cocos2d;
using namespace paimon::menuphysics;

int main() {
    PhysicsWorld world;
    PhysicsConfig config;
    config.bounciness = 0.f;
    world.configure(config);
    world.setBounds({0.f, 0.f, 320.f, 240.f});
    CCNode lower;
    CCNode upper;
    world.addBody(&lower, {160.f, 18.f}, {40.f, 40.f}, {}, 0.f);
    world.addBody(&upper, {160.f, 54.f}, {40.f, 40.f}, {}, 0.f);
    for (int frame = 0; frame < 480; ++frame) world.step(1.f / 60.f);
    bool const setupSettled = world.m_bodies[0].asleep && world.m_bodies[1].asleep;
    float const before = world.m_bodies[1].pos.y;
    bool const grabbed = world.beginDrag(world.m_bodies[0].pos);
    world.moveDrag({250.f, 18.f}, 1.f / 60.f);
    world.endDrag();
    for (int frame = 0; frame < 120; ++frame) world.step(1.f / 60.f);
    auto const& body = world.m_bodies[1];
    bool const passed = setupSettled && grabbed && body.pos.y < 20.f;
    std::cout << "setupSettled=" << setupSettled << " grabbed=" << grabbed
              << " upperYBefore=" << before << " upperYAfter=" << body.pos.y
              << " upperAsleep=" << body.asleep << '\n';
    std::cout << (passed ? "[PASS] " : "[FAIL] ")
              << "menu body falls when its support is dragged away\n";
    return passed ? 0 : 1;
}
