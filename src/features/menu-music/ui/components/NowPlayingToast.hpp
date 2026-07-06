#pragma once

#include <Geode/Geode.hpp>
#include <string>

namespace paimon::menumusic {

class NowPlayingToast : public cocos2d::CCNode {
public:
    static void showForCurrent(cocos2d::CCNode* parent);

    void onExit() override;

protected:
    bool init(const std::string& title, const std::string& subtitle);

    enum class Phase {
        Wait,
        DropIn,
        Expand,
        Hold,
        Collapse,
        LiftOut,
        Done,
    };

    void onTick(float dt);
    void redrawPill(float width);
    void setContentOpacity(float opacity01);

    static float easeOutCubic(float t);
    static float easeInCubic(float t);
    static float easeInOutCubic(float t);

    cocos2d::CCDrawNode* m_pill = nullptr;
    cocos2d::CCNode* m_contentHolder = nullptr;

    float m_circleWidth = 0.f;
    float m_pillWidth = 0.f;
    float m_pillHeight = 0.f;

    float m_showY = 0.f;
    float m_hideY = 0.f;

    float m_stayFor = 1.5f;

    Phase m_phase = Phase::DropIn;
    float m_phaseTime = 0.f;
    float m_phaseDuration = 0.f;
};

} // namespace paimon::menumusic
