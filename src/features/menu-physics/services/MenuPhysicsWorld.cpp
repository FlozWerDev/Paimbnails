#include "MenuPhysicsWorld.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

using namespace cocos2d;

namespace paimon::menuphysics {

namespace {
    constexpr float kGravityScale = 32.f;
    constexpr float kMaxDt = 1.f / 30.f;
    constexpr float kSleepAngVel = 12.f;
    constexpr float kSleepTime = 0.55f;
    constexpr int kSolverIterations = 5;
    constexpr float kSubstepTarget = 5.f;
    constexpr float kDragVelLerp = 0.6f;
    constexpr float kRestitutionCutoff = 45.f;
    constexpr float kSleepVel = 8.f;
    constexpr int kMaxSubsteps = 5;
    constexpr float kPushRadius = 180.f;
    constexpr float kDegToRad = 0.01745329252f;
    constexpr float kRadToDeg = 57.29577951f;
    constexpr float kMaxAngularVel = 1440.f;   // deg/s
    constexpr float kMaxLinearSpeed = 2200.f;  // px/s
    constexpr float kSquashDecay = 6.5f;
    constexpr float kMaxSquash = 0.28f;
    constexpr float kRollGrip = 2.8f;          // rolling grip
    constexpr float kSpinFromWall = 0.55f;     // wall torque scale

    float clamp01(float v) { return std::clamp(v, 0.f, 1.f); }

    float len(CCPoint p) { return std::sqrt(p.x * p.x + p.y * p.y); }

    float crossZ(CCPoint r, CCPoint v) { return r.x * v.y - r.y * v.x; }

    // 2D cross with angular velocity, in radians per second.
    CCPoint angularVelAt(CCPoint r, float angVelDeg) {
        float w = angVelDeg * kDegToRad;
        return CCPoint{-r.y * w, r.x * w};
    }

    void clampVel(Body& b) {
        float sp = len(b.vel);
        if (sp > kMaxLinearSpeed) b.vel = b.vel * (kMaxLinearSpeed / sp);
        b.angularVel = std::clamp(b.angularVel, -kMaxAngularVel, kMaxAngularVel);
    }

