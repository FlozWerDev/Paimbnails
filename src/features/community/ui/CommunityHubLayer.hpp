#pragma once
#include <Geode/DefaultInclude.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/binding/GJUserScore.hpp>
#include <Geode/binding/UserInfoDelegate.hpp>

class PaimonLoadingOverlay;
#include <Geode/binding/CustomListView.hpp>
#include <Geode/binding/GJListLayer.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/utils/web.hpp>
#include <fmod.hpp>
#include <memory>
#include <unordered_set>

class CommunityHubLayer : public cocos2d::CCLayer, public UserInfoDelegate {
public:
    static CommunityHubLayer* create();
    static cocos2d::CCScene* scene();
    friend class PaimonCommunityHubGameLevelManager;

    enum class Tab {
        Moderators,
        TopCreators,
        TopThumbnails,
        CompatibleMods
    };

protected:
    bool init() override;
    void keyBackClicked() override;
    void onEnterTransitionDidFinish() override;
    void update(float dt) override;
    bool ccMouseScroll(float x, float y);

    void onBack(cocos2d::CCObject* sender);
    void onTab(cocos2d::CCObject* sender);
    void onModProfile(cocos2d::CCObject* sender);
    void onDeferredModeratorsRebuild(float);
    void addInfoButton(Tab tab);
    void onInfoButton(cocos2d::CCObject* sender);

    void getUserInfoFinished(GJUserScore* score) override;
    void getUserInfoFailed(int type) override;
    void userInfoChanged(GJUserScore* score) override;

    // data loading
    void loadTab(Tab tab);
    void loadModerators(int attempt = 0);
    void loadTopCreators(int attempt = 0);
    void loadTopThumbnails(int attempt = 0);
    void retryLoadTab(Tab tab, int attempt);
    void requestNativeModeratorLookup(std::string const& username);
    void requestNativeModeratorUserInfo(std::string const& username, int accountID);
    void onNativeModeratorSearchCompleted(std::string const& username, int accountID);
    void onNativeModeratorUserInfoCompleted(std::string const& username, int accountID);
    void clearPendingNativeModeratorRequests();

    // list building
    void buildModeratorsList();
    void buildCreatorsList();
    void buildThumbnailsList();
    void loadCompatibleMods();
    void buildCompatibleModsList();
    void requestModeratorsListRebuild();

    void onAllProfilesFetched();

    void clearList();
    void showLoading();
    void hideLoading();
    void ensureBgSilenced();

    // tab state
    Tab m_currentTab = Tab::Moderators;
    cocos2d::CCMenu* m_tabsMenu = nullptr;
    std::vector<CCMenuItemToggler*> m_tabs;
    bool m_isLoadingTab = false;
    bool m_moderatorsRebuildQueued = false;

    // loading
    PaimonLoadingOverlay* m_loadingSpinner = nullptr;

    // list container
    cocos2d::CCNode* m_listContainer = nullptr;
    geode::ScrollLayer* m_scrollView = nullptr;

    // moderators data
    struct ModEntry {
        std::string username;
        std::string role; // "admin" or "mod"
        int accountID = 0;
    };
    std::vector<ModEntry> m_modEntries;
    geode::Ref<cocos2d::CCArray> m_modScores;
    std::unordered_set<int> m_requestedModeratorProfiles;
    std::vector<std::string> m_pendingNativeModeratorLookupQueue;
    size_t m_nextNativeModeratorLookupIndex = 0;
    std::string m_activeNativeModeratorSearchUsername;
    std::string m_activeNativeModeratorUserInfoUsername;
    int m_activeNativeModeratorUserInfoAccountID = 0;

    // creators data
    struct CreatorEntry {
        std::string username;
        int accountID = 0;
        int uploadCount = 0;
        float avgRating = 0.f;
    };
    std::vector<CreatorEntry> m_creatorEntries;

    // thumbnails data
    struct ThumbnailEntry {
        int levelId = 0;
        float rating = 0.f;
        int count = 0;
        std::string uploadedBy;
        int accountID = 0;
    };
    std::vector<ThumbnailEntry> m_thumbnailEntries;

    // FMOD efecto cueva sobre musica de menu
    FMOD::DSP* m_lowpassDSP = nullptr;
    FMOD::DSP* m_reverbDSP = nullptr;
    float m_savedBgVolume = 1.0f;
    bool m_caveApplied = false;
    bool m_isExiting = false;
    int m_retryTag = 0; // incremented on tab switch to cancel pending retries
    Tab m_pendingRetryTab = Tab::Moderators;
    int m_pendingRetryAttempt = 0;
    void onRetryTimer(float dt);
    void applyCaveEffect();
    void removeCaveEffect();

public:
    ~CommunityHubLayer();
};
