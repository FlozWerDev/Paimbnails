#pragma once
#include <Geode/DefaultInclude.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/Scrollbar.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/GJUserScore.hpp>
#include "../../compat-mods/services/ModlyTypes.hpp"
#include <fmod.hpp>
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
    bool init() override;
    void onExit() override;
    void keyBackClicked() override;
    void onEnterTransitionDidFinish() override;
    void update(float dt) override;
    bool ccMouseScroll(float x, float y);

    void onBack(cocos2d::CCObject* sender);
    void onTab(cocos2d::CCObject* sender);
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

    void buildModeratorsList();
    void buildCreatorsList();
    void buildThumbnailsList();
    void buildCompatibleModsList();
    void sortModerators();

    // GD-style chrome, built once and kept across tabs
    void buildChrome();
    CCMenuItemToggler* createTabButton(std::string const& text, char const* id, cocos2d::CCPoint pos);
    void playIntro();
    cocos2d::CCNode* beginList();
    cocos2d::CCNode* addScrollList(float contentHeight);
    cocos2d::CCLayerColor* addCell(cocos2d::CCNode* content, float height, int index, float totalHeight);
    void animateCellIn(cocos2d::CCLayerColor* cell, int index);
    void showEmptyState();
    void finishTabLoad();

    // Moderator profiles: username -> accountID -> GD profile, throttled.
    // Icons, stats and banners land on their own cell; the list is never rebuilt.
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
    cocos2d::CCNode* m_listFrame = nullptr;
    float m_listW = 356.f;
    float m_listH = 0.f;
    cocos2d::CCPoint m_listCenter = {0.f, 0.f};
    float m_tabBaseY = 0.f;

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

    // Snapshot of the Modly catalog for the tab; index into it is the row tag.
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
