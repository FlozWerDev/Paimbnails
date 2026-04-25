#pragma once
#include <Geode/Geode.hpp>
#include <Geode/utils/function.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include "../../../managers/ThumbnailAPI.hpp"

class RatePopup : public geode::Popup {
protected:
    int m_levelID;
    std::string m_thumbnailId;
    int m_rating = 0;
    std::vector<CCMenuItemSpriteExtra*> m_starBtns;
    cocos2d::CCLabelBMFont* m_ratingLabel = nullptr;
    cocos2d::CCLabelBMFont* m_averageLabel = nullptr;
    cocos2d::CCLabelBMFont* m_countLabel = nullptr;

    bool init(int levelID, std::string thumbnailId);
    void onStar(cocos2d::CCObject* sender);
    void onSubmit(cocos2d::CCObject* sender);
    void updateStarVisuals();
    void loadExistingRating();

public:
    geode::CopyableFunction<void()> m_onRateCallback;
    static RatePopup* create(int levelID, std::string thumbnailId);
};
