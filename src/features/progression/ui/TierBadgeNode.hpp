#pragma once

#include <Geode/Geode.hpp>
#include "../data/ProgressionTiers.hpp"

namespace paimon::progression {

// Tier medal on a paim_progTier* plate; effects are plain sprites, so a new
// tier only costs a row in the tier table.
class TierBadgeNode : public cocos2d::CCNode {
public:
    static TierBadgeNode* create(int level, float size);

    void setLevel(int level);
    // Ring around the medal. Negative hides it.
    void setProgress(float progress);
    void playIntro(float delay);
    void playLevelUp();
    void startPulse();

protected:
    bool init(int level, float size);
    void rebuild();
    void buildFrame(Tier const& tier);
    void buildEffects(Tier const& tier);
    void redrawRing(Tier const& tier);

    int m_level = 1;
    float m_size = 40.f;
    float m_progress = -1.f;
    bool m_pulses = false;
    cocos2d::CCNode* m_content = nullptr;
    cocos2d::CCNode* m_ring = nullptr;
    cocos2d::CCProgressTimer* m_ringFill = nullptr;
    cocos2d::CCLabelBMFont* m_levelLabel = nullptr;
};

// Tinted glow sprite, already set to additive blend.
cocos2d::CCSprite* makeRadialGlow(cocos2d::ccColor3B color, float radius, float peakAlpha);

// Medal plate for a tier, unscaled and untinted.
cocos2d::CCSprite* makeTierPlate(TierFrame frame);

} // namespace paimon::progression
