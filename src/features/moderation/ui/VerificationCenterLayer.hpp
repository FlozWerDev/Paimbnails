#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/Scrollbar.hpp>

class PaimonLoadingOverlay;
#include "../services/PendingQueue.hpp"
#include "../../../utils/ThumbnailTypes.hpp"

class VerificationCenterLayer : public cocos2d::CCLayer {
protected:
    // pestanas
    cocos2d::CCMenu* m_tabsMenu = nullptr;
    PendingCategory m_current = PendingCategory::Verify;

    // lista
    geode::ScrollLayer* m_scrollLayer = nullptr;
    geode::Scrollbar* m_scrollbar = nullptr;
    cocos2d::CCNode* m_listContainer = nullptr;

    // preview
    cocos2d::CCNode* m_previewPanel = nullptr;
    cocos2d::CCSprite* m_previewSprite = nullptr;
    cocos2d::CCNode* m_previewAnimNode = nullptr; // animated GIF sprite
    cocos2d::CCLabelBMFont* m_previewLabel = nullptr;
    cocos2d::CCNode* m_previewBorder = nullptr;
    PaimonLoadingOverlay* m_previewSpinner = nullptr;

    // suggestion navigation
    int m_currentSuggestionIndex = 0;
    cocos2d::CCMenu* m_previewNavMenu = nullptr;
    CCMenuItemSpriteExtra* m_prevArrowBtn = nullptr;
    CCMenuItemSpriteExtra* m_nextArrowBtn = nullptr;
    cocos2d::CCLabelBMFont* m_suggestionCountLabel = nullptr;

    // unclaimed filter
    bool m_filterUnclaimed = false;
    std::vector<PendingItem> m_allItems;

    // manual refresh
    CCMenuItemSpriteExtra* m_refreshBtn = nullptr;

    // datos
    std::vector<PendingItem> m_items;
    int m_selectedIndex = -1;
    int m_pendingLevelID = 0;
    int m_downloadCheckCount = 0;

    bool init() override;
    void onBack(cocos2d::CCObject*);
    void keyBackClicked() override;

    // tabs
    void switchTo(PendingCategory cat);
    void onTabVerify(cocos2d::CCObject*);
    void onTabUpdate(cocos2d::CCObject*);
    void onTabReport(cocos2d::CCObject*);
    void onTabProfileBackground(cocos2d::CCObject*);
    void onTabProfileImg(cocos2d::CCObject*);

    // lista
    void rebuildList();
    cocos2d::CCNode* createRowForItem(const PendingItem& item, float width, int index);
    void highlightRow(int index);

    // preview
    void showPreviewForItem(int index);
    void clearPreview();
    void setPreviewTexture(cocos2d::CCTexture2D* tex);
    void setPreviewSprite(cocos2d::CCSprite* spr);
    void loadCurrentSuggestionPreview();
    void updateNavigationArrows();

    // acciones
    void onSelectItem(cocos2d::CCObject* sender);
    void onOpenLevel(cocos2d::CCObject* sender);
    void onAccept(cocos2d::CCObject* sender);
    void onReject(cocos2d::CCObject* sender);
    void onClaimLevel(cocos2d::CCObject* sender);
    void onViewReport(cocos2d::CCObject* sender);
    void onViewThumb(cocos2d::CCObject* sender);
    void onViewProfileBackground(cocos2d::CCObject* sender);
    void onOpenProfile(cocos2d::CCObject* sender);
    void onViewBans(cocos2d::CCObject*);
    void onViewWhitelist(cocos2d::CCObject*);
    void onBanUser(cocos2d::CCObject*);
    void onPreviewClick(cocos2d::CCObject*);
    void onPrevSuggestion(cocos2d::CCObject*);
    void onNextSuggestion(cocos2d::CCObject*);
    void onToggleFilter(cocos2d::CCObject*);
    void onRefresh(cocos2d::CCObject*);
    void applyFilter();
    void autoRefreshClaims(float dt);
    void checkLevelDownloaded(float dt);

public:
    static VerificationCenterLayer* create();
    static cocos2d::CCScene* scene();
};
