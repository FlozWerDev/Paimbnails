#include "MenuPhysicsWorld.hpp"

#include <algorithm>
#include <cmath>

using namespace cocos2d;

namespace paimon::menuphysics {

namespace {
    constexpr float kGravityScale = 32.f;
    constexpr float kMaxDt = 1.f / 30.f;
    constexpr float kSleepAngVel = 18.f;
    constexpr float kSleepTime = 0.6f;
    constexpr int kSolverIterations = 4;
    constexpr float kSubstepTarget = 6.f;        // px per substep; caps tunneling
    constexpr float kDragVelLerp = 0.55f;
    constexpr float kPushTorqueFactor = 0.0008f;
    constexpr float kRestitutionCutoff = 60.f;   // px/s: below this, no bounce
    constexpr float kSleepVel = 10.f;
    constexpr int kMaxSubsteps = 4;
    constexpr float kPushRadius = 160.f;

    float clamp01(float v) { return std::clamp(v, 0.f, 1.f); }

    float len(CCPoint p) { return std::sqrt(p.x * p.x + p.y * p.y); }

    CCPoint norm(CCPoint p) {
        float l = len(p);
        if (l < 1e-5f) return {0.f, 0.f};
        return p / l;
    }

    float cross(CCPoint a, CCPoint b) { return a.x * b.y - a.y * b.x; }
}

void PhysicsWorld::configure(PhysicsConfig const& cfg) {
    m_cfg = cfg;
    m_cfg.bounciness = clamp01(cfg.bounciness);
    m_cfg.friction = clamp01(cfg.friction);
    m_cfg.airDrag = clamp01(cfg.airDrag);
    m_cfg.angularDrag = std::clamp(cfg.angularDrag, 0.f, 4.f);
    m_cfg.pushPower = std::clamp(cfg.pushPower, 0.f, 4.f);
}

void PhysicsWorld::setBounds(CCRect bounds) {
    m_bounds = bounds;
}

void PhysicsWorld::addBody(CCNode* node, CCPoint worldPos, CCSize worldSize,
                           CCPoint initialVel, float initialAngularVel) {
    if (!node) return;
    Body b;
    b.node = node;
    b.pos = worldPos;
    b.lastPos = worldPos;
    b.vel = initialVel;
    b.angularVel = initialAngularVel;
    b.halfW = std::max(2.f, worldSize.width * 0.5f * 0.9f);
    b.halfH = std::max(2.f, worldSize.height * 0.5f * 0.9f);
    float area = (2.f * b.halfW) * (2.f * b.halfH);
    b.invMass = m_cfg.massBySize ? 3600.f / std::max(400.f, area) : 1.f;
    m_bodies.push_back(b);
}

void PhysicsWorld::clear() {
    m_bodies.clear();
    m_dragIndex = -1;
}

void PhysicsWorld::wake(Body& b) {
    b.asleep = false;
    b.sleepTimer = 0.f;
}

void PhysicsWorld::integrate(float dt) {
    float g = m_cfg.gravity * kGravityScale;
    float linDamp = std::exp(-m_cfg.airDrag * dt * 3.f);
    float angDamp = std::exp(-m_cfg.angularDrag * dt * 3.f);

    for (int i = 0; i < static_cast<int>(m_bodies.size()); ++i) {
        if (i == m_dragIndex) continue;
        auto& b = m_bodies[i];
        if (b.asleep) continue;

        b.vel.y += g * dt;
        b.vel *= linDamp;
        b.angularVel *= angDamp;

        b.pos.x += b.vel.x * dt;
        b.pos.y += b.vel.y * dt;
        b.angle += b.angularVel * dt;
    }
}

void PhysicsWorld::collideWalls(Body& b) {
    float left = m_bounds.origin.x + b.halfW;
    float right = m_bounds.origin.x + m_bounds.size.width - b.halfW;
    float bottom = m_bounds.origin.y + b.halfH;
    float top = m_bounds.origin.y + m_bounds.size.height - b.halfH;

    auto bounce = [](float v, float e) {
        if (std::abs(v) < kRestitutionCutoff) return 0.f;
        return -v * e;
    };

    if (b.pos.x < left) {
        b.pos.x = left;
        if (b.vel.x < 0.f) {
            b.vel.x = bounce(b.vel.x, m_cfg.bounciness);
            b.vel.y *= (1.f - m_cfg.friction * 0.5f);
            b.angularVel += b.vel.y * 0.04f;
            wake(b);
        }
    } else if (b.pos.x > right) {
        b.pos.x = right;
        if (b.vel.x > 0.f) {
            b.vel.x = bounce(b.vel.x, m_cfg.bounciness);
            b.vel.y *= (1.f - m_cfg.friction * 0.5f);
            b.angularVel -= b.vel.y * 0.04f;
            wake(b);
        }
    }

    if (b.pos.y < bottom) {
        b.pos.y = bottom;
        if (b.vel.y < 0.f) {
            b.vel.y = bounce(b.vel.y, m_cfg.bounciness);
            b.vel.x *= (1.f - m_cfg.friction);
            b.angularVel += b.vel.x * 0.05f;
            if (std::abs(b.vel.y) < kSleepVel) b.vel.y = 0.f;
            wake(b);
        }
    } else if (!m_cfg.removeCeiling && b.pos.y > top) {
        b.pos.y = top;
        if (b.vel.y > 0.f) {
            b.vel.y = bounce(b.vel.y, m_cfg.bounciness);
            b.vel.x *= (1.f - m_cfg.friction);
            wake(b);
        }
    }
}

void PhysicsWorld::resolveBodyPair(Body& a, Body& b, int idxA, int idxB) {
    CCPoint d = b.pos - a.pos;
    float dx = d.x, dy = d.y;
    float overlapX = (a.halfW + b.halfW) - std::abs(dx);
    float overlapY = (a.halfH + b.halfH) - std::abs(dy);
    if (overlapX <= 0.f || overlapY <= 0.f) return;

    bool aFixed = (idxA == m_dragIndex);
    bool bFixed = (idxB == m_dragIndex);
    float ima = aFixed ? 0.f : a.invMass;
    float imb = bFixed ? 0.f : b.invMass;
    float invSum = ima + imb;
    if (invSum <= 0.f) return;  // ambos estaticos

    CCPoint n;
    float pen;
    if (overlapX < overlapY) {
        n = CCPoint{(dx < 0.f) ? -1.f : 1.f, 0.f};
        pen = overlapX;
    } else {
        n = CCPoint{0.f, (dy < 0.f) ? -1.f : 1.f};
        pen = overlapY;
    }

    // Correccion posicional (split por masa inversa).
    float corr = pen / invSum * 0.8f;  // 0.8 = Baumgarte suave
    a.pos -= n * (corr * ima);
    b.pos += n * (corr * imb);
    CCPoint rv = b.vel - a.vel;
    float vn = rv.x * n.x + rv.y * n.y;
    if (vn > 0.f) return;  // ya se estan separando

    float e = (std::abs(vn) < kRestitutionCutoff) ? 0.f : m_cfg.bounciness;
    float j = -(1.f + e) * vn / invSum;
    CCPoint impulse = n * j;
    a.vel -= impulse * ima;
    b.vel += impulse * imb;

    // Friccion tangencial (Coulomb simplificado).
    CCPoint t = CCPoint{n.y, -n.x};  // tangente
    float vt = rv.x * t.x + rv.y * t.y;
    float jt = -vt / invSum;
    float maxFriction = m_cfg.friction * std::abs(j);
    jt = std::clamp(jt, -maxFriction, maxFriction);
    CCPoint tImpulse = t * jt;
    a.vel -= tImpulse * ima;
    b.vel += tImpulse * imb;

    a.angularVel -= cross(n, impulse) * ima * 0.02f;
    b.angularVel += cross(n, impulse) * imb * 0.02f;

    wake(a);
    wake(b);
}

void PhysicsWorld::separateAndResolve() {
    int n = static_cast<int>(m_bodies.size());
    for (int iter = 0; iter < kSolverIterations; ++iter) {
        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                resolveBodyPair(m_bodies[i], m_bodies[j], i, j);
            }
        }
    }
}

