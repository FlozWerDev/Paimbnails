#pragma once
#include <Geode/Geode.hpp>
#include <Geode/cocos/layers_scenes_transitions_nodes/CCLayer.h>
#include <Geode/cocos/actions/CCActionInterval.h>
#include <Geode/cocos/actions/CCActionInstant.h>
#include <Geode/cocos/cocoa/CCGeometry.h>
#include <Geode/utils/cocos.hpp>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include "../../../core/RuntimeLifecycle.hpp"

namespace paimon::profilebg {

// Vector/opacity-only: animating the transform uncovered cell corners and
// accumulated offsets when switching effects, so only direction/color move.
class AnimatedGradientLayer : public cocos2d::CCLayerGradient {
public:
    static constexpr float kDiagY = -0.35f;

    static AnimatedGradientLayer* create(
        cocos2d::ccColor3B a,
        cocos2d::ccColor3B b
    ) {
        auto* node = new AnimatedGradientLayer();
        if (node && node->initWithColor(
                cocos2d::ccc4(a.r, a.g, a.b, 255),
                cocos2d::ccc4(b.r, b.g, b.b, 255)
            )) {
            node->m_baseA = a;
            node->m_baseB = b;
            node->m_baseOpacity = node->getOpacity();
            node->setStartColor(a);
            node->setEndColor(b);
            node->setVector({1.f, kDiagY});
            node->autorelease();
            return node;
        }
        delete node;
        return nullptr;
    }

    cocos2d::ccColor3B baseColorA() const { return m_baseA; }
    cocos2d::ccColor3B baseColorB() const { return m_baseB; }

    void setEffect(std::string const& effect, float speed) {
        m_effect  = effect;
        m_speed   = std::clamp(speed, 0.1f, 5.0f);
        m_time    = 0.0;

        // Full reset: no transform or color drift may survive a switch.
        this->stopAllActions();
        if (!m_hasBase) {
            m_basePos = this->getPosition();
            m_hasBase = true;
        } else {
            this->setPosition(m_basePos);
        }
        this->setRotation(0.f);
        this->setScale(1.f);
        this->setStartColor(m_baseA);
        this->setEndColor(m_baseB);
        this->setVector({1.f, kDiagY});

        // Snapshot the live opacity (callers setOpacity before setEffect):
        // pulse/hover breathe around the real value, not init-time 255.
        m_baseOpacity = this->getOpacity();
        m_hover = 0.f;
        m_burst = 0.f;
        m_wasHovered = false;
        // Always update-driven, even for "none": the hover burst must fire
        // on every mouse-enter regardless of the idle effect.
        this->scheduleUpdate();
    }

    std::string const& effect() const { return m_effect; }
    float speed() const { return m_speed; }

    virtual void update(float dt) override {
        cocos2d::CCLayerGradient::update(dt);
        if (paimon::isRuntimeShuttingDown()) return;
        if (dt <= 0.f) return;

        constexpr double kTwoPi = 6.283185307179586;
        m_time += static_cast<double>(dt) * m_speed;

        // Hover state (desktop only): smooth lift + retriggered burst on
        // every rising edge, so each pass over the cell animates.
        bool hovered = false;
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_MACOS)
        if (auto* cell = getParent()) {
            auto mouse = geode::cocos::getMousePos();
            auto size = getContentSize();
            auto local = cell->convertToNodeSpace(mouse);
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
                        hovered = cocos2d::CCRect(0.f, 0.f, bounds.width, bounds.height)
                                      .containsPoint(point);
                    }
                }
            }
        }
