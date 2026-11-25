#pragma once

#include <Geode/Geode.hpp>

class PaimonLoadingOverlay;
#include "../services/PendingQueue.hpp"
#include "../../../utils/ThumbnailTypes.hpp"
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class VerificationCenterLayer : public cocos2d::CCLayer, public LevelManagerDelegate {
protected:
    cocos2d::CCMenu* m_tabsMenu = nullptr;
    PendingCategory m_current = PendingCategory::Verify;

    geode::ScrollLayer* m_scrollLayer = nullptr;
    geode::Scrollbar* m_scrollbar = nullptr;
    cocos2d::CCNode* m_listContainer = nullptr;

    cocos2d::CCNode* m_previewPanel = nullptr;
    cocos2d::CCSprite* m_previewSprite = nullptr;
    cocos2d::CCNode* m_previewAnimNode = nullptr;
    cocos2d::CCLabelBMFont* m_previewLabel = nullptr;
    cocos2d::CCNode* m_previewBorder = nullptr;
    PaimonLoadingOverlay* m_previewSpinner = nullptr;

    int m_currentSuggestionIndex = 0;
    cocos2d::CCMenu* m_previewNavMenu = nullptr;
    CCMenuItemSpriteExtra* m_prevArrowBtn = nullptr;
    CCMenuItemSpriteExtra* m_nextArrowBtn = nullptr;
    cocos2d::CCLabelBMFont* m_suggestionCountLabel = nullptr;

    bool m_filterUnclaimed = false;
    std::vector<PendingItem> m_allItems;

    CCMenuItemSpriteExtra* m_refreshBtn = nullptr;
    unsigned m_queueGeneration = 0;
    bool m_actionPending = false;
    void onSession(cocos2d::CCObject*);

    std::vector<PendingItem> m_items;
    int m_selectedIndex = -1;
    int m_pendingLevelID = 0;
    int m_downloadCheckCount = 0;

    // Fast level metadata batch-lookup & LRU preview cache
    std::unordered_map<int, geode::Ref<GJGameLevel>> m_prefetchedLevels;
    std::unordered_set<int> m_prefetchedDownloadStarted;
    std::string m_pendingBatchSearchKey;
    std::unordered_map<std::string, std::vector<uint8_t>> m_previewBytesCache;
    std::deque<std::string> m_previewBytesOrder;
    std::unordered_set<std::string> m_previewPrefetchInFlight;

    void prefetchQueueLevelsBatch();
    void prefetchLevelData(int levelId);
    GJGameLevel* findCachedLevel(int levelId) const;
    void cachePreviewBytes(std::string const& url, std::vector<uint8_t> const& data);
    void prefetchPreviewNeighbors(int index);
    std::string resolvePreviewUrl(PendingItem const& item, int sugIdx) const;
    void applyRawPreviewBytes(std::vector<uint8_t> const& data);
    void loadLevelsFinished(cocos2d::CCArray* levels, char const* key) override;
    void loadLevelsFinished(cocos2d::CCArray* levels, char const* key, int) override;
    void loadLevelsFailed(char const* key) override;
    void loadLevelsFailed(char const* key, int) override;

    bool init() override;
    void onExit() override;
    void onBack(cocos2d::CCObject*);
    void keyBackClicked() override;

    void switchTo(PendingCategory cat);
    void onTabVerify(cocos2d::CCObject*);
    void onTabUpdate(cocos2d::CCObject*);
    void onTabReport(cocos2d::CCObject*);
    void onTabProfileBackground(cocos2d::CCObject*);
    void onTabProfileImg(cocos2d::CCObject*);

    void rebuildList();
    cocos2d::CCNode* createRowForItem(const PendingItem& item, float width, int index, std::string const& currentUsername);
    void highlightRow(int index);

    void showPreviewForItem(int index);
    void clearPreview();
    void setPreviewTexture(cocos2d::CCTexture2D* tex);
    void dismissPreviewSpinner();
    void showNoPreview();
    void updateNavigationArrows();

    void onSelectItem(cocos2d::CCObject* sender);
    void onOpenLevel(cocos2d::CCObject* sender);
    void onAccept(cocos2d::CCObject* sender);
    void onAcceptAll(cocos2d::CCObject* sender);
    void onReject(cocos2d::CCObject* sender);
    // which entry of a level's review gallery the moderator is looking at.
    // empty when the level has no per-file record to act on.
    std::string selectedSuggestionFilename(int levelID) const;
    void runQueueAction(int levelID, bool acceptAll, bool reject);
    void onClaimLevel(cocos2d::CCObject* sender);
    void onViewReport(cocos2d::CCObject* sender);
    void onViewBans(cocos2d::CCObject*);
    void onViewWhitelist(cocos2d::CCObject*);
    void onBanUser(cocos2d::CCObject*);
    void onPreviewClick(cocos2d::CCObject*);
    void onPrevSuggestion(cocos2d::CCObject*);
    void onNextSuggestion(cocos2d::CCObject*);
    void onToggleFilter(cocos2d::CCObject*);
    void onRefresh(cocos2d::CCObject*);
    void applyFilter();
    void checkLevelDownloaded(float dt);

public:
    static VerificationCenterLayer* create();
    static cocos2d::CCScene* scene();
};
