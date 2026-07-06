#pragma once
#include <Geode/Geode.hpp>
#include <string>

namespace paimon::scorecell {

class ScoreCellHoverWatcher : public cocos2d::CCNode {
public:
    static ScoreCellHoverWatcher* create(std::string const& type, float intensity);

    void setTransformTarget(cocos2d::CCNode* target,
                            float baseScaleX, float baseScaleY,
                            cocos2d::CCPoint basePos, float baseRot);

protected:
    bool init(std::string const& type, float intensity);
    void update(float dt) override;

    void enterHover();
    void exitHover();
    void applyTransformHover(bool on);
    void ensureGlow();
    void startShine();
    void stopShine();

    std::string m_type = "glow";
    float m_intensity = 0.6f;
    bool m_hovered = false;

    geode::Ref<cocos2d::CCNode> m_target = nullptr;
    bool m_hasTarget = false;
    float m_baseScaleX = 1.f;
    float m_baseScaleY = 1.f;
    cocos2d::CCPoint m_basePos = {0.f, 0.f};
    float m_baseRot = 0.f;

    geode::Ref<cocos2d::CCLayerColor> m_glow = nullptr;
    geode::Ref<cocos2d::CCNode> m_shine = nullptr;
};

void applyEntrance(cocos2d::CCNode* node, std::string const& type,
                   cocos2d::CCPoint finalPos, float finalScaleX, float finalScaleY);

}