void PhysicsWorld::updateSleep(Body& b, float dt) {
    if (b.asleep) return;
    float speed = len(b.vel);
    bool slow = (speed < kSleepVel) && (std::abs(b.angularVel) < kSleepAngVel);
    if (slow) {
        b.sleepTimer += dt;
        if (b.sleepTimer >= kSleepTime) {
            b.asleep = true;
            b.vel = CCPoint{0.f, 0.f};
            b.angularVel = 0.f;
        }
    } else {
        b.sleepTimer = 0.f;
    }
    b.lastPos = b.pos;
}

void PhysicsWorld::step(float dt) {
    if (m_bodies.empty()) return;
    if (dt <= 0.f) dt = 1.f / 60.f;
    dt = std::min(dt, kMaxDt);

    float maxSpeed = 0.f;
    for (auto const& b : m_bodies) {
        if (b.asleep) continue;
        maxSpeed = std::max(maxSpeed, len(b.vel));
    }
    int substeps = 1;
    if (maxSpeed * dt > kSubstepTarget) {
        substeps = std::min(kMaxSubsteps,
                            static_cast<int>(std::ceil((maxSpeed * dt) / kSubstepTarget)));
    }
    float h = dt / static_cast<float>(substeps);

    for (int s = 0; s < substeps; ++s) {
        integrate(h);
        separateAndResolve();
        for (auto& b : m_bodies) {
            if (b.asleep) continue;
            collideWalls(b);
        }
    }

    for (auto& b : m_bodies) {
        if (&b - m_bodies.data() == m_dragIndex) continue;
        updateSleep(b, dt);
    }
}

