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
    constexpr float kRollGrip = 2.8f;          // fuerza de rodadura en suelo
    constexpr float kSpinFromWall = 0.55f;     // torque en paredes (escala)

    float clamp01(float v) { return std::clamp(v, 0.f, 1.f); }

    float len(CCPoint p) { return std::sqrt(p.x * p.x + p.y * p.y); }

    CCPoint norm(CCPoint p) {
        float l = len(p);
        if (l < 1e-5f) return {0.f, 0.f};
        return p / l;
    }

    float cross(CCPoint a, CCPoint b) { return a.x * b.y - a.y * b.x; }

    float crossZ(CCPoint r, CCPoint v) { return r.x * v.y - r.y * v.x; }

    // r x (0,0,w) en 2D -> (-r.y * w, r.x * w) con w en rad/s
    CCPoint angularVelAt(CCPoint r, float angVelDeg) {
        float w = angVelDeg * kDegToRad;
        return CCPoint{-r.y * w, r.x * w};
    }

    void clampVel(Body& b) {
        float sp = len(b.vel);
        if (sp > kMaxLinearSpeed) b.vel = b.vel * (kMaxLinearSpeed / sp);
        b.angularVel = std::clamp(b.angularVel, -kMaxAngularVel, kMaxAngularVel);
    }
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
                           CCPoint initialVel, float initialAngularVel,
                           float initialAngle) {
    if (!node) return;
    Body b;
    b.node = node;
    b.pos = worldPos;
    b.lastPos = worldPos;
    b.vel = initialVel;
    b.angularVel = initialAngularVel;
    b.angle = initialAngle;
    b.halfW = std::max(2.f, worldSize.width * 0.5f * 0.9f);
    b.halfH = std::max(2.f, worldSize.height * 0.5f * 0.9f);
    b.baseScaleX = node->getScaleX();
    b.baseScaleY = node->getScaleY();

    float area = (2.f * b.halfW) * (2.f * b.halfH);
    float mass = m_cfg.massBySize ? std::max(0.35f, area / 3600.f) : 1.f;
    b.invMass = 1.f / mass;
    // Caja 2D: I = m * (w^2 + h^2) / 12
    float w = 2.f * b.halfW;
    float h = 2.f * b.halfH;
    float inertia = mass * (w * w + h * h) / 12.f;
    b.invInertia = 1.f / std::max(80.f, inertia);

    m_bodies.push_back(b);
}

