#pragma once

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <string>

namespace paimon::thumbviewer {

inline constexpr float kPopupWidth = 400.f;
inline constexpr float kPopupHeight = 232.f;
inline constexpr float kToolWidth = 68.f;

cocos2d::CCRect imageArea(cocos2d::CCSize const& popupSize);
cocos2d::CCRect actionArea(cocos2d::CCSize const& popupSize);
cocos2d::CCRect focusedImageArea(cocos2d::CCSize const& popupSize);
void setChromeFade(cocos2d::CCNode* node, float opacity);

// header above the image: identity on the left, rating and gallery position on the right.
class InfoBar : public cocos2d::CCNode {
public:
    static InfoBar* create(float width);

    void setLevelId(int32_t levelID);
    void setCreator(std::string const& creator);
    void setRating(bool hasData, float average, int count);
    void setCounter(int index, int total);

protected:
    bool initWithWidth(float width);

    float m_width = 0.f;
    cocos2d::CCLabelBMFont* m_titleLabel = nullptr;
    cocos2d::CCLabelBMFont* m_creatorLabel = nullptr;
    cocos2d::CCSprite* m_starSprite = nullptr;
    cocos2d::CCLabelBMFont* m_ratingLabel = nullptr;
    cocos2d::CCLabelBMFont* m_counterLabel = nullptr;
    cocos2d::CCNode* m_dotsHolder = nullptr;

    void rebuildDots(int index, int total);
};

// native GD action button, or a standalone game icon when no caption is supplied.
CCMenuItemSpriteExtra* makeToolButton(char const* frame, float iconScale, char const* caption,
    cocos2d::CCObject* target, cocos2d::SEL_MenuHandler cb, cocos2d::ccColor3B tint = {255, 255, 255},
    char const* background = "GJ_button_01.png");
CCMenuItemSpriteExtra* makeToolButton(cocos2d::CCSprite* icon, float iconScale, char const* caption,
    cocos2d::CCObject* target, cocos2d::SEL_MenuHandler cb, cocos2d::ccColor3B tint = {255, 255, 255},
    char const* background = "GJ_button_01.png");

// horizontal separator between viewing and moderation actions.
cocos2d::CCNode* makeToolDivider(float width);

}