    void updateMassProperties(Body& b, bool massBySize) {
        float const area = (2.f * b.halfW) * (2.f * b.halfH);
        float const mass = massBySize ? std::max(0.35f, area / 3600.f) : 1.f;
        b.invMass = 1.f / mass;

        float const width = 2.f * b.halfW;
        float const height = 2.f * b.halfH;
        float const inertia = mass * (width * width + height * height) / 12.f;
        b.invInertia = 1.f / std::max(80.f, inertia);
    }
}

void PhysicsWorld::configure(PhysicsConfig const& cfg) {
    bool const gravityChanged = std::abs(m_cfg.gravity - cfg.gravity) > 1e-4f;
    bool const massModeChanged = m_cfg.massBySize != cfg.massBySize;

    m_cfg = cfg;
    m_cfg.bounciness = clamp01(cfg.bounciness);
    m_cfg.friction = clamp01(cfg.friction);
    m_cfg.airDrag = clamp01(cfg.airDrag);
    m_cfg.angularDrag = std::clamp(cfg.angularDrag, 0.f, 4.f);

    for (auto& body : m_bodies) {
        if (massModeChanged) updateMassProperties(body, m_cfg.massBySize);
        if (gravityChanged) wake(body);
    }
}

void PhysicsWorld::setBounds(CCRect bounds) {
    m_bounds = bounds;
}

void PhysicsWorld::addBody(CCNode* node, CCPoint worldPos, CCSize worldSize,
                           CCPoint initialVel, float initialAngularVel,
                           float initialAngle) {
    if (!node) return;
    Body b;
    b.node = node;
    b.pos = worldPos;
    b.vel = initialVel;
    b.angularVel = initialAngularVel;
    b.angle = initialAngle;
    b.halfW = std::max(2.f, worldSize.width * 0.5f * 0.9f);
    b.halfH = std::max(2.f, worldSize.height * 0.5f * 0.9f);
    b.baseScaleX = node->getScaleX();
    b.baseScaleY = node->getScaleY();

    updateMassProperties(b, m_cfg.massBySize);

    m_bodies.push_back(b);
}

void PhysicsWorld::clear() {
    for (auto& b : m_bodies) {
        if (auto* n = b.node.data()) {
            n->setScaleX(b.baseScaleX);
            n->setScaleY(b.baseScaleY);
        }
    }
    m_bodies.clear();
    m_dragIndex = -1;
}

void PhysicsWorld::wake(Body& b) {
    b.asleep = false;
    b.sleepTimer = 0.f;
}

void PhysicsWorld::applyImpulse(Body& b, CCPoint impulse, CCPoint r) {
    if (b.invMass <= 0.f) return;
    b.vel += impulse * b.invMass;
    // L = r x J; convert angular impulse back to degrees per second.
    float angImpulse = crossZ(r, impulse) * b.invInertia * kRadToDeg;
    b.angularVel += angImpulse;
    clampVel(b);
}

void PhysicsWorld::registerImpact(Body& b, float speed, float normalAngleDeg) {
    if (speed < 80.f) return;
    float amount = std::clamp((speed - 80.f) / 900.f, 0.f, 1.f) * kMaxSquash;
    if (amount > b.squash) {
        b.squash = amount;
        b.stretchAxis = normalAngleDeg;
    }
}

void PhysicsWorld::integrate(float dt) {
    float g = m_cfg.gravity * kGravityScale;
    // Reduce air drag to preserve spin and flight.
    float linDamp = std::exp(-m_cfg.airDrag * dt * 2.4f);
    float angDamp = std::exp(-m_cfg.angularDrag * dt * 2.2f);

    for (int i = 0; i < static_cast<int>(m_bodies.size()); ++i) {
        if (i == m_dragIndex) continue;
        auto& b = m_bodies[i];
        if (b.asleep) continue;

        b.vel.y += g * dt;
        b.vel *= linDamp;
        b.angularVel *= angDamp;
        clampVel(b);

        b.pos.x += b.vel.x * dt;
        b.pos.y += b.vel.y * dt;
        b.angle += b.angularVel * dt;
        if (b.angle > 720.f || b.angle < -720.f) {
            b.angle = std::fmod(b.angle, 360.f);
        }
        b.supportOffsetY = 0.f;
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
        float impact = std::abs(b.vel.x);
        b.pos.x = left;
        if (b.vel.x < 0.f) {
            float oldVy = b.vel.y;
            b.vel.x = bounce(b.vel.x, m_cfg.bounciness);
            // Include tangential velocity and wall-induced torque.
            CCPoint r{-b.halfW, 0.f};
            float tangential = oldVy + angularVelAt(r, b.angularVel).y;
            float frictionJ = -tangential * m_cfg.friction * 0.65f;
            b.vel.y += frictionJ * b.invMass * 0.35f;
            b.angularVel -= (frictionJ * b.halfW) * b.invInertia * kRadToDeg * kSpinFromWall;
            registerImpact(b, impact, 0.f);
            wake(b);
        }
    } else if (b.pos.x > right) {
        float impact = std::abs(b.vel.x);
        b.pos.x = right;
        if (b.vel.x > 0.f) {
            float oldVy = b.vel.y;
            b.vel.x = bounce(b.vel.x, m_cfg.bounciness);
            CCPoint r{b.halfW, 0.f};
            float tangential = oldVy + angularVelAt(r, b.angularVel).y;
            float frictionJ = -tangential * m_cfg.friction * 0.65f;
            b.vel.y += frictionJ * b.invMass * 0.35f;
            b.angularVel += (frictionJ * b.halfW) * b.invInertia * kRadToDeg * kSpinFromWall;
            registerImpact(b, impact, 180.f);
            wake(b);
        }
    }

    if (b.pos.y < bottom) {
        float impact = std::abs(b.vel.y);
        b.pos.y = bottom;
        if (b.vel.y < 0.f) {
            float oldVx = b.vel.x;
            b.vel.y = bounce(b.vel.y, m_cfg.bounciness);
            float radius = b.halfH;
            // Tangential contact speed includes rotation.
            float omegaRad = b.angularVel * kDegToRad;
            float vContact = oldVx + omegaRad * radius;
            float mu = m_cfg.friction;
            float jF = -vContact * mu / std::max(1e-4f, b.invMass + radius * radius * b.invInertia);
            // Bound friction by the normal impulse.
            float jN = std::abs(impact) * (1.f + m_cfg.bounciness) * 0.5f;
            float maxF = mu * jN * 2.5f;
            jF = std::clamp(jF, -maxF, maxF);

            b.vel.x += jF * b.invMass;
            // Apply friction torque around the contact point.
            b.angularVel += (jF * radius) * b.invInertia * kRadToDeg;

            if (std::abs(b.vel.y) < kSleepVel) b.vel.y = 0.f;
            registerImpact(b, impact, 90.f);
        }
        if (m_cfg.gravity < 0.f) b.supportOffsetY = -b.halfH;
    } else if (!m_cfg.removeCeiling && b.pos.y > top) {
        float impact = std::abs(b.vel.y);
        b.pos.y = top;
        if (m_cfg.gravity > 0.f) b.supportOffsetY = b.halfH;
        if (b.vel.y > 0.f) {
            float oldVx = b.vel.x;
            b.vel.y = bounce(b.vel.y, m_cfg.bounciness);
            float radius = b.halfH;
            float omegaRad = b.angularVel * kDegToRad;
            float vContact = oldVx - omegaRad * radius;
            float jF = -vContact * m_cfg.friction * 0.5f;
            b.vel.x += jF * b.invMass * 0.4f;
            b.angularVel -= (jF * radius) * b.invInertia * kRadToDeg * 0.6f;
            registerImpact(b, impact, -90.f);
        }
    }
}

void PhysicsWorld::applyRolling(Body& b, float dt) {
    if (b.supportOffsetY == 0.f || b.asleep) return;
    if (b.invMass <= 0.f) return;

    // Use the actual supporting side so floor and ceiling rolling share one formula.
    float const contactY = b.supportOffsetY;
    float slip = b.vel.x + angularVelAt({0.f, contactY}, b.angularVel).x;

    if (std::abs(slip) < 2.f && std::abs(b.vel.x) < kSleepVel) return;

    float corr = -slip * std::min(1.f, kRollGrip * m_cfg.friction * dt * 8.f);
    float invSum = b.invMass + contactY * contactY * b.invInertia;
    float j = corr / std::max(1e-4f, invSum);
    float maxJ = 400.f * m_cfg.friction;
    j = std::clamp(j, -maxJ, maxJ);

    b.vel.x += j * b.invMass;
    b.angularVel -= (j * contactY) * b.invInertia * kRadToDeg;

    float rollDamp = std::exp(-m_cfg.friction * dt * 1.2f);
    b.vel.x *= rollDamp;
    b.angularVel *= rollDamp;
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
    float iia = aFixed ? 0.f : a.invInertia;
    float iib = bFixed ? 0.f : b.invInertia;
    float invSum = ima + imb;
    if (invSum <= 0.f) return;

    CCPoint n;
    float pen;
    if (overlapX < overlapY) {
        n = CCPoint{(dx < 0.f) ? -1.f : 1.f, 0.f};
        pen = overlapX;
    } else {
        n = CCPoint{0.f, (dy < 0.f) ? -1.f : 1.f};
        pen = overlapY;
    }

    if (m_cfg.gravity < 0.f) {
        if (n.y > 0.f) b.supportOffsetY = -b.halfH;
        else if (n.y < 0.f) a.supportOffsetY = -a.halfH;
    } else if (m_cfg.gravity > 0.f) {
        if (n.y > 0.f) a.supportOffsetY = a.halfH;
        else if (n.y < 0.f) b.supportOffsetY = b.halfH;
    }

    // Baumgarte positional correction.
    float corr = pen / invSum * 0.85f;
    if (!aFixed) a.pos -= n * (corr * ima);
    if (!bFixed) b.pos += n * (corr * imb);

    CCPoint contact = (a.pos + b.pos) * 0.5f;
    CCPoint ra = contact - a.pos;
    CCPoint rb = contact - b.pos;

    CCPoint va = a.vel + angularVelAt(ra, a.angularVel);
    CCPoint vb = b.vel + angularVelAt(rb, b.angularVel);
    CCPoint rv = vb - va;
    float vn = rv.x * n.x + rv.y * n.y;
    if (vn > 0.f) return;

    float ran = crossZ(ra, n);
    float rbn = crossZ(rb, n);
    float effMass = invSum + ran * ran * iia + rbn * rbn * iib;
    if (effMass < 1e-6f) return;

    float e = (std::abs(vn) < kRestitutionCutoff) ? 0.f : m_cfg.bounciness;
    float j = -(1.f + e) * vn / effMass;
    CCPoint impulse = n * j;

    if (!aFixed) applyImpulse(a, impulse * -1.f, ra);
    if (!bFixed) applyImpulse(b, impulse, rb);

    // Tangential impulse with a Coulomb friction limit.
    CCPoint t = CCPoint{n.y, -n.x};
    CCPoint va2 = a.vel + angularVelAt(ra, a.angularVel);
    CCPoint vb2 = b.vel + angularVelAt(rb, b.angularVel);
    CCPoint rv2 = vb2 - va2;
    float vt = rv2.x * t.x + rv2.y * t.y;

    float rat = crossZ(ra, t);
    float rbt = crossZ(rb, t);
    float effT = invSum + rat * rat * iia + rbt * rbt * iib;
    if (effT > 1e-6f) {
        float jt = -vt / effT;
        float maxFriction = m_cfg.friction * std::abs(j);
        jt = std::clamp(jt, -maxFriction, maxFriction);
        CCPoint tImpulse = t * jt;
        if (!aFixed) applyImpulse(a, tImpulse * -1.f, ra);
        if (!bFixed) applyImpulse(b, tImpulse, rb);
    }

    float impactSpeed = std::abs(vn);
    registerImpact(a, impactSpeed, std::atan2(n.y, n.x) * kRadToDeg);
    registerImpact(b, impactSpeed, std::atan2(-n.y, -n.x) * kRadToDeg);

    auto wakeFromImpact = [this](Body& body) {
        if (!body.asleep) return;
        if (len(body.vel) >= kSleepVel || std::abs(body.angularVel) >= kSleepAngVel) {
            wake(body);
        } else {
            body.vel = CCPoint{0.f, 0.f};
            body.angularVel = 0.f;
        }
    };
    wakeFromImpact(a);
    wakeFromImpact(b);
}

void PhysicsWorld::separateAndResolve() {
    int n = static_cast<int>(m_bodies.size());
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            resolveBodyPair(m_bodies[i], m_bodies[j], i, j);
        }
    }
}