void PhysicsWorld::clear() {
    // Restaurar escala visual antes de soltar nodos.
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
    // L = r x J; omega += L / I  (J en px/s*mass, omega en deg/s)
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
    // Drag mas suave en aire para conservar spin y vuelo interesante
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
        // Normalizar angulo para evitar overflow
        if (b.angle > 720.f || b.angle < -720.f) {
            b.angle = std::fmod(b.angle, 360.f);
        }
        b.onGround = false;
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

    // --- Paredes laterales ---
    if (b.pos.x < left) {
        float impact = std::abs(b.vel.x);
        b.pos.x = left;
        if (b.vel.x < 0.f) {
            float oldVy = b.vel.y;
            b.vel.x = bounce(b.vel.x, m_cfg.bounciness);
            // Friccion + torque realista: el contacto esta en el borde izquierdo
            CCPoint r{-b.halfW, 0.f};
            float tangential = oldVy - angularVelAt(r, b.angularVel).y;
            float frictionJ = -tangential * m_cfg.friction * 0.65f;
            b.vel.y += frictionJ * b.invMass * 0.35f;
            b.angularVel += (frictionJ * b.halfW) * b.invInertia * kRadToDeg * kSpinFromWall;
            // Empuje de giro por impacto oblicuo
            b.angularVel += oldVy * 0.12f * (1.f + m_cfg.friction);
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
            float tangential = oldVy - angularVelAt(r, b.angularVel).y;
            float frictionJ = -tangential * m_cfg.friction * 0.65f;
            b.vel.y += frictionJ * b.invMass * 0.35f;
            b.angularVel -= (frictionJ * b.halfW) * b.invInertia * kRadToDeg * kSpinFromWall;
            b.angularVel -= oldVy * 0.12f * (1.f + m_cfg.friction);
            registerImpact(b, impact, 180.f);
            wake(b);
        }
    }

    // --- Suelo ---
    if (b.pos.y < bottom) {
        float impact = std::abs(b.vel.y);
        b.pos.y = bottom;
        if (b.vel.y < 0.f) {
            float oldVx = b.vel.x;
            b.vel.y = bounce(b.vel.y, m_cfg.bounciness);
            // Contacto en la base: r = (0, -halfH)
            float radius = b.halfH;
            // Velocidad tangencial del punto de contacto (incl. rotacion)
            // v_point = v + omega x r  =>  en x: vx - omega_rad * halfH
            float omegaRad = b.angularVel * kDegToRad;
            float vContact = oldVx - omegaRad * radius;
            float mu = m_cfg.friction;
            // Impulso de friccion que intenta anular deslizamiento
            float jF = -vContact * mu / std::max(1e-4f, b.invMass + radius * radius * b.invInertia);
            // Limitar por friccion de Coulomb respecto al normal
            float jN = std::abs(impact) * (1.f + m_cfg.bounciness) * 0.5f;
            float maxF = mu * jN * 2.5f;
            jF = std::clamp(jF, -maxF, maxF);

            b.vel.x += jF * b.invMass;
            // Torque: r x F = halfH * jF  (sentido: deslizamiento a la derecha => gira antihorario)
            b.angularVel += (jF * radius) * b.invInertia * kRadToDeg;

            if (std::abs(b.vel.y) < kSleepVel) b.vel.y = 0.f;
            b.onGround = true;
            registerImpact(b, impact, 90.f);
            wake(b);
        } else {
            b.onGround = true;
        }
    } else if (!m_cfg.removeCeiling && b.pos.y > top) {
        float impact = std::abs(b.vel.y);
        b.pos.y = top;
        if (b.vel.y > 0.f) {
            float oldVx = b.vel.x;
            b.vel.y = bounce(b.vel.y, m_cfg.bounciness);
            float radius = b.halfH;
            float omegaRad = b.angularVel * kDegToRad;
            float vContact = oldVx + omegaRad * radius;
            float jF = -vContact * m_cfg.friction * 0.5f;
            b.vel.x += jF * b.invMass * 0.4f;
            b.angularVel -= (jF * radius) * b.invInertia * kRadToDeg * 0.6f;
            registerImpact(b, impact, -90.f);
            wake(b);
        }
    }
}