void PhysicsWorld::syncNodes() {
    for (auto& b : m_bodies) {
        auto* node = b.node.data();
        if (!node) continue;
        auto* parent = node->getParent();
        if (!parent) continue;
        node->setPosition(parent->convertToNodeSpace(b.pos));
        node->setRotation(-b.angle);
    }
}

bool PhysicsWorld::beginDrag(CCPoint p) {
    for (int i = static_cast<int>(m_bodies.size()) - 1; i >= 0; --i) {
        auto& b = m_bodies[i];
        if (std::abs(p.x - b.pos.x) <= b.halfW && std::abs(p.y - b.pos.y) <= b.halfH) {
            m_dragIndex = i;
            m_dragOffset = b.pos - p;
            wake(b);
            return true;
        }
    }
    return false;
}

void PhysicsWorld::moveDrag(CCPoint p, float dt) {
    if (m_dragIndex < 0 || m_dragIndex >= static_cast<int>(m_bodies.size())) return;
    auto& b = m_bodies[m_dragIndex];

    float left = m_bounds.origin.x + b.halfW;
    float right = m_bounds.origin.x + m_bounds.size.width - b.halfW;
    float bottom = m_bounds.origin.y + b.halfH;
    float top = m_bounds.origin.y + m_bounds.size.height - b.halfH;
    p.x = std::clamp(p.x, left, right);
    if (!m_cfg.removeCeiling) p.y = std::clamp(p.y, bottom, top);
    else p.y = std::max(p.y, bottom);

    CCPoint target = p + m_dragOffset;
    CCPoint delta = target - b.pos;

    if (dt > 0.f) {
        CCPoint targetVel = delta / dt;
        // Suavizado pasa-bajos: lance estable, no ruidoso.
        b.vel.x = std::lerp(b.vel.x, targetVel.x, kDragVelLerp);
        b.vel.y = std::lerp(b.vel.y, targetVel.y, kDragVelLerp);
        float torque = cross(m_dragOffset, delta) * kPushTorqueFactor;
        b.angularVel = std::lerp(b.angularVel, torque, kDragVelLerp);
    }
    b.pos = target;
}

void PhysicsWorld::endDrag() {
    m_dragIndex = -1;
}

CCNode* PhysicsWorld::draggedNode() const {
    if (m_dragIndex < 0 || m_dragIndex >= static_cast<int>(m_bodies.size())) return nullptr;
    return m_bodies[m_dragIndex].node.data();
}

void PhysicsWorld::pushExplosion(CCPoint worldPoint, float strength) {
    if (strength <= 0.f) return;
    for (auto& b : m_bodies) {
        CCPoint delta = b.pos - worldPoint;
        float dist = len(delta);
        if (dist >= kPushRadius || dist < 1e-3f) {
            if (dist < 1e-3f) {
                b.vel.y += 300.f * strength * b.invMass;
                b.angularVel += (std::rand() % 200 - 100) * strength;
                wake(b);
            }
            continue;
        }
        float falloff = 1.f - dist / kPushRadius;
        CCPoint dir = delta / dist;
        float impulseMag = strength * falloff * 600.f;
        b.vel += dir * (impulseMag * b.invMass);
        b.angularVel += cross(delta, dir * impulseMag) * 0.0003f;
        wake(b);
    }
}

} // namespace paimon::menuphysics