void PhysicsWorld::decayVisuals(Body& b, float dt) {
    if (b.squash > 0.f) {
        b.squash = std::max(0.f, b.squash - kSquashDecay * dt);
    }
}

void PhysicsWorld::updateSleep(Body& b, float dt) {
    if (b.asleep) return;
    float speed = len(b.vel);
    bool slow = (speed < kSleepVel) && (std::abs(b.angularVel) < kSleepAngVel);
    // With zero gravity, a stationary body may sleep anywhere.
    bool const supported = b.supportOffsetY != 0.f || std::abs(m_cfg.gravity) < 1e-4f;
    if (slow && supported) {
        b.sleepTimer += dt;
        if (b.sleepTimer >= kSleepTime) {
            b.asleep = true;
            b.vel = CCPoint{0.f, 0.f};
            b.angularVel = 0.f;
            b.squash = 0.f;
        }
    } else {
        b.sleepTimer = 0.f;
    }
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
        for (int iter = 0; iter < kSolverIterations; ++iter) {
            for (int i = 0; i < static_cast<int>(m_bodies.size()); ++i) {
                if (i == m_dragIndex) continue;
                auto& b = m_bodies[i];
                if (!b.asleep) collideWalls(b);
            }
            separateAndResolve();
        }
        for (int i = 0; i < static_cast<int>(m_bodies.size()); ++i) {
            if (i == m_dragIndex) continue;
            auto& b = m_bodies[i];
            if (b.asleep) continue;
            collideWalls(b);
            applyRolling(b, h);
        }
    }

    for (int i = 0; i < static_cast<int>(m_bodies.size()); ++i) {
        if (i == m_dragIndex) continue;
        auto& b = m_bodies[i];
        decayVisuals(b, dt);
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

        // Apply impact squash along the collision axis.
        if (b.squash > 0.01f) {
            float s = b.squash;
            float axis = b.stretchAxis * kDegToRad;
            float sx = 1.f - s * std::abs(std::cos(axis)) + s * 0.5f * std::abs(std::sin(axis));
            float sy = 1.f - s * std::abs(std::sin(axis)) + s * 0.5f * std::abs(std::cos(axis));
            sx = std::clamp(sx, 0.7f, 1.35f);
            sy = std::clamp(sy, 0.7f, 1.35f);
            node->setScaleX(b.baseScaleX * sx);
            node->setScaleY(b.baseScaleY * sy);
        } else {
            node->setScaleX(b.baseScaleX);
            node->setScaleY(b.baseScaleY);
        }
    }
}

