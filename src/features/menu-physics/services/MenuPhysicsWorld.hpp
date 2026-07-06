#pragma once

#include <Geode/Geode.hpp>
#include <vector>

namespace paimon::menuphysics {

struct Body {
    geode::Ref<cocos2d::CCNode> node = nullptr;
    cocos2d::CCPoint pos {0.f, 0.f};      // centro, espacio mundo
    cocos2d::CCPoint vel {0.f, 0.f};      // px/s
    float angle = 0.f;                    // grados (giro visual)
    float angularVel = 0.f;               // deg/s
    float halfW = 0.f;
    float halfH = 0.f;
    float invMass = 1.f;                  // 0 = estatico (arrastrado)
    bool asleep = false;
    float sleepTimer = 0.f;
    cocos2d::CCPoint lastPos {0.f, 0.f};
};

struct PhysicsConfig {
    float gravity = -30.f;       // unidades (negativo = abajo); se escala a px/s^2
    float bounciness = 0.2f;     // 0..1
    float friction = 0.4f;       // 0..1, friccion tangencial en contactos
    float airDrag = 0.1f;        // 0..1, amortiguacion lineal en aire
    float angularDrag = 0.6f;    // 0..2, amortiguacion de giro
    bool removeCeiling = false;
    float pushPower = 1.0f;      // 0..2, fuerza del empujon al click en vacio
    bool massBySize = true;      // masa proporcional al area
};

class PhysicsWorld {
public:
    void configure(PhysicsConfig const& cfg);
    void setBounds(cocos2d::CCRect bounds);

    void addBody(cocos2d::CCNode* node, cocos2d::CCPoint worldPos,
                 cocos2d::CCSize worldSize, cocos2d::CCPoint initialVel,
                 float initialAngularVel);
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
    void updateSleep(Body& b, float dt);
    void wake(Body& b);

    std::vector<Body> m_bodies;
    cocos2d::CCRect m_bounds {0.f, 0.f, 0.f, 0.f};

    PhysicsConfig m_cfg;

    int m_dragIndex = -1;
    cocos2d::CCPoint m_dragOffset {0.f, 0.f};   // offset centro->cursor al agarrar
};

} // namespace paimon::menuphysics
