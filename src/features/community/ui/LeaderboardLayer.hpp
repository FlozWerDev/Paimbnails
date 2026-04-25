#pragma once
#include <Geode/DefaultInclude.hpp>
#include <Geode/loader/Event.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/ui/LoadingSpinner.hpp>

class PaimonLoadingOverlay;
#include <Geode/ui/ScrollLayer.hpp>
#include <fmod.hpp>

#include "../../foryou/services/ForYouEngine.hpp"
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
    void loadLeaderboard(std::string type);
    void createList(std::string type);
    void onViewLevel(cocos2d::CCObject* sender);
    
    // ForYou tab
    void loadForYou();
    void fireNextForYouQuery();
    void createForYouList();
    void onForYouPlayLevel(cocos2d::CCObject* sender);
    void onForYouRefresh(cocos2d::CCObject* sender);
    void onForYouPreferences(cocos2d::CCObject* sender);
    
    // LevelManagerDelegate
    void loadLevelsFinished(cocos2d::CCArray* levels, char const* key) override;
    void loadLevelsFailed(char const* key) override;
    void setupPageInfo(gd::string, char const*) override;
    
    // actualizar info sin recrear la lista
    void updateLevelInfo();
    void checkLoadingComplete();
    
    // particulas tematicas
    void createThemeParticles();
    void spawnThemeParticle(float dt);
    void clearParticles();
    
    // historial
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

    // dedicated request generation guards for leaderboard async callbacks
    uint32_t m_requestGeneration = 0;
    uint32_t m_pendingLevelGeneration = 0;

    // ForYou state
    bool m_forYouActive = false;
    std::vector<geode::Ref<GJGameLevel>> m_forYouResults;
    std::vector<paimon::foryou::ForYouQuery> m_forYouQueryQueue;
    int m_forYouQueryIndex = 0;
    bool m_forYouTagsPromptShown = false;

    cocos2d::CCSprite* m_bgSprite = nullptr;
    cocos2d::CCLayerColor* m_bgOverlay = nullptr;
    float m_blurTime = 0.f;
    BackTarget m_backTarget = BackTarget::CreatorLayer;

    // flags de carga (spinner hasta que ambos esten listos)
    bool m_dataLoaded = false;
    bool m_thumbLoaded = false;
    bool m_listCreated = false;
    
    // particulas
    cocos2d::CCNode* m_particleContainer = nullptr;
    cocos2d::ccColor3B m_themeColorA = {255, 200, 50};
    cocos2d::ccColor3B m_themeColorB = {255, 150, 30};

    // FMOD musica con efecto cueva
    FMOD::Channel* m_levelMusicChannel = nullptr;
    FMOD::Sound* m_levelMusicSound = nullptr;
    FMOD::ChannelGroup* m_levelAudioGroup = nullptr;
    FMOD::DSP* m_lowpassDSP = nullptr;
    FMOD::DSP* m_reverbDSP = nullptr;
    bool m_musicPlaying = false;
    bool m_leavingForGood = false;  // true si estamos saliendo del layer (back)
    bool m_goingToHistory = false;  // true si vamos al historial (no pausar cueva)
    
    static constexpr int AUDIO_FADE_STEPS = 15;
    static constexpr float AUDIO_FADE_MS = 500.f;
    bool m_isFadingCaveIn = false;
    bool m_isFadingCaveOut = false;
    int m_lifecycleToken = 0;
    
    // audio-reactive visuals
    FMOD::DSP* m_fftDSP = nullptr;
    float m_beatPulse = 0.f;        // 0-1 current beat intensity
    float m_prevBassLevel = 0.f;    // previous frame bass for beat detection
    float m_glowPulse = 0.f;        // smooth glow value
    float m_bgPulse = 0.f;          // bg brightness pulse
    float m_particleBoost = 0.f;    // particle spawn boost
    cocos2d::CCLayerColor* m_glowOverlay = nullptr;
    cocos2d::CCLayerColor* m_beatFlash = nullptr;
    float m_audioReactTime = 0.f;
    void updateAudioReactive(float dt);
    float getAudioBassLevel();
    
    void startCaveMusic();         // busca cancion, crea FMOD sound/channel, fade-in con cave DSPs
    void fadeOutCaveMusic();       // fade-out suave y luego libera
    void killCaveMusic();          // parar inmediatamente (cleanup)
    void applyCaveEffect();
    void removeCaveEffect();
    void executeCaveFade(int step, int totalSteps, float from, float to, bool fadeOut);
    
    void fadeOutMenuMusic();       // fade-out suave de BG al entrar
    void fadeInMenuMusic();        // fade-in suave de BG al salir
    void executeMenuFade(int step, int totalSteps, float from, float to);
    void ensureBgSilenced();
    void delaySilenceBg(float dt);
    void delaySilenceBg2(float dt);

    // tags para labels actualizables
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