bool PhysicsWorld::beginDrag(CCPoint p) {
    for (int i = static_cast<int>(m_bodies.size()) - 1; i >= 0; --i) {
        auto& b = m_bodies[i];
        float pad = std::max(b.halfW, b.halfH) * 0.15f;
        if (std::abs(p.x - b.pos.x) <= b.halfW + pad &&
            std::abs(p.y - b.pos.y) <= b.halfH + pad) {
            m_dragIndex = i;
            m_dragOffset = b.pos - p;
            m_prevDragPos = p;
            m_dragVel = CCPoint{0.f, 0.f};
            b.vel = CCPoint{0.f, 0.f};
            b.angularVel *= 0.3f; // Preserve some spin while grabbing.
            b.squash = 0.f;
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
        CCPoint frameVel = (p - m_prevDragPos) / dt;
        m_dragVel.x = std::lerp(m_dragVel.x, frameVel.x, kDragVelLerp);
        m_dragVel.y = std::lerp(m_dragVel.y, frameVel.y, kDragVelLerp);

        CCPoint targetVel = delta / dt;
        b.vel.x = std::lerp(b.vel.x, targetVel.x, kDragVelLerp);
        b.vel.y = std::lerp(b.vel.y, targetVel.y, kDragVelLerp);

        // Offset dragging produces torque and swing spin.
        float torque = crossZ(m_dragOffset, delta) * 0.0022f;
        float spinFromSwing = crossZ(m_dragOffset, m_dragVel) * 0.0015f;
        float targetSpin = torque + spinFromSwing;
        b.angularVel = std::lerp(b.angularVel, targetSpin, kDragVelLerp);
        clampVel(b);
    }
    b.pos = target;
    m_prevDragPos = p;
}

void PhysicsWorld::endDrag() {
    if (m_dragIndex >= 0 && m_dragIndex < static_cast<int>(m_bodies.size())) {
        auto& b = m_bodies[m_dragIndex];
        // Release inherits drag velocity and adds offset-based spin.
        b.vel = m_dragVel;
        float releaseSpin = crossZ(m_dragOffset, m_dragVel) * 0.004f;
        b.angularVel += releaseSpin;
        clampVel(b);
        wake(b);
    }
    m_dragIndex = -1;
    m_dragVel = CCPoint{0.f, 0.f};
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
        if (dist >= kPushRadius) continue;

        if (dist < 1e-3f) {
            // Center hits get a vertical kick and random spin.
            b.vel.y += 380.f * strength * b.invMass;
            b.vel.x += (static_cast<float>(std::rand() % 200) - 100.f) * strength * 0.8f;
            b.angularVel += (static_cast<float>(std::rand() % 700) - 350.f) * strength;
            registerImpact(b, 400.f * strength, 90.f);
            wake(b);
            continue;
        }

        float falloff = 1.f - dist / kPushRadius;
        falloff *= falloff; // Bias force toward the click.
        CCPoint dir = delta / dist;
        float impulseMag = strength * falloff * 720.f;
        CCPoint impulse = dir * impulseMag;
        // Offset impulse plus small spin variation.
        CCPoint r = dir * (-b.halfW * 0.35f);
        applyImpulse(b, impulse, r);
        b.angularVel += crossZ(delta, dir) * strength * falloff * 0.8f;
        b.angularVel += (static_cast<float>(std::rand() % 120) - 60.f) * strength * falloff;
        registerImpact(b, impulseMag * 0.5f, std::atan2(dir.y, dir.x) * kRadToDeg);
        wake(b);
    }
}

}
