#pragma once

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <string>

namespace paimon::thumbviewer {

// Top strip: level id / creator on the left, star rating on the right, page dots below.
class InfoBar : public cocos2d::CCNode {
public:
    static InfoBar* create(float width);

    void setLevelId(int32_t levelID);
    void setCreator(std::string const& creator);
    void setRating(bool hasData, float average, int count);
    void setCounter(int index, int total);

protected:
    bool init(float width);

    float m_width = 0.f;
    cocos2d::CCLabelBMFont* m_titleLabel = nullptr;
    cocos2d::CCLabelBMFont* m_creatorLabel = nullptr;
    cocos2d::CCSprite* m_starSprite = nullptr;
    cocos2d::CCLabelBMFont* m_ratingLabel = nullptr;
    cocos2d::CCLabelBMFont* m_counterLabel = nullptr;
    cocos2d::CCNode* m_dotsHolder = nullptr;

    void rebuildDots(int index, int total);
};

// Icon with an optional caption underneath; the whole thing is one menu item.
CCMenuItemSpriteExtra* makeToolButton(char const* frame, float iconScale, char const* caption,
    cocos2d::CCObject* target, cocos2d::SEL_MenuHandler cb, cocos2d::ccColor3B tint = {255, 255, 255});

// Thin vertical gold line to separate toolbar groups inside a RowLayout.
cocos2d::CCNode* makeToolDivider(float height);

}
