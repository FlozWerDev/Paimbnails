#pragma once

#include <Geode/Geode.hpp>
#include <vector>

namespace paimon::menuphysics {

struct Body {
    geode::Ref<cocos2d::CCNode> node = nullptr;
    cocos2d::CCPoint pos {0.f, 0.f};      // center, world space
    cocos2d::CCPoint vel {0.f, 0.f};      // px/s
    float angle = 0.f;                    // degrees (visual spin)
    float angularVel = 0.f;               // deg/s
    float halfW = 0.f;
    float halfH = 0.f;
    float invMass = 1.f;                  // 0 = static (dragged)
    float invInertia = 1.f;               // 1/I
    bool asleep = false;
    float sleepTimer = 0.f;
    // base scale + impact deform (squash/stretch)
    float baseScaleX = 1.f;
    float baseScaleY = 1.f;
    float squash = 0.f;                   // 0..1, decays per frame
    float stretchAxis = 0.f;              // degrees: impact axis
    float supportOffsetY = 0.f;
};

struct PhysicsConfig {
    float gravity = -30.f;       // units (negative = down); scaled to px/s^2
    float bounciness = 0.35f;    // 0..1
    float friction = 0.45f;      // 0..1, tangential contact friction
    float airDrag = 0.08f;       // 0..1, linear air damping
    float angularDrag = 0.35f;   // 0..2, spin damping
    bool removeCeiling = false;
    bool massBySize = true;      // mass scales with area
};

class PhysicsWorld {
public:
    void configure(PhysicsConfig const& cfg);
    void setBounds(cocos2d::CCRect bounds);

    void addBody(cocos2d::CCNode* node, cocos2d::CCPoint worldPos,
                 cocos2d::CCSize worldSize, cocos2d::CCPoint initialVel,
                 float initialAngularVel, float initialAngle = 0.f);
    void clear();
    bool empty() const { return m_bodies.empty(); }

    void step(float dt);
    void syncNodes();

    bool beginDrag(cocos2d::CCPoint worldPoint);
    void moveDrag(cocos2d::CCPoint worldPoint, float dt);
    void endDrag();
    bool isDragging() const { return m_dragIndex >= 0; }
    cocos2d::CCNode* draggedNode() const;

    void pushExplosion(cocos2d::CCPoint worldPoint, float strength);

private:
    void integrate(float dt);
    void collideWalls(Body& b);
    void resolveBodyPair(Body& a, Body& b, int idxA, int idxB);
    void separateAndResolve();
    void applyRolling(Body& b, float dt);
    void decayVisuals(Body& b, float dt);
    void updateSleep(Body& b, float dt);
    void wake(Body& b);
    void applyImpulse(Body& b, cocos2d::CCPoint impulse, cocos2d::CCPoint r);
    void registerImpact(Body& b, float speed, float normalAngleDeg);

    std::vector<Body> m_bodies;
    cocos2d::CCRect m_bounds {0.f, 0.f, 0.f, 0.f};

    PhysicsConfig m_cfg;

    int m_dragIndex = -1;
    cocos2d::CCPoint m_dragOffset {0.f, 0.f};   // center->cursor offset on grab
    cocos2d::CCPoint m_prevDragPos {0.f, 0.f};
    cocos2d::CCPoint m_dragVel {0.f, 0.f};
};

} // namespace paimon::menuphysics
