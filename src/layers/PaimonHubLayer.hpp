#pragma once
#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include "../features/forum/services/ForumApi.hpp"
#include "../utils/PaimonLoadingOverlay.hpp"

class PaimonHubLayer : public cocos2d::CCLayer {
protected:
    ~PaimonHubLayer();
    bool init() override;
    void update(float dt) override;
    void keyBackClicked() override;

    cocos2d::CCMenu* m_mainMenu = nullptr;
    cocos2d::CCLayer* m_homeTab = nullptr;
    cocos2d::CCLayer* m_newsTab = nullptr;
    cocos2d::CCLayer* m_forumTab = nullptr;
    cocos2d::CCMenu* m_homeMenu = nullptr;
    cocos2d::CCMenu* m_newsMenu = nullptr;
    cocos2d::CCMenu* m_forumMenu = nullptr;
    cocos2d::CCMenu* m_homeCategoryMenu = nullptr;
    cocos2d::CCMenu* m_homeActionsMenu = nullptr;
    cocos2d::CCNode* m_homeActionsAnchor = nullptr;
    geode::ScrollLayer* m_homeSettingsScroll = nullptr;
    cocos2d::CCLabelBMFont* m_homeCategoryTitle = nullptr;
    cocos2d::CCLabelBMFont* m_homeCategoryDesc = nullptr;
    CCMenuItemSpriteExtra* m_homeCategoryInfoBtn = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_homeCategoryBtns;
    int m_homeSelectedCategory = 0;
    float m_homeCategorySelectorY = 0.f;

    int m_currentTab = 0;
    std::vector<CCMenuItemSpriteExtra*> m_tabBtns;

    cocos2d::CCNode* m_forumPostList = nullptr;
    cocos2d::CCMenu* m_tagMenu = nullptr;
    std::vector<std::string> m_forumTags;
    std::vector<std::string> m_customTags;
    std::vector<std::string> m_activePredefTags;
    std::vector<std::string> m_visibleTags;
    std::vector<std::string> m_selectedTags;
    cocos2d::CCLabelBMFont* m_noPostsLabel = nullptr;
    cocos2d::CCLabelBMFont* m_emptyTagsHint = nullptr;

    enum class SortMode { Recent = 0, TopRated = 1, MostLiked = 2 };
    SortMode m_sortMode = SortMode::Recent;
    std::vector<CCMenuItemSpriteExtra*> m_sortBtns;

    // ── Forum sub-tabs (Browse / Create) ──
    int m_forumSubTab = 0; // 0 = Browse, 1 = Create
    std::vector<CCMenuItemSpriteExtra*> m_forumSubTabBtns;
    cocos2d::CCNode* m_forumBrowseNode = nullptr;
    cocos2d::CCNode* m_forumCreateNode = nullptr;

    // ── Inline create-post form ──
    geode::TextInput* m_createTitleInput = nullptr;
    geode::TextInput* m_createDescInput = nullptr;
    cocos2d::CCMenu* m_createTagMenu = nullptr;
    cocos2d::CCLabelBMFont* m_createTagsHint = nullptr;
    std::vector<std::string> m_createSelectedTags;

    cocos2d::CCNode* m_createPostOverlay = nullptr;
    cocos2d::CCNode* m_createTagOverlay = nullptr;
    cocos2d::CCNode* m_predefPickerOverlay = nullptr;
    geode::TextInput* m_postTitleInput = nullptr;
    geode::TextInput* m_postDescInput = nullptr;
    geode::TextInput* m_postTagInput = nullptr;
    geode::TextInput* m_newTagInput = nullptr;

    void onCloseCreatePost(cocos2d::CCObject*);
    void onSubmitPost(cocos2d::CCObject*);
    void onCloseCreateTag(cocos2d::CCObject*);
    void onSubmitTag(cocos2d::CCObject*);
    void onOpenPredefPicker(cocos2d::CCObject*);
    void onClosePredefPicker(cocos2d::CCObject*);
    void onTogglePredefTag(cocos2d::CCObject* sender);
    void onSortChanged(cocos2d::CCObject* sender);

    void onTabSwitch(cocos2d::CCObject* sender);
    void switchTab(int idx);

    void buildHomeTab();
    void buildNewsTab();
    void buildForumTab();
    void refreshHomeCategorySelector();
    void switchHomeCategory(int idx);
    void rebuildHomeCategoryCards();
    void rebuildHomeCategorySettings();
    void onPrevHomeCategory(cocos2d::CCObject*);
    void onNextHomeCategory(cocos2d::CCObject*);
    void onOpenHelp(cocos2d::CCObject*);

public:
    void onOpenConfig(cocos2d::CCObject*);
    void onOpenProfiles(cocos2d::CCObject*);
    void onOpenBackgrounds(cocos2d::CCObject*);
    void onOpenExtras(cocos2d::CCObject*);
    void onOpenSupport(cocos2d::CCObject*);
    void onCheckUpdate(cocos2d::CCObject*);
    void onBack(cocos2d::CCObject*);

protected:

    // Refresca el color/etiqueta del boton de update segun el estado actual
    void refreshUpdateButton();
    cocos2d::CCLabelBMFont* m_versionLabel = nullptr;

    void onRefreshNews(cocos2d::CCObject*);
    void onCreatePost(cocos2d::CCObject*);
    void onFilterByTag(cocos2d::CCObject*);
    void onCreateTag(cocos2d::CCObject*);
    void onViewPost(cocos2d::CCObject*);

    void refreshForumPosts();
    void renderPosts(std::vector<paimon::forum::Post> const& posts);
    void refreshTagButtons();

    void showForumLoading();
    void hideForumLoading();

    void onForumSubTabSwitch(cocos2d::CCObject* sender);
    void switchForumSubTab(int idx);
    void buildForumBrowse(cocos2d::CCNode* parent, cocos2d::CCMenu* menu);
    void buildForumCreate(cocos2d::CCNode* parent, cocos2d::CCMenu* menu);
    void rebuildCreateTagChips();
    void onCreateToggleTag(cocos2d::CCObject* sender);
    void onCreateAddCustomTag(cocos2d::CCObject*);
    void onCreateSubmit(cocos2d::CCObject*);

    PaimonLoadingOverlay* m_forumLoadingSpinner = nullptr;

    CCMenuItemSpriteExtra* makeBtn(char const* text, cocos2d::CCPoint pos,
        cocos2d::SEL_MenuHandler handler, cocos2d::CCNode* parent, float scale = 0.55f);

public:
    static PaimonHubLayer* create();
    static cocos2d::CCScene* scene();
};
