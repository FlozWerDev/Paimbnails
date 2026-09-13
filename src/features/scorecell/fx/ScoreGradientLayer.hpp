#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/cocos.hpp>
#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include <algorithm>
#include <cmath>

namespace paimon::scorecell {

inline bool scoreGradientEnabled() {
    return modules::isEnabled("paimbnails.scoregradient.browser");
}

// Animate only the background: text and profile buttons keep their hit boxes.
class ScoreGradientLayer : public cocos2d::CCLayerGradient {
    float m_hover = 0.f;

    void update(float dt) override {
        if (paimon::isRuntimeShuttingDown()) return;
        auto* cell = getParent();
        if (!cell) return;
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
        if (m_hover == target) return;
        m_hover += (target - m_hover) * (1.f - std::exp(-10.f * std::max(dt, 0.f)));
        if (std::abs(m_hover - target) < 0.001f) m_hover = target;
        setOpacity(static_cast<GLubyte>(125.f + 70.f * m_hover));
        setVector(ccp(1.f, -0.15f - 0.7f * m_hover));
    }

public:
    static ScoreGradientLayer* create(cocos2d::CCSize size,
                                     cocos2d::ccColor3B first, cocos2d::ccColor3B second) {
        auto* layer = new ScoreGradientLayer();
        if (!layer->initWithColor(cocos2d::ccc4(first.r, first.g, first.b, 255),
                                  cocos2d::ccc4(second.r, second.g, second.b, 255),
                                  ccp(1.f, -0.15f))) {
            delete layer;
            return nullptr;
        }
        layer->autorelease();
        layer->setContentSize(size);
        layer->setOpacity(125);
        layer->setID("paimon-score-gradient");
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_MACOS)
        layer->scheduleUpdate();
#endif
        return layer;
    }
};

} // namespace paimon::scorecell
