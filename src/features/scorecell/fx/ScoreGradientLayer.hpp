#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/cocos.hpp>
#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "ScoreGradientDesign.hpp"
#include <algorithm>
#include <cmath>

namespace paimon::scorecell {

// Gradient background built from the player's icon colors.
//
// - Colors go through designScoreGradient(): hue is preserved, saturation /
//   lightness are clamped into a background-safe band, and identical stops
//   become an analogous duo instead of a flat rectangle.
// - Diagonal vector with real vertical travel, so wide/short cells show a
//   visible blend instead of two solid halves.
// - Continuous idle motion (slow sheen sway) on every platform, plus a
//   smooth hover lift (opacity + steeper vector) on desktop. The quad
//   itself never moves: no uncovered corners, no size breathing.
class ScoreGradientLayer : public cocos2d::CCLayerGradient {
    float m_hover = 0.f;
    double m_time = 0.0;
    GLubyte m_baseOpacity = 125;
    float m_idleSpeed = 1.f;
    // Retriggered pulse: set to 1 on every mouse-enter rising edge, then
    // decays exponentially. Vector/opacity-only, so the quad never moves.
    float m_burst = 0.f;
    bool m_wasHovered = false;

    static constexpr float kDiagY = -0.35f;

    void update(float dt) override {
        if (paimon::isRuntimeShuttingDown()) return;
        auto* cell = getParent();
        if (!cell) return;
        if (dt < 0.f) dt = 0.f;
        m_time += static_cast<double>(dt);

        bool hovered = false;
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_MACOS)
        auto mouse = geode::cocos::getMousePos();
        auto local = cell->convertToNodeSpace(mouse);
        auto size = getContentSize();
        hovered = cocos2d::CCRect(0.f, 0.f, size.width, size.height).containsPoint(local);
        for (auto* node = cell; node && hovered; node = node->getParent()) {
            if (!node->isVisible()) {
                hovered = false;
                break;
            }
            // Respect the viewport of scrollable leaderboard lists.
            if (geode::cast::typeinfo_cast<cocos2d::CCLayer*>(node) && node != cell) {
                auto bounds = node->getContentSize();
                if (bounds.width > 0.f && bounds.height > 0.f) {
                    auto point = node->convertToNodeSpace(mouse);
                    hovered = cocos2d::CCRect(0.f, 0.f, bounds.width, bounds.height).containsPoint(point);
                }
            }
        }
#endif
        float target = hovered ? 1.f : 0.f;
        m_hover += (target - m_hover) * (1.f - std::exp(-10.f * dt));
        if (std::abs(m_hover - target) < 0.001f) m_hover = target;

        // Rising edge: the cursor just entered the cell. Restart the pulse
        // every time, so each pass gets its own animation.
        if (hovered && !m_wasHovered) m_burst = 1.f;
        m_wasHovered = hovered;
        m_burst *= std::exp(-3.2f * dt);
        if (m_burst < 0.01f) m_burst = 0.f;

        // Idle sheen: gentle sway of the gradient direction + a faint
        // brightness breath. Fades out as hover takes over.
        constexpr double kTwoPi = 6.283185307179586;
        double ph = std::fmod(m_time * m_idleSpeed * kTwoPi / 5.0, kTwoPi);
        float sway = static_cast<float>(std::sin(ph)) * (1.f - m_hover);

        setOpacity(static_cast<GLubyte>(std::clamp(
            static_cast<float>(m_baseOpacity) + 60.f * m_hover + 7.f * sway + 35.f * m_burst,
            0.f, 255.f)));
        setVector(ccp(1.f, kDiagY + 0.12f * sway - 0.55f * m_hover - 0.25f * m_burst));
    }

public:
    static ScoreGradientLayer* create(cocos2d::CCSize size,
                                     cocos2d::ccColor3B first, cocos2d::ccColor3B second) {
        auto designed = designScoreGradient(first, second);
        auto* layer = new ScoreGradientLayer();
        if (!layer->initWithColor(cocos2d::ccc4(designed.first.r, designed.first.g, designed.first.b, 255),
                                  cocos2d::ccc4(designed.second.r, designed.second.g, designed.second.b, 255),
                                  ccp(1.f, kDiagY))) {
            delete layer;
            return nullptr;
        }
        layer->autorelease();
        layer->setContentSize(size);
        layer->setOpacity(layer->m_baseOpacity);
        layer->setID("paimon-score-gradient");
        layer->scheduleUpdate();
        return layer;
    }

    void setBaseOpacity(GLubyte o) {
        m_baseOpacity = o;
        setOpacity(static_cast<GLubyte>(std::clamp(
            static_cast<float>(o) + 60.f * m_hover, 0.f, 255.f)));
    }

    void setIdleSpeed(float s) {
        m_idleSpeed = std::clamp(s, 0.f, 5.f);
    }
    float idleSpeed() const { return m_idleSpeed; }
};

} // namespace paimon::scorecell
