#pragma once
#include <Geode/DefaultInclude.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/Scrollbar.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/GJUserScore.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include <Geode/ui/NineSlice.hpp>
#include "../../compat-mods/services/ModlyTypes.hpp"
#include <fmod.hpp>
#include <matjson.hpp>
#include <ctime>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class PaimonLoadingOverlay;
class SimplePlayer;

class CommunityHubLayer : public cocos2d::CCLayer {
public:
    static CommunityHubLayer* create();
    static cocos2d::CCScene* scene();

    enum class Tab {
        Moderators,
        TopCreators,
        TopThumbnails,
        CompatibleMods
    };

protected:
    // why the last request failed; picks the message in the empty state.
    enum class LoadIssue {
        None,
        Locked,   // ban verification failed, so the mod blocks every request
        Storage,  // backend answered STORAGE_ERROR
        Network,
        Server,
        BadData
    };

    bool init() override;
    void onExit() override;
    void keyBackClicked() override;
    void onEnterTransitionDidFinish() override;
    void update(float dt) override;
    void keyDown(cocos2d::enumKeyCodes key, double timestamp) override;

    void onBack(cocos2d::CCObject* sender);
    void onTab(cocos2d::CCObject* sender);
    void selectTab(Tab tab);
    void onScrollTop(cocos2d::CCObject* sender);
    void onCreatorRow(cocos2d::CCObject* sender);
    void onThumbRow(cocos2d::CCObject* sender);
    void onModProfile(cocos2d::CCObject* sender);
    void onCompatMod(cocos2d::CCObject* sender);
    void onInfoButton(cocos2d::CCObject* sender);
    void onRetryTimer(float dt);
    void onIconTick(float dt);

    void loadTab(Tab tab);
    void loadModerators(int attempt = 0);
    void loadTopCreators(int attempt = 0);
    void loadTopThumbnails(int attempt = 0);
    void loadCompatibleMods();
    void retryLoadTab(Tab tab, int attempt);
    void scheduleRetry(Tab tab, int attempt);

    // shared response path for the backend tabs: parse, snapshot, retry or fall back.
    void handleTabResponse(Tab tab, int attempt, bool success, std::string const& response);
    bool parseTabData(Tab tab, matjson::Value const& json);
    bool parseModerators(matjson::Value const& json);
    bool parseCreators(matjson::Value const& json);
    bool parseThumbnails(matjson::Value const& json);
    void prepareModeratorScores();
    void clearTabData(Tab tab);
    bool tabHasEntries(Tab tab) const;
    LoadIssue classifyFailure(std::string const& response, int& httpCode) const;
    std::string issueMessage() const;

    // last good response per tab on disk, shown when the server can't answer.
    void saveSnapshot(Tab tab, matjson::Value const& data);
    std::optional<matjson::Value> loadSnapshot(Tab tab, std::time_t& savedAt) const;

    void buildCurrentList();
    void buildModeratorsList();
    void buildCreatorsList();
    void buildThumbnailsList();
    void buildCompatibleModsList();
    void sortModerators();

    // gd-style chrome, built once and kept across tabs
    void buildChrome();
    CCMenuItemToggler* createTabButton(std::string const& text, char const* id, cocos2d::CCPoint pos, float width);
    void playIntro();
    cocos2d::CCNode* beginList();
    cocos2d::CCNode* addScrollList(float contentHeight);
    cocos2d::CCLayerColor* addCell(cocos2d::CCNode* content, float height, int index, float totalHeight);
    void animateCellIn(cocos2d::CCLayerColor* cell, int index);
    void showEmptyState(bool error = false);
    void finishTabLoad();

    // ambient fx: accent-tinted particles, hover glow on rows, back-to-top button.
    cocos2d::ccColor3B accentFor(Tab tab) const;
    void spawnParticle();
    void updateAccent(float dt);
    void updateHover(float dt);
    void updateScrollTop();
    void registerHoverRow(cocos2d::CCNode* cell);
    cocos2d::CCMenu* addRowMenu(cocos2d::CCNode* content, float totalH);
    CCMenuItemSpriteExtra* addRowButton(cocos2d::CCMenu* menu, float totalH, float cellH,
        int row, int tag, cocos2d::SEL_MenuHandler handler);
    cocos2d::CCNode* makeMetricBar(float width, float fraction, cocos2d::ccColor3B color);

    // decorated chrome extras, built once with the chrome and kept across tabs
    void buildHeader();
    void buildToolbar();
    void buildFooter();
    void updateTabHeader();
    void layoutToolbar(bool sortable);
    void setSortLabel(std::string const& text);
    void updateFooter(int shown, bool fromCache);
    char const* tabIconFrame(Tab tab) const;
    std::string tabSubtitle(Tab tab) const;
    void onSearchChanged(std::string const& text);
    void onSort(cocos2d::CCObject* sender);
    void onRefresh(cocos2d::CCObject* sender);
    void setToolbarBusy(bool busy);
    // top-3 crown/medal strip for ranking tabs; returns the vertical space it took.
    cocos2d::CCNode* buildPodium(std::vector<std::string> const& names,
        std::vector<cocos2d::CCNode*> const& faces, std::vector<std::string> const& subs);
    bool rowMatchesFilter(std::string const& name) const;
    cocos2d::CCSprite* makeRankMedal(int rank);

