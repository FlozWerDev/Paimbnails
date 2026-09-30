#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/cocos.hpp>
#include "../../../core/RuntimeLifecycle.hpp"
#include "ScoreGradientDesign.hpp"
#include <algorithm>
#include <cmath>

namespace paimon::scorecell {

// icon-color gradient background. vector/opacity-only: quad never moves, no uncovered corners.
class ScoreGradientLayer : public cocos2d::CCLayerGradient {
    float m_hover = 0.f;
    double m_time = 0.0;
    GLubyte m_baseOpacity = 125;
    float m_idleSpeed = 1.f;
    // retriggered pulse: 1 on every mouse-enter rising edge, then exponential decay.
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
            // respect the viewport of scrollable leaderboard lists.
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

        // rising edge: restart the pulse so each pass animates.
        if (hovered && !m_wasHovered) m_burst = 1.f;
        m_wasHovered = hovered;
        m_burst *= std::exp(-3.2f * dt);
        if (m_burst < 0.01f) m_burst = 0.f;

        // idle sheen: direction sway + faint breath, fading as hover takes over.
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
        layer->setID("paimon-score-gradient"_spr);
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
};

} // namespace paimon::scorecell
