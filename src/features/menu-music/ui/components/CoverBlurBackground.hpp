#pragma once

#include <Geode/Geode.hpp>
#include <string>

namespace paimon::menumusic {

class CoverBlurBackground : public cocos2d::CCNode {
public:
    static CoverBlurBackground* create(cocos2d::CCSize const& size);

    // swaps in the given cover (empty path clears); safe to call while a blur is in flight.
    void setCoverFromPath(const std::string& absolutePath);

protected:
    bool init(cocos2d::CCSize const& size);
    void applyBlurFromTexture(cocos2d::CCTexture2D* tex, std::uint64_t generation);

    cocos2d::CCSize m_size;
    cocos2d::CCSprite* m_currentBlur = nullptr;
    std::uint64_t m_generation = 0;
    std::string m_lastPath;
};

} // namespace paimon::menumusic