    // moderator profiles (username -> accountid -> gd, throttled); cells update in place, never rebuilt.
    void startIconPipeline();
    void beginIconRequest(std::string const& key);
    void requestUserInfo(std::string const& key, int accountID);
    void onAccountIDResolved(std::string const& key, bool ok, int accountID);
    void onUserInfoResponse(std::string const& key, bool ok, std::string const& response, int accountID);
    void finishIconRequest(std::string const& key, bool success);
    void refreshSlot(std::string const& key, bool animated);
    void applyBanner(std::string const& key);
    void queueBannerLoad(std::string const& key);
    SimplePlayer* createIcon(GJUserScore* score, bool hasData);
    void cacheModerators();

    void clearList();
    void showLoading();
    void hideLoading();

    Tab m_currentTab = Tab::Moderators;
    cocos2d::CCMenu* m_tabsMenu = nullptr;
    std::vector<CCMenuItemToggler*> m_tabs;

    PaimonLoadingOverlay* m_loadingSpinner = nullptr;

    cocos2d::CCNode* m_listContainer = nullptr;
    geode::ScrollLayer* m_scrollView = nullptr;
    CCMenuItemSpriteExtra* m_infoButton = nullptr;
    cocos2d::CCLabelBMFont* m_title = nullptr;
    cocos2d::CCNodeRGBA* m_card = nullptr;
    cocos2d::CCRect m_cardRect;
    cocos2d::CCNode* m_listFrame = nullptr;
    float m_listW = 356.f;
    float m_listH = 0.f;
    cocos2d::CCPoint m_listCenter = {0.f, 0.f};
    float m_tabBaseY = 0.f;
    float m_toolbarY = 0.f;
    float m_subtitleY = 0.f;

    cocos2d::CCLabelBMFont* m_subtitle = nullptr;
    geode::NineSlice* m_searchBg = nullptr;
    geode::TextInput* m_searchInput = nullptr;
    std::string m_searchFilter;
    CCMenuItemSpriteExtra* m_sortButton = nullptr;
    cocos2d::CCLabelBMFont* m_sortLabel = nullptr;
    CCMenuItemSpriteExtra* m_refreshButton = nullptr;
    geode::LoadingSpinner* m_refreshSpinner = nullptr;
    cocos2d::CCLabelBMFont* m_footerLabel = nullptr;
    cocos2d::CCNode* m_statusDot = nullptr;
    cocos2d::CCNode* m_toolbarMenu = nullptr;
    // sort mode per ranking tab; creators/thumbnails cycle, moderators keep role order.
    int m_sortMode = 0;
    bool m_lastLoadFromCache = false;
    bool m_loadFailed = false;
    LoadIssue m_issue = LoadIssue::None;
    int m_issueCode = 0;
    bool m_fromSnapshot = false;
    std::time_t m_snapshotAt = 0;
    int m_slideDir = 1;

    cocos2d::CCNode* m_fxLayer = nullptr;
    cocos2d::CCNodeRGBA* m_listBorder = nullptr;
    CCMenuItemSpriteExtra* m_topButton = nullptr;
    cocos2d::ccColor3B m_accent = {170, 130, 255};
    cocos2d::ccColor3B m_accentTarget = {170, 130, 255};
    cocos2d::ccColor3B m_appliedAccent = {0, 0, 0};
    float m_fxClock = 0.f;
    bool m_topShown = false;

    struct HoverRow {
        geode::Ref<cocos2d::CCLayerColor> glow;
        geode::Ref<cocos2d::CCNode> cell;
    };
    std::vector<HoverRow> m_hoverRows;

    struct ModEntry {
        std::string username;
        std::string role;
        int accountID = 0;
    };
    std::vector<ModEntry> m_modEntries;
    geode::Ref<cocos2d::CCArray> m_modScores;

    struct IconState {
        int attempts = 0;
        float readyAt = 0.f;
        bool inFlight = false;
        bool done = false;
    };
    struct IconSlot {
        geode::Ref<GJUserScore> score;
        geode::Ref<cocos2d::CCNode> cell;
        geode::Ref<cocos2d::CCNode> icon;
        geode::Ref<cocos2d::CCNode> banner;
        geode::Ref<cocos2d::CCNode> stats;
        geode::Ref<cocos2d::CCNode> rank;
        geode::Ref<CCMenuItemSpriteExtra> button;
        std::string key;
        std::string username;
        bool bannerShown = false;
        bool bannerQueued = false;
    };
    std::unordered_map<std::string, IconState> m_iconStates;
    std::vector<IconSlot> m_iconSlots;
    IconSlot* findIconSlot(std::string const& key);
    float m_iconClock = 0.f;

    struct CreatorEntry {
        std::string username;
        int accountID = 0;
        int uploadCount = 0;
        float avgRating = 0.f;
    };
    std::vector<CreatorEntry> m_creatorEntries;

    struct ThumbnailEntry {
        int levelId = 0;
        float rating = 0.f;
        int count = 0;
        std::string uploadedBy;
        int accountID = 0;
    };
    std::vector<ThumbnailEntry> m_thumbnailEntries;

    // snapshot of the modly catalog for the tab; index into it is the row tag.
    std::vector<paimon::compat_mods::ModlyMod> m_compatMods;
    bool m_compatLoadFailed = false;

    FMOD::DSP* m_lowpassDSP = nullptr;
    FMOD::DSP* m_reverbDSP = nullptr;
    float m_savedBgVolume = 1.0f;
    bool m_caveApplied = false;
    bool m_isExiting = false;
    int m_retryTag = 0;
    Tab m_pendingRetryTab = Tab::Moderators;
    int m_pendingRetryAttempt = 0;
    void applyCaveEffect();
    void removeCaveEffect();

public:
    ~CommunityHubLayer();
};
