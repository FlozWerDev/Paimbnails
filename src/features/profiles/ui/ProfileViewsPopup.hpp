#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>

class PaimonLoadingOverlay;
#include "../../../features/forum/services/ForumApi.hpp"

class ProfileViewsPopup : public geode::Popup {
protected:
    int m_accountID;
    uint64_t m_requestGeneration = 0;
    cocos2d::CCLabelBMFont* m_countLabel = nullptr;
    geode::ScrollLayer* m_scrollView = nullptr;
    cocos2d::CCNode* m_scrollContent = nullptr;
    PaimonLoadingOverlay* m_spinner = nullptr;

    bool init(int accountID);
    void loadViews();
    void buildViewsList(const std::vector<paimon::forum::ProfileView>& views);
    cocos2d::CCNode* createViewCell(std::string const& username, int64_t viewedAt, float width);

public:
    static ProfileViewsPopup* create(int accountID);
};
