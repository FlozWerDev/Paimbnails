#pragma once

#include <Geode/Geode.hpp>
#include <string>
#include <vector>

namespace paimon::menumusic {

class VinylDisc;

class CoverHero : public cocos2d::CCNode {
public:
    using cocos2d::CCNode::init;
    static CoverHero* create(const cocos2d::CCSize& size, float skew = 0.f);

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
    void tickTonearm(float dt);
    void onExit() override;

protected:
    bool init(const cocos2d::CCSize& size, float skew);
    void applyCoverTexture(cocos2d::CCTexture2D* texture);
    void buildTonearm();

    cocos2d::CCSize m_size;
    float m_skew = 0.f;

    cocos2d::CCClippingNode* m_clip = nullptr;
    cocos2d::CCSprite* m_coverSprite = nullptr;
    std::string m_coverPath;
    std::uint64_t m_coverGeneration = 0;
    bool m_coverLoading = false;
    bool m_paused = false;
    cocos2d::CCNode* m_fallback = nullptr;
    cocos2d::CCNode* m_gradient = nullptr;
    VinylDisc* m_smallDisc = nullptr;
    cocos2d::CCDrawNode* m_borderGloss = nullptr;
    cocos2d::CCNode* m_tonearmPivot = nullptr;
    float m_tonearmAngle = -6.f;

    std::vector<std::string> m_coverPaths;
    std::size_t m_carouselIndex = 0;
    float m_carouselElapsed = 0.f;
    static constexpr float kCarouselInterval = 5.f;
};

} // namespace paimon::menumusic
