#pragma once

#include <Geode/Geode.hpp>
#include <string>

namespace paimon::menumusic {

class VinylDisc : public cocos2d::CCNode {
public:
    static VinylDisc* create(float radius);

    void setCoverFromPath(const std::string& absolutePath);
    void clearCover();

    void startSpinning();
    void stopSpinning();
    bool isSpinning() const { return m_spinning; }

    void setSpinSpeed(float degPerSec) { m_spinSpeed = degPerSec; }

    void setPausedAppearance(bool paused);

    void onExit() override;

protected:
    bool init(float radius);
    void tick(float dt);

    float m_radius = 60.f;
    float m_spinSpeed = 40.f;
    bool m_spinning = false;

    cocos2d::CCNode* m_rotating = nullptr;
    cocos2d::CCClippingNode* m_coverClip = nullptr;
    cocos2d::CCSprite* m_coverSprite = nullptr;
    cocos2d::CCSprite* m_centerDot = nullptr;
};

} // namespace paimon::menumusic
