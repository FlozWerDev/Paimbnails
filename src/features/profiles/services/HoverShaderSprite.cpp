#include "HoverShaderSprite.hpp"
#include "../../../utils/GLSLLoader.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include <Geode/utils/cocos.hpp>
#include <cmath>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::profile_pic {

HoverShaderSprite* HoverShaderSprite::createWithTexture(CCTexture2D* tex, std::string const& shaderId, float intensity) {
    auto* ret = new HoverShaderSprite();
    if (ret && ret->initEffect(tex, shaderId, intensity)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool HoverShaderSprite::initEffect(CCTexture2D* tex, std::string const& shaderId, float intensity) {
    if (!tex || !this->initWithTexture(tex)) return false;
    m_shaderId = shaderId;
    m_intensity = intensity;

    std::string key = "paimon-" + shaderId;
    std::string file = shaderId + ".glsl";
    m_program = paimon::shaders::loadShader(key, "cell_vertex.glsl", file, nullptr, nullptr);
    if (!m_program) return false;
    this->setShaderProgram(m_program);
    this->scheduleUpdate();
    return true;
}

bool HoverShaderSprite::pointerInside() const {
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_MACOS)
    auto* node = m_hitNode ? m_hitNode : static_cast<CCNode const*>(this);
    auto mouse = geode::cocos::getMousePos();
    auto size = node->getContentSize();
    auto local = const_cast<CCNode*>(node)->convertToNodeSpace(mouse);
    return CCRect(0.f, 0.f, size.width, size.height).containsPoint(local);
#else
    return false;
#endif
}

void HoverShaderSprite::triggerBurst() {
    m_burst = 1.f;
}

void HoverShaderSprite::update(float dt) {
    if (paimon::isRuntimeShuttingDown()) return;
    if (dt <= 0.f) return;

    bool hovered = pointerInside();
    float target = hovered ? 1.f : 0.f;
    m_hover += (target - m_hover) * (1.f - std::exp(-9.f * dt));
    if (std::fabs(m_hover - target) < 0.002f) m_hover = target;

    m_burst *= std::exp(-2.0f * dt);
    if (m_burst < 0.01f) m_burst = 0.f;

    float active = std::max(m_hover, m_burst);
    // advance time only while the effect is visible; idle costs nothing.
    if (active > 0.002f) m_time += dt;

#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_MACOS)
    if (hovered) {
        auto mouse = geode::cocos::getMousePos();
        auto size = this->getContentSize();
        auto local = this->convertToNodeSpace(mouse);
        if (size.width > 0.f && size.height > 0.f) {
            m_cursorX = std::clamp(local.x / size.width, 0.f, 1.f);
            m_cursorY = std::clamp(local.y / size.height, 0.f, 1.f);
        }
    } else {
        m_cursorX = -1.f;
        m_cursorY = -1.f;
    }
#endif
}

void HoverShaderSprite::draw() {
    if (m_program) {
        float active = std::max(m_hover, m_burst);
        m_program->use();
        m_program->setUniformsForBuiltins();

        GLint loc = m_program->getUniformLocationForName("u_intensity");
        if (loc != -1) m_program->setUniformLocationWith1f(loc, m_intensity * active);

        loc = m_program->getUniformLocationForName("u_time");
        if (loc != -1) m_program->setUniformLocationWith1f(loc, m_time);

        loc = m_program->getUniformLocationForName("u_cursor");
        if (loc != -1) m_program->setUniformLocationWith2f(loc, m_cursorX, m_cursorY);
    }
    CCSprite::draw();
}

}
