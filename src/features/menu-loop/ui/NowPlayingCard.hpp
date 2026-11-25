#pragma once

#include <Geode/Geode.hpp>

namespace paimon::menuloop {

// now Playing notification for the menu loop song. Drawn by the menu music toast,
// so it uses the same animation settings.
class NowPlayingCard {
public:
    static void showForCurrentSong(cocos2d::CCNode* parent);
};

} // namespace paimon::menuloop
