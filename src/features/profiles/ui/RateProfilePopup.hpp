#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>

class PaimonLoadingOverlay;
#include "../../../utils/HttpClient.hpp"

class RateProfilePopup : public geode::Popup {
protected:
    int m_accountID;
    std::string m_targetUsername;
    int m_rating = 0;
    float m_currentAverage = 0.f;
    int m_totalVotes = 0;
    std::vector<CCMenuItemSpriteExtra*> m_starBtns;
    geode::TextInput* m_messageInput = nullptr;
    cocos2d::CCLabelBMFont* m_averageLabel = nullptr;
    cocos2d::CCLabelBMFont* m_countLabel = nullptr;
    cocos2d::CCLabelBMFont* m_selectedLabel = nullptr;
    cocos2d::CCNode* m_starHighlight = nullptr;
    PaimonLoadingOverlay* m_loadingSpinner = nullptr;

    bool init(int accountID, std::string const& targetUsername);
    void onStar(cocos2d::CCObject* sender);
    void onSubmit(cocos2d::CCObject* sender);
    void onReport(cocos2d::CCObject* sender);
    void onViewReviews(cocos2d::CCObject* sender);
    void updateStarVisuals();
    void loadExistingRating();

public:
    static RateProfilePopup* create(int accountID, std::string const& targetUsername);
};
