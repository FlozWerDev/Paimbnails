#pragma once
#include <Geode/DefaultInclude.hpp>
#include <Geode/loader/Event.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/ui/LoadingSpinner.hpp>

class PaimonLoadingOverlay;
#include <Geode/ui/ScrollLayer.hpp>
#include <fmod.hpp>

#include "../../foryou/services/RecommendationEngine.hpp"
#include <cstdint>

class LeaderboardLayer : public cocos2d::CCLayer, public LevelManagerDelegate {
public:
    enum class BackTarget {
        CreatorLayer,
        LevelSearchLayer
    };
protected:
    bool init() override;
    void keyBackClicked() override;
    void onExit() override;
    void onEnterTransitionDidFinish() override;
    void onExitTransitionDidStart() override;
    
    void onBack(cocos2d::CCObject* sender);
    void onTab(cocos2d::CCObject* sender);
    void showLoading();
    void dismissLoading();
    void loadLeaderboard(std::string type);
    void createList(std::string type);
    void onViewLevel(cocos2d::CCObject* sender);
    
    void loadForYou();
    void startForYouQueries();
    void fireNextForYouQuery();
    // Called once every planned query has answered: ranks what came back.
    void finishForYouQueries();
    void createForYouList();
    void onForYouPlayLevel(cocos2d::CCObject* sender);
    void onForYouRefresh(cocos2d::CCObject* sender);
    void onForYouPreferences(cocos2d::CCObject* sender);
    void onForYouDismiss(cocos2d::CCObject* sender);
    
    void loadLevelsFinished(cocos2d::CCArray* levels, char const* key) override;
    void loadLevelsFailed(char const* key) override;
    void setupPageInfo(gd::string, char const*) override;
    
    void updateLevelInfo();
    void checkLoadingComplete();
    
    void createThemeParticles();
    void spawnThemeParticle(float dt);
    void clearParticles();
    
    void onHistory(cocos2d::CCObject* sender);
    
    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCLayer* m_listMenu = nullptr;
    PaimonLoadingOverlay* m_loadingSpinner = nullptr;
    cocos2d::CCMenu* m_tabsMenu = nullptr;
    CCMenuItemSpriteExtra* m_historyButton = nullptr;
    std::vector<CCMenuItemToggler*> m_tabs;
    bool m_isLoadingTab = false;
    std::string m_currentType = "daily";

    geode::Ref<GJGameLevel> m_featuredLevel;
    long long m_featuredExpiresAt = 0;

    uint32_t m_requestGeneration = 0;
    uint32_t m_pendingLevelGeneration = 0;

    bool m_forYouActive = false;
    // Everything the planned queries returned, before scoring.
    std::vector<geode::Ref<GJGameLevel>> m_forYouCandidates;
    // The ranked feed actually rendered.
    std::vector<paimon::foryou::Recommendation> m_forYouFeed;
    std::vector<paimon::foryou::FeedQuery> m_forYouQueryQueue;
    int m_forYouQueryIndex = 0;
    // Guards against a second load while planning or ranking is in flight.
    bool m_forYouBusy = false;

    cocos2d::CCSprite* m_bgSprite = nullptr;
    cocos2d::CCLayerColor* m_bgOverlay = nullptr;
    float m_blurTime = 0.f;
    int m_bgSilenceCounter = 0;
    bool m_bgSpriteCastCached = false;
    cocos2d::CCSprite* m_cachedPaimonSprite = nullptr;
    BackTarget m_backTarget = BackTarget::CreatorLayer;

    bool m_dataLoaded = false;
    bool m_thumbLoaded = false;
    bool m_listCreated = false;
    
    cocos2d::CCNode* m_particleContainer = nullptr;
    cocos2d::ccColor3B m_themeColorA = {255, 200, 50};
    cocos2d::ccColor3B m_themeColorB = {255, 150, 30};

    FMOD::Channel* m_levelMusicChannel = nullptr; // unused after refactor; kept for ABI safety
    FMOD::Sound* m_levelMusicSound = nullptr;     // unused after refactor; kept for ABI safety
    FMOD::ChannelGroup* m_levelAudioGroup = nullptr; // unused after refactor; kept for ABI safety
    FMOD::DSP* m_lowpassDSP = nullptr;
    FMOD::DSP* m_reverbDSP = nullptr;

    // Push/pop position save so cave music resumes where it left off when the
    // user comes back from a pushed scene (LevelInfoLayer, history, etc.).
    unsigned int m_savedCaveMusicPosMs = 0;
    bool m_caveMusicShouldRestore = false;
    bool m_musicPlaying = false;
    bool m_leavingForGood = false;
    bool m_goingToHistory = false;
    bool m_didSuspendDynSong = false;
    
    static constexpr int AUDIO_FADE_STEPS = 15;
    static constexpr float AUDIO_FADE_MS = 500.f;
    bool m_isFadingCaveIn = false;
    bool m_isFadingCaveOut = false;
    int m_lifecycleToken = 0;
    
    FMOD::DSP* m_fftDSP = nullptr;
    float m_beatPulse = 0.f;
    float m_prevBassLevel = 0.f;
    float m_glowPulse = 0.f;
    float m_bgPulse = 0.f;
    float m_particleBoost = 0.f;
    cocos2d::CCLayerColor* m_glowOverlay = nullptr;
    cocos2d::CCLayerColor* m_beatFlash = nullptr;
    float m_audioReactTime = 0.f;
    void updateAudioReactive(float dt);
    float getAudioBassLevel();
    
    void startCaveMusic();
    void fadeOutCaveMusic();
    void killCaveMusic();
    void applyCaveEffect();
    void removeCaveEffect();
    void executeCaveFade(int step, int totalSteps, float from, float to, bool fadeOut);
    
    void fadeOutMenuMusic();
    void fadeInMenuMusic();
    void executeMenuFade(int step, int totalSteps, float from, float to);
    void ensureBgSilenced();
    void delaySilenceBg(float dt);
    void delaySilenceBg2(float dt);

    static constexpr int TAG_NAME_LABEL = 2001;
    static constexpr int TAG_CREATOR_LABEL = 2002;
    static constexpr int TAG_TIME_LABEL = 2003;
    static constexpr int TAG_BADGE_LABEL = 2004;
    static constexpr int TAG_DIFF_SPRITE = 2005;

    void update(float dt) override;
    void updateBackground(int levelID);
    void applyBackground(cocos2d::CCTexture2D* texture);

public:
    ~LeaderboardLayer();
    static LeaderboardLayer* create(BackTarget backTarget = BackTarget::CreatorLayer);
    static cocos2d::CCScene* scene(BackTarget backTarget = BackTarget::CreatorLayer);
};
