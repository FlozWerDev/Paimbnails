#pragma once

#include <Geode/Geode.hpp>
#include <string>
#include <vector>

namespace paimon::menumusic {

class VinylDisc;

class CoverHero : public cocos2d::CCNode {
public:
    static CoverHero* create(const cocos2d::CCSize& size, float skew = 28.f);

    void setCoverFromPath(const std::string& absolutePath);
    void setCoversFromPaths(std::vector<std::string> const& absolutePaths);
    void clearCover();

    std::string getCurrentCoverPath() const;

    void startSpinning();
    void stopSpinning();

    VinylDisc* getDisc() const { return m_smallDisc; }

    void setPausedAppearance(bool paused);

    void showCoverAtIndex(std::size_t index);
    void tickCarousel(float dt);

protected:
    bool init(const cocos2d::CCSize& size, float skew);

    cocos2d::CCSize m_size;
    float m_skew = 0.f;

    cocos2d::CCClippingNode* m_clip = nullptr;
    cocos2d::CCSprite* m_coverSprite = nullptr;
    cocos2d::CCNode* m_fallback = nullptr;
    cocos2d::CCNode* m_gradient = nullptr;
    VinylDisc* m_smallDisc = nullptr;
    cocos2d::CCDrawNode* m_borderGloss = nullptr;

    std::vector<std::string> m_coverPaths;
    std::size_t m_carouselIndex = 0;
    float m_carouselElapsed = 0.f;
    static constexpr float kCarouselInterval = 5.f;
};

} // namespace paimon::menumusic