void PhysicsWorld::applyRolling(Body& b, float dt) {
    if (!b.onGround || b.asleep) return;
    if (b.invMass <= 0.f) return;

    // Objetivo de rodadura pura: v = -omega * r  (omega en rad/s, r = halfH)
    // En GD y crece hacia arriba; gira positivo (CCW) => en el suelo el punto se mueve a -x
    float r = std::max(b.halfH, 4.f);
    float omegaRad = b.angularVel * kDegToRad;
    float vTarget = omegaRad * r; // v_x deseada si rueda sin deslizar (signo cocos)
    // En realidad para rueda: vx = -omega * r con convención usual;
    // setRotation(-angle) y angle+ => usamos: slip = vx + omega*r
    float slip = b.vel.x + omegaRad * r;

    if (std::abs(slip) < 2.f && std::abs(b.vel.x) < kSleepVel) return;

    // Corregir slip con friccion de rodadura (suave)
    float corr = -slip * std::min(1.f, kRollGrip * m_cfg.friction * dt * 8.f);
    float invSum = b.invMass + r * r * b.invInertia;
    float j = corr / std::max(1e-4f, invSum);
    // Limitar impulso de rodadura
    float maxJ = 400.f * m_cfg.friction;
    j = std::clamp(j, -maxJ, maxJ);

    b.vel.x += j * b.invMass;
    b.angularVel += (j * r) * b.invInertia * kRadToDeg;

    // Friccion de rodadura (pierde energia lentamente en el suelo)
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

    // Correccion posicional (Baumgarte)
    float corr = pen / invSum * 0.85f;
    if (!aFixed) a.pos -= n * (corr * ima);
    if (!bFixed) b.pos += n * (corr * imb);

    // Punto de contacto aproximado entre centros
    CCPoint contact = (a.pos + b.pos) * 0.5f;
    CCPoint ra = contact - a.pos;
    CCPoint rb = contact - b.pos;

    // Velocidad relativa en el punto de contacto (con rotacion)
    CCPoint va = a.vel + angularVelAt(ra, a.angularVel);
    CCPoint vb = b.vel + angularVelAt(rb, b.angularVel);
    CCPoint rv = vb - va;
    float vn = rv.x * n.x + rv.y * n.y;
    if (vn > 0.f) return;

    // Denominador con terminos angulares: 1/m + (r x n)^2 / I
    float ran = crossZ(ra, n);
    float rbn = crossZ(rb, n);
    float effMass = invSum + ran * ran * iia + rbn * rbn * iib;
    if (effMass < 1e-6f) return;

    float e = (std::abs(vn) < kRestitutionCutoff) ? 0.f : m_cfg.bounciness;
    float j = -(1.f + e) * vn / effMass;
    CCPoint impulse = n * j;

    if (!aFixed) applyImpulse(a, impulse * -1.f, ra);
    if (!bFixed) applyImpulse(b, impulse, rb);

    // Friccion tangencial con torque
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

void PhysicsWorld::decayVisuals(Body& b, float dt) {
    if (b.squash > 0.f) {
        b.squash = std::max(0.f, b.squash - kSquashDecay * dt);
    }
}

void PhysicsWorld::updateSleep(Body& b, float dt) {
    if (b.asleep) return;
    float speed = len(b.vel);
    bool slow = (speed < kSleepVel) && (std::abs(b.angularVel) < kSleepAngVel);
    // Solo dormirse en el suelo o muy quieto (evita congelar en el aire)
    bool grounded = b.onGround || b.pos.y <= m_bounds.origin.y + b.halfH + 2.f;
    if (slow && grounded) {
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

        // Squash/stretch visual en el eje del impacto
        if (b.squash > 0.01f) {
            float s = b.squash;
            float axis = b.stretchAxis * kDegToRad;
            // Comprimir en normal, estirar en tangente
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
        // Hitbox un poco mas generosa al girar
        float pad = std::max(b.halfW, b.halfH) * 0.15f;
        if (std::abs(p.x - b.pos.x) <= b.halfW + pad &&
            std::abs(p.y - b.pos.y) <= b.halfH + pad) {
            m_dragIndex = i;
            m_dragOffset = b.pos - p;
            m_prevDragPos = p;
            m_dragVel = CCPoint{0.f, 0.f};
            b.vel = CCPoint{0.f, 0.f};
            b.angularVel *= 0.3f; // conserva un poco de spin al agarrar
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

        // Torque al arrastrar fuera del centro: el boton "gira" en la mano
        float torque = crossZ(m_dragOffset, delta) * 0.0022f;
        // Tambien gira segun la velocidad perpendicular al offset de agarre
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
        // Lanzamiento: hereda velocidad del drag + spin residual del agarre
        b.vel = m_dragVel;
        // Empuje angular extra al soltar segun offset de agarre
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
            // Click casi en el centro: impulso vertical + spin aleatorio fuerte
            b.vel.y += 380.f * strength * b.invMass;
            b.vel.x += (static_cast<float>(std::rand() % 200) - 100.f) * strength * 0.8f;
            b.angularVel += (static_cast<float>(std::rand() % 700) - 350.f) * strength;
            registerImpact(b, 400.f * strength, 90.f);
            wake(b);
            continue;
        }

        float falloff = 1.f - dist / kPushRadius;
        falloff *= falloff; // mas fuerza cerca del click
        CCPoint dir = delta / dist;
        float impulseMag = strength * falloff * 720.f;
        CCPoint impulse = dir * impulseMag;
        // Aplicar en un punto descentrado para generar spin natural
        CCPoint r = dir * (-b.halfW * 0.35f);
        applyImpulse(b, impulse, r);
        // Spin extra proporcional a la distancia al centro del body
        b.angularVel += crossZ(delta, dir) * strength * falloff * 0.8f;
        // Torsión aleatoria pequeña para variedad
        b.angularVel += (static_cast<float>(std::rand() % 120) - 60.f) * strength * falloff;
        registerImpact(b, impulseMag * 0.5f, std::atan2(dir.y, dir.x) * kRadToDeg);
        wake(b);
    }
}

} // namespace paimon::menuphysics
