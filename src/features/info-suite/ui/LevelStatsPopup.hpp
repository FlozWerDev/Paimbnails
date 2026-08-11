#pragma once

// Compact replacement for the vanilla "Level Stats" alert: your attempts, jumps
// and best runs on one screen, over the level's own thumbnail, plus the two
// charts the game never draws — where you die and how many jumps each attempt
// took. Everything else about the level lives in ExtendedInfoPopup.

#include <Geode/Geode.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include "../services/ProgressTracker.hpp"

namespace paimon::info {

class LevelStatsPopup : public geode::Popup {
public:
    static LevelStatsPopup* create(GJGameLevel* level);

protected:
    bool init(GJGameLevel* level);

    void buildTiles();
    // Both charts and their captions; rebuilt when the practice toggle flips.
    void buildCharts();

    cocos2d::CCNode* makeTile(float width, float height, std::vector<char const*> const& frames,
                              std::string const& value, char const* caption,
                              cocos2d::ccColor3B valueColor);

    void requestThumbnail();
    void applyThumbnail(cocos2d::CCTexture2D* texture);
    void installThumbnail(cocos2d::CCSprite* thumbnail, cocos2d::CCSize const& area, bool fadeIn);

    void onPracticeToggle(cocos2d::CCObject*);
    void onFullInfo(cocos2d::CCObject*);

    geode::Ref<GJGameLevel> m_level;
    LevelProgress m_progress;
    bool m_practice = false;

    cocos2d::CCNode* m_chartLayer = nullptr;
    bool m_hasThumbnail = false;
};

} // namespace paimon::info
