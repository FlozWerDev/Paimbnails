#pragma once

#include <Geode/Geode.hpp>

namespace paimon::menuloop {

class NowPlayingCard : public cocos2d::CCNode {
public:
    static NowPlayingCard* create(const std::string& text);
    static void showForCurrentSong(cocos2d::CCNode* parent);

protected:
    bool init(const std::string& text);
};

} // namespace paimon::menuloop
