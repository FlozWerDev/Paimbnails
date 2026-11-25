#pragma once

#include <Geode/Geode.hpp>
#include <string>

namespace paimon::menumusic {

class CoverBlurBackground : public cocos2d::CCNode {
public:
    using cocos2d::CCNode::init;
    static CoverBlurBackground* create(cocos2d::CCSize const& size);

    // swaps in the given cover (empty path clears); safe to call while a blur is in flight.
    void setCoverFromPath(const std::string& absolutePath);
    void onExit() override;

protected:
    bool init(cocos2d::CCSize const& size);
    void applyBlurFromTexture(cocos2d::CCTexture2D* tex, std::uint64_t generation);
    void buildBlur(cocos2d::CCTexture2D* texture, std::uint64_t generation, float intensity);

    cocos2d::CCSize m_size;
    cocos2d::CCSprite* m_currentBlur = nullptr;
    std::uint64_t m_generation = 0;
    std::string m_lastPath;
    float m_lastIntensity = -1.f;
    bool m_blurPending = false;
};

} // namespace paimon::menumusic
