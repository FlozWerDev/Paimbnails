#pragma once

#include "../data/VersusTypes.hpp"

#include <Geode/Geode.hpp>

namespace paimon::versus {

// what a duel looks like while it is being played: two bars at the top with the
// icons, the rival's number, the countdown, and the latency of the fast channel.
class VersusHUDNode : public cocos2d::CCNode {
public:
    static VersusHUDNode* create();

    void refresh();
    void playCountdown(float seconds);
    void showResult(Outcome outcome);

protected:
    bool init() override;
    void update(float dt) override;
    cocos2d::CCNode* buildBar(bool own, float y);
    void buildRope(float y);

    cocos2d::CCNode* m_ownRow = nullptr;
    cocos2d::CCNode* m_rivalRow = nullptr;
    cocos2d::CCNode* m_ropeRow = nullptr;
    cocos2d::CCSprite* m_ropePip = nullptr;
    cocos2d::CCLabelBMFont* m_clock = nullptr;
    cocos2d::CCProgressTimer* m_ownFill = nullptr;
    cocos2d::CCProgressTimer* m_rivalFill = nullptr;
    cocos2d::CCLabelBMFont* m_ownLabel = nullptr;
    cocos2d::CCLabelBMFont* m_rivalLabel = nullptr;
    cocos2d::CCLabelBMFont* m_rivalName = nullptr;
    cocos2d::CCLabelBMFont* m_ping = nullptr;
    cocos2d::CCLabelBMFont* m_countdown = nullptr;
    cocos2d::CCNode* m_offline = nullptr;
    float m_pingTimer = 0.f;
    // refresh() runs every frame and the clock changes once a second; rebuilding
    // the label's quads at 240 fps for the same string is not free.
    int m_shownSeconds = -1;
};

} // namespace paimon::versus