#endif
        float target = hovered ? 1.f : 0.f;
        m_hover += (target - m_hover) * (1.f - std::exp(-10.f * dt));
        if (std::abs(m_hover - target) < 0.001f) m_hover = target;
        if (hovered && !m_wasHovered) m_burst = 1.f;
        m_wasHovered = hovered;
        m_burst *= std::exp(-3.2f * dt);
        if (m_burst < 0.01f) m_burst = 0.f;

        float lift = 60.f * m_hover + 35.f * m_burst;
        float kick = -0.55f * m_hover - 0.25f * m_burst;
        auto hoveredOpacity = [&](float base) -> GLubyte {
            return static_cast<GLubyte>(std::clamp(base + lift, 0.f, 255.f));
        };

        if (m_effect == "none") {
            // Static gradient, but the hover burst still plays.
            this->setOpacity(hoveredOpacity(static_cast<float>(m_baseOpacity)));
            this->setVector({1.f, kDiagY + kick});
            return;
        }

        if (m_effect == "rotate") {
            // Sweep the gradient direction instead of rotating the quad:
            // corners can never be uncovered.
            double ang = m_time * 0.55;
            this->setVector({static_cast<float>(std::cos(ang)),
                             static_cast<float>(std::sin(ang)) + kick});
            this->setOpacity(hoveredOpacity(static_cast<float>(m_baseOpacity)));
        }
        else if (m_effect == "pulse") {
            // Breathe in brightness, not in size.
            double ph = std::fmod(m_time * kTwoPi / 2.4, kTwoPi);
            float k = static_cast<float>(0.5 - 0.5 * std::cos(ph));
            this->setOpacity(hoveredOpacity(
                static_cast<float>(m_baseOpacity) - 22.f + 44.f * k));
            this->setVector({1.f, kDiagY + 0.10f * (k - 0.5f) + kick});
        }
        else if (m_effect == "slide") {
            // Flowing sheen: sway direction + shimmer, quad stays put.
            double ph = std::fmod(m_time * kTwoPi / 3.2, kTwoPi);
            float s = static_cast<float>(std::sin(ph));
            this->setVector({1.f, kDiagY + 0.55f * s + kick});
            this->setOpacity(hoveredOpacity(
                static_cast<float>(m_baseOpacity) + 12.f * s));
        }
        else if (m_effect == "shift") {
            // Cosine ping-pong A->B->A: smooth at the mirrors, and the
            // period wrap keeps m_time bounded (no fmod precision decay).
            double period = 3.0;
            double ph = std::fmod(m_time * kTwoPi / period, kTwoPi);
            float k = static_cast<float>(0.5 - 0.5 * std::cos(ph));

            auto lerp = [](GLubyte a, GLubyte b, float t) -> GLubyte {
                float v = static_cast<float>(a) +
                          (static_cast<float>(b) - static_cast<float>(a)) * t;
                return static_cast<GLubyte>(std::clamp(v, 0.f, 255.f));
            };

            this->setStartColor({
                lerp(m_baseA.r, m_baseB.r, k),
                lerp(m_baseA.g, m_baseB.g, k),
                lerp(m_baseA.b, m_baseB.b, k)
            });
            this->setEndColor({
                lerp(m_baseB.r, m_baseA.r, k),
                lerp(m_baseB.g, m_baseA.g, k),
                lerp(m_baseB.b, m_baseA.b, k)
            });
            this->setOpacity(hoveredOpacity(static_cast<float>(m_baseOpacity)));
            this->setVector({1.f, kDiagY + kick});
        }
    }

protected:
    cocos2d::ccColor3B m_baseA{255,255,255};
    cocos2d::ccColor3B m_baseB{255,255,255};
    std::string m_effect = "none";
    float       m_speed  = 1.0f;
    double      m_time   = 0.0;
    GLubyte     m_baseOpacity = 255;
    float m_hover = 0.f;
    float m_burst = 0.f;
    bool m_wasHovered = false;
    cocos2d::CCPoint m_basePos{0.f, 0.f};
    bool m_hasBase = false;
};

inline std::vector<std::string> const& availableEffects() {
    static std::vector<std::string> const list = {
        "none", "rotate", "pulse", "shift", "slide"
    };
    return list;
}

inline bool isValidEffect(std::string const& effect) {
    auto const& list = availableEffects();
    return std::find(list.begin(), list.end(), effect) != list.end();
}

inline std::string normalizeEffect(std::string const& effect) {
    return isValidEffect(effect) ? effect : std::string("none");
}

inline float normalizeSpeed(float speed) {
    if (!std::isfinite(speed)) return 1.0f;
    return std::clamp(speed, 0.1f, 5.0f);
}

}
