#include "MenuPhysicsNode.hpp"

#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <algorithm>
#include <cmath>
#include <random>

using namespace geode::prelude;

namespace paimon::menuphysics {

namespace {
    constexpr int kMaxBodies = 80;
    constexpr float kReturnDuration = 0.5f;
    constexpr float kPi = 3.14159265f;
    constexpr float kTapMoveThreshold = 10.f;
    constexpr float kConfigRefresh = 0.25f;

    void collectButtons(CCNode* n, CCNode* exclude, std::vector<CCMenuItem*>& out) {
        if (!n || n == exclude) return;
        if (auto* item = typeinfo_cast<CCMenuItem*>(n)) {
            if (typeinfo_cast<CCMenu*>(n->getParent()) && n->isVisible()) {
                out.push_back(item);
            }
            return;
        }
        if (auto* children = n->getChildren()) {
            for (int i = 0; i < static_cast<int>(children->count()); ++i) {
                collectButtons(typeinfo_cast<CCNode*>(children->objectAtIndex(i)), exclude, out);
            }
        }
    }

    PhysicsConfig readConfig() {
        PhysicsConfig cfg;
        auto* mod = Mod::get();
        cfg.gravity = static_cast<float>(mod->getSettingValue<double>("menu-physics-gravity"));
        cfg.bounciness = static_cast<float>(mod->getSettingValue<double>("menu-physics-bounciness"));
        cfg.friction = static_cast<float>(mod->getSettingValue<double>("menu-physics-friction"));
        cfg.airDrag = static_cast<float>(mod->getSettingValue<double>("menu-physics-air-drag"));
        cfg.angularDrag = static_cast<float>(mod->getSettingValue<double>("menu-physics-angular-drag"));
        cfg.removeCeiling = mod->getSettingValue<bool>("menu-physics-remove-ceiling");
        cfg.massBySize = mod->getSettingValue<bool>("menu-physics-mass-by-size");
        return cfg;
    }
}

MenuPhysicsNode* MenuPhysicsNode::create() {
    auto ret = new MenuPhysicsNode();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool MenuPhysicsNode::init() {
    if (!CCLayer::init()) return false;

    this->setID("paim-menu-physics-node"_spr);

    m_world.configure(readConfig());

    this->setTouchEnabled(true);
    this->scheduleUpdate();
    return true;
}

void MenuPhysicsNode::captureHost() {
    auto* host = this->getParent();
    if (!host) return;

    auto win = CCDirector::get()->getWinSize();
    bool removeCeiling = Mod::get()->getSettingValue<bool>("menu-physics-remove-ceiling");
    float height = win.height;
    if (removeCeiling) height *= 50.f;
    m_world.setBounds(CCRect{0.f, 0.f, win.width, height});
    m_world.configure(readConfig());

    std::vector<CCMenuItem*> buttons;
    collectButtons(host, this, buttons);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> speed(220.f, 560.f);
    std::uniform_real_distribution<float> spin(-720.f, 720.f);
    std::uniform_real_distribution<float> perp(-110.f, 110.f);
    std::uniform_real_distribution<float> popUp(40.f, 180.f);
    std::uniform_real_distribution<float> tilt(-25.f, 25.f);

    CCPoint center{win.width * 0.5f, win.height * 0.5f};
    int count = 0;
    for (auto* btn : buttons) {
        if (count >= kMaxBodies) break;
        auto* parent = btn->getParent();
        if (!parent) continue;

        CCPoint worldPos = parent->convertToWorldSpace(btn->getPosition());
        CCSize size = btn->getScaledContentSize();
        if (size.width < 4.f || size.height < 4.f) size = CCSize{30.f, 30.f};

        float baseRot = btn->getRotation();
        m_originals.push_back({
            btn,
            btn->getPosition(),
            baseRot,
            btn->getScaleX(),
            btn->getScaleY()
        });

        // Explosion radial desde el centro + empujon hacia arriba + spin fuerte
        CCPoint dir = worldPos - center;
        float dl = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (dl < 1e-3f) {
            std::uniform_real_distribution<float> angleDist(0.f, 6.2831853f);
            float a = angleDist(gen);
            dir = CCPoint{std::cos(a), std::sin(a)};
        } else {
            dir = dir / dl;
        }
        CCPoint perpDir{-dir.y, dir.x};
        float s = speed(gen);
        CCPoint vel = dir * s + perpDir * perp(gen);
        vel.y += popUp(gen);

        // Angulo inicial: rotacion del boton + leve inclinacion aleatoria
        float startAngle = -baseRot + tilt(gen);
        float angVel = spin(gen);
        // Botones mas lejanos del centro giran un poco mas
        angVel += (dl * 0.15f) * ((angVel >= 0.f) ? 1.f : -1.f);

        m_world.addBody(btn, worldPos, size, vel, angVel, startAngle);
        ++count;
    }
}

void MenuPhysicsNode::update(float dt) {
    if (m_detaching) {
        updateReturnAnimation(dt);
        return;
    }

    if (!m_captured) {
        captureHost();
        m_captured = true;
        return;
    }

    // Releer ajustes periodicamente para feedback al vivo desde el panel
    m_configTimer += dt;
    if (m_configTimer >= kConfigRefresh) {
        m_configTimer = 0.f;
        m_world.configure(readConfig());
        auto win = CCDirector::get()->getWinSize();
        bool removeCeiling = Mod::get()->getSettingValue<bool>("menu-physics-remove-ceiling");
        float height = win.height;
        if (removeCeiling) height *= 50.f;
        m_world.setBounds(CCRect{0.f, 0.f, win.width, height});
    }

    m_world.step(dt);
    m_world.syncNodes();
}

void MenuPhysicsNode::detach() {
    if (m_detaching) return;
    m_detaching = true;
    this->setTouchEnabled(false);
    m_world.endDrag();
    m_world.clear();
    m_returnElapsed = 0.f;

    bool hasAnimatedNodes = false;
    for (auto& o : m_originals) {
        if (!o.node || !o.node->getParent()) continue;
        o.returnStartPos = o.node->getPosition();
        o.returnStartRotation = o.node->getRotation();
        o.returnStartScaleX = o.node->getScaleX();
        o.returnStartScaleY = o.node->getScaleY();
        hasAnimatedNodes = true;
    }

    if (!hasAnimatedNodes) {
        finishDetach();
        return;
    }
}

void MenuPhysicsNode::updateReturnAnimation(float dt) {
    m_returnElapsed += std::clamp(dt, 0.f, 1.f / 30.f);
    float const t = std::clamp(m_returnElapsed / kReturnDuration, 0.f, 1.f);
    float const easePos = 1.f - std::pow(1.f - t, 3.f);
    float const easeRot = 1.f - std::pow(1.f - t, 2.5f);

    for (auto& o : m_originals) {
        if (!o.node || !o.node->getParent()) continue;

        o.node->setPosition(o.returnStartPos + (o.pos - o.returnStartPos) * easePos);

        float const rotationDelta = std::remainder(
            o.rotation - o.returnStartRotation, 360.f
        );
        // Overshoot de giro al reacomodarse
        float overshoot = (t < 0.85f) ? std::sin(t * kPi) * 8.f * (1.f - t) : 0.f;
        o.node->setRotation(o.returnStartRotation + rotationDelta * easeRot + overshoot);

        float sx = o.returnStartScaleX + (o.scaleX - o.returnStartScaleX) * easePos;
        float sy = o.returnStartScaleY + (o.scaleY - o.returnStartScaleY) * easePos;
        o.node->setScaleX(sx);
        o.node->setScaleY(sy);
    }

    if (t >= 1.f) finishDetach();
}

void MenuPhysicsNode::finishDetach() {
    for (auto& o : m_originals) {
        if (!o.node || !o.node->getParent()) continue;
        o.node->setPosition(o.pos);
        o.node->setRotation(o.rotation);
        o.node->setScaleX(o.scaleX);
        o.node->setScaleY(o.scaleY);
    }
    m_originals.clear();
    this->removeFromParent();
}

bool MenuPhysicsNode::ccTouchBegan(CCTouch* touch, CCEvent*) {
    CCPoint p = touch->getLocation();
    m_touchStart = p;
    m_dragMoved = false;
    if (m_world.beginDrag(p)) {
        return true;
    }
    float push = static_cast<float>(Mod::get()->getSettingValue<double>("menu-physics-push-power"));
    if (push > 0.f) {
        m_world.pushExplosion(p, push);
    }
    return false;  // touch must pass through so GD buttons stay clickable
}

void MenuPhysicsNode::ccTouchMoved(CCTouch* touch, CCEvent*) {
    if (m_world.isDragging()) {
        CCPoint p = touch->getLocation();
        if (!m_dragMoved && ccpDistance(p, m_touchStart) > kTapMoveThreshold) {
            m_dragMoved = true;
        }
        float dt = CCDirector::get()->getDeltaTime();
        if (dt <= 0.f) dt = 1.f / 60.f;
        m_world.moveDrag(p, dt);
    }
}

void MenuPhysicsNode::ccTouchEnded(CCTouch*, CCEvent*) {
    // A quick tap activates the button; a hold/drag just releases it.
    if (m_world.isDragging() && !m_dragMoved) {
        if (auto* item = typeinfo_cast<CCMenuItem*>(m_world.draggedNode())) {
            if (item->isEnabled()) item->activate();
        }
    }
    m_world.endDrag();
}

void MenuPhysicsNode::ccTouchCancelled(CCTouch* touch, CCEvent* e) {
    ccTouchEnded(touch, e);
}

void MenuPhysicsNode::registerWithTouchDispatcher() {
    CCDirector::sharedDirector()->getTouchDispatcher()
        ->addTargetedDelegate(this, -129, true);
}

void MenuPhysicsNode::onExit() {
    CCDirector::sharedDirector()->getTouchDispatcher()->removeDelegate(this);
    CCLayer::onExit();
}

} // namespace paimon::menuphysics
