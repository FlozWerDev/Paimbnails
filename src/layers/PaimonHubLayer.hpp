#pragma once
#include <Geode/Geode.hpp>

namespace paimon::forum {
class ForumPanel;
}

class PaimonHubLayer : public cocos2d::CCLayer {
protected:
    ~PaimonHubLayer();
    bool init() override;
    void keyBackClicked() override;
    bool scrollWheel(float x, float y) override;
    void update(float dt) override;

    // liquid Glass (src/features/liquid-glass): plates on the home and forum panels.
    // buttons are covered by the global ButtonSprite hook.
    void setupGlassChrome();

    cocos2d::CCMenu* m_mainMenu = nullptr;
    cocos2d::CCLayerRGBA* m_homeTab = nullptr;
    cocos2d::CCLayerRGBA* m_forumTab = nullptr;
    cocos2d::CCMenu* m_homeMenu = nullptr;
    cocos2d::CCMenu* m_forumMenu = nullptr;
    cocos2d::CCMenu* m_homeActionsMenu = nullptr;
    geode::ScrollLayer* m_homeActionsScroll = nullptr;
    cocos2d::CCLabelBMFont* m_homeCategoryTitle = nullptr;
    cocos2d::CCLabelBMFont* m_homeCategoryDesc = nullptr;
    CCMenuItemSpriteExtra* m_homeCategoryInfoBtn = nullptr;
    geode::TextInput* m_searchInput = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_homeCategoryBtns;
    int m_homeSelectedCategory = 0;
    int m_homeFilter = 0;
    float m_homeScrollTarget = 0.f;
    bool m_homeScrollTargetSet = false;
    cocos2d::CCNode* m_homeFilterBar = nullptr;

    cocos2d::CCNodeRGBA* m_sidebarBg = nullptr;
    cocos2d::CCNodeRGBA* m_detailsBg = nullptr;
    cocos2d::CCMenu* m_sidebarMenu = nullptr;

    int m_currentTab = 0;
    std::vector<CCMenuItemSpriteExtra*> m_tabBtns;

    cocos2d::CCNode* m_bgNode = nullptr;
    cocos2d::ccColor3B m_bgColorHome = {20, 20, 32};
    cocos2d::ccColor3B m_bgColorSub = {160, 170, 195};

    paimon::forum::ForumPanel* m_forumPanel = nullptr;

    void onTabSwitch(cocos2d::CCObject* sender);
    void switchTab(int idx);

    void buildHomeTab();
    void buildHomeFilterBar();
    void buildForumTab();
    void refreshHomeCategorySelector();
    void switchHomeCategory(int idx);
    void rebuildHomeCategoryCards(bool resetScroll = true);
    void onOpenHelp(cocos2d::CCObject*);

public:
    void activateHubAction(std::string const& id, std::function<void(PaimonHubLayer*)> const& callback);
    void toggleHubFavorite(std::string const& id);
    void onOpenConfig(cocos2d::CCObject*);
    void onOpenProfiles(cocos2d::CCObject*);
    void onOpenBackgrounds(cocos2d::CCObject*);
    void onOpenPaiDraw(cocos2d::CCObject*);
    void onOpenSupport(cocos2d::CCObject*);
    void onOpenDiscordConfig(cocos2d::CCObject*);
    void onCheckUpdate(cocos2d::CCObject*);
    void refreshUpdateBadge(float dt);
    void onBack(cocos2d::CCObject*);

protected:
    cocos2d::CCLabelBMFont* m_versionLabel = nullptr;

    bool m_gdMode = false;
    int m_gdHomeState = 0;
    int m_gdCategoryIdx = 0;
    cocos2d::CCNode* m_gdContent = nullptr;
    cocos2d::CCMenu* m_gdContentMenu = nullptr;
    geode::ScrollLayer* m_gdScroll = nullptr;
    cocos2d::CCLabelBMFont* m_gdHintLabel = nullptr;
    cocos2d::CCNode* m_gdTourOverlay = nullptr;

    void buildGDShell();
    void gdBuildHome();
    void gdClearContent();
    void gdShowCategories(bool animate = true);
    void gdShowCategory(int idx);
    void gdShowSearchResults(std::string const& query);
    void gdSetHint(std::string const& text);
    void gdStartTour();
    void gdShowTourStep(int step);
    void gdEndTour();

public:
    void onToggleUIStyle(cocos2d::CCObject*);

    static PaimonHubLayer* create();
    static cocos2d::CCScene* scene();
};
