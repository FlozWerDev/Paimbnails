#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>

class PaimonLoadingOverlay;
#include "../../../utils/HttpClient.hpp"

class ProfileReviewsPopup : public geode::Popup {
protected:
    int m_accountID;
    uint64_t m_requestGeneration = 0;
    cocos2d::CCLabelBMFont* m_averageLabel = nullptr;
    cocos2d::CCLabelBMFont* m_countLabel = nullptr;
    geode::ScrollLayer* m_scrollView = nullptr;
    cocos2d::CCNode* m_scrollContent = nullptr;
    PaimonLoadingOverlay* m_spinner = nullptr;

    bool init(int accountID);
    void loadReviews();
    void buildReviewList(float average, int count, const matjson::Value& reviews);
    cocos2d::CCNode* createReviewCell(std::string const& username, int stars, std::string const& message, float width);
    void animateReviewCells(std::vector<cocos2d::CCNode*> const& cells);

public:
    static ProfileReviewsPopup* create(int accountID);
};
