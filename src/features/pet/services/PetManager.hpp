#pragma once
#include <Geode/Geode.hpp>
#include <fmod.hpp>
#include <array>
#include <filesystem>
#include <algorithm>
#include <vector>
#include <string>
#include <set>
#include <unordered_map>
#include <functional>

inline std::vector<std::string> PET_LAYER_OPTIONS = {
    "MenuLayer", "LevelBrowserLayer", "LevelInfoLayer",
    "CreatorLayer", "LevelSearchLayer", "GauntletSelectLayer",
    "ProfilePage", "LevelListLayer", "LevelEditorLayer",
    "PlayLayer", "PauseLayer", "GJGarageLayer",
    "GJShopLayer", "SecretLayer", "TreasureRoomLayer",
    "ChallengesLayer", "LevelAreaLayer", "DailyLevelLayer",
    "WeeklyLevelLayer", "GauntletLayer", "LeaderboardLayer",
    "LevelLeaderboard", "CommentListLayer", "InfoLayer",
    "SongInfoLayer", "CustomSongLayer", "GJMoreGamesLayer",
    "GJOptionsLayer", "OptionsLayer", "MoreOptionsLayer",
    "AccountLayer", "AccountLoginLayer", "GJAccountSettingsLayer",
    "GJScoreLayer", "EndLevelLayer", "LevelCompleteLayer",
    "LevelFailedLayer", "RetryLayer", "FLAlertLayer",
    "GJDropDownLayer", "SelectItemLayer", "GJLocalLevelSelector",
    "TowerSelectorLayer", "GJPathsLayer", "GJPathPage",
    "GJMapPackLayer", "PromoArtLayer", "SupportLayer",
    "CreditsLayer", "GJChallengeLayer", "GJRewardLayer",
    "LevelSelectLayer", "GJFriendsLayer", "GJScoresLayer",
    "LeaderboardsLayer", "GJCommentListLayer", "FRequestProfilePage",
    "GJLevelScoreCell"
};

inline std::vector<std::string> PET_GAMEPLAY_LAYER_OPTIONS = {
    "PlayLayer",
    "PauseLayer",
    "EndLevelLayer",
    "LevelCompleteLayer",
    "LevelFailedLayer",
    "RetryLayer"
};

inline bool isPetGameplayLayer(std::string const& layerName) {
    return std::find(
        PET_GAMEPLAY_LAYER_OPTIONS.begin(),
        PET_GAMEPLAY_LAYER_OPTIONS.end(),
        layerName
    ) != PET_GAMEPLAY_LAYER_OPTIONS.end();
}

enum class PetIconState : int {
    Default = 0,   // selectedimage (fallback for all)
    Idle    = 1,   // standing still
    Walk    = 2,   // moving
    Sleep   = 3,   // idle for too long
    React   = 4,   // reacting to game event
};

enum class PetParticleType : int {
    Sparkles  = 0,
    Hearts    = 1,
    Stars     = 2,
    Snowflakes = 3,
    Bubbles   = 4,
};

enum class PetDialogueCategory : int {
    Idle = 0,
    Click = 1,
    LevelComplete = 2,
    Death = 3,
    Petting = 4,
    Gesture = 5,
    Count = 6,
};

enum class PetGesture : int {
    Happy = 0,
    Spin = 1,
    Wave = 2,
    Surprised = 3,
    Love = 4,
    Celebrate = 5,
    Count = 6,
};

struct PetDialogueEntry {
    std::string text;
    float weight = 10.f;      // relative probability weight (1..100)
    std::string audioFile;    // optional per-dialogue audio override ("gd:<sfx>" or filename in pet_audio)
};

inline std::vector<PetDialogueEntry> makeDefaultPetDialogues(PetDialogueCategory cat) {
    switch (cat) {
        case PetDialogueCategory::Idle:
            return {
                {"Hmm...", 15.f, ""},
                {"I'm bored!", 12.f, ""},
                {"Watcha doing?", 12.f, ""},
                {"Hehe~", 12.f, ""},
                {"La la la~", 10.f, ""},
                {"So sleepy...", 10.f, ""},
                {"Any levels to play?", 10.f, ""},
                {"Paimon is hungry!", 8.f, ""},
            };
        case PetDialogueCategory::Click:
            return {
                {"Hey!", 15.f, ""},
                {"That tickles!", 15.f, ""},
                {"Stop it!", 10.f, ""},
                {"Hehe~", 15.f, ""},
                {"What?", 10.f, ""},
                {"Paimon is not a toy!", 8.f, ""},
                {"Excuse me?!", 8.f, ""},
                {"Boop!", 15.f, ""},
            };
        case PetDialogueCategory::LevelComplete:
            return {
                {"Amazing!", 15.f, ""},
                {"You did it!", 15.f, ""},
                {"Woohoo!", 12.f, ""},
                {"So cool!", 10.f, ""},
                {"NICE!", 12.f, ""},
                {"Paimon is impressed!", 10.f, ""},
                {"GG!", 16.f, ""},
                {"That was awesome!", 10.f, ""},
            };
        case PetDialogueCategory::Death:
            return {
                {"Ouch!", 15.f, ""},
                {"You'll get it!", 15.f, ""},
                {"Try again!", 14.f, ""},
                {"Don't give up!", 15.f, ""},
                {"So close!", 12.f, ""},
                {"Paimon believes in you!", 10.f, ""},
                {"Oops!", 14.f, ""},
            };
        case PetDialogueCategory::Petting:
            return {
                {"Purr~", 16.f, ""},
                {"Hehe, more!", 15.f, ""},
                {"So cozy~", 14.f, ""},
                {"Thank you!", 12.f, ""},
                {"Best friend!", 10.f, ""},
            };
        case PetDialogueCategory::Gesture:
            return {
                {"Ta-da!", 15.f, ""},
                {"Look at me!", 14.f, ""},
                {"Yay~", 14.f, ""},
                {"Woohoo!", 12.f, ""},
            };
        default:
            return {};
    }
}

inline std::vector<std::string> makeDefaultGestureAudioPool(PetGesture gesture) {
    switch (gesture) {
        case PetGesture::Happy:     return {"gd:gold01.ogg", "gd:gold02.ogg"};
        case PetGesture::Spin:      return {"gd:crystal01.ogg"};
        case PetGesture::Wave:      return {"gd:chestClick.ogg"};
        case PetGesture::Surprised: return {"gd:playSound_01.ogg"};
        case PetGesture::Love:      return {"gd:secretKey.ogg"};
        case PetGesture::Celebrate: return {"gd:achievement_01.ogg", "gd:chestOpen01.ogg"};
        default:                    return {};
    }
}

inline char const* petGestureName(PetGesture gesture) {
    switch (gesture) {
        case PetGesture::Happy:     return "Feliz";
        case PetGesture::Spin:      return "Giro";
        case PetGesture::Wave:      return "Saludo";
        case PetGesture::Surprised: return "Sorpresa";
        case PetGesture::Love:      return "Carino";
        case PetGesture::Celebrate: return "Celebrar";
        default:                    return "Gesto";
    }
}

inline char const* petGestureKey(PetGesture gesture) {
    switch (gesture) {
        case PetGesture::Happy:     return "happy";
        case PetGesture::Spin:      return "spin";
        case PetGesture::Wave:      return "wave";
        case PetGesture::Surprised: return "surprised";
        case PetGesture::Love:      return "love";
        case PetGesture::Celebrate: return "celebrate";
        default:                    return "happy";
    }
}

inline char const* petDialogueCategoryName(PetDialogueCategory cat) {
    switch (cat) {
        case PetDialogueCategory::Idle:          return "Reposo";
        case PetDialogueCategory::Click:         return "Click";
        case PetDialogueCategory::LevelComplete: return "Nivel";
        case PetDialogueCategory::Death:         return "Muerte";
        case PetDialogueCategory::Petting:       return "Caricias";
        case PetDialogueCategory::Gesture:       return "Gestos";
        default:                                 return "Dialogo";
    }
}

struct PetConfig {
    bool enabled = false;
    float scale = 0.5f;
    float sensitivity = 0.12f;
    int   opacity = 220;
    float bounceHeight = 4.f;
    float bounceSpeed = 3.f;
    float rotationDamping = 0.3f;
    float maxTilt = 15.f;
    bool  flipOnDirection = true;
    bool  showTrail = false;
    float trailLength = 30.f;
    float trailWidth = 6.f;
    bool  idleAnimation = true;
    bool  bounce = true;
    float idleBreathScale = 0.04f;
    float idleBreathSpeed = 1.5f;
    std::string selectedImage; // filename in the gallery directory

    bool squishOnLand = true;
    float squishAmount = 0.15f;

    float offsetX = 0.f;
    float offsetY = 25.f; // default: above the cursor

    bool allLayers = true;
    bool showInGameplay = true;

    // empty with alllayers off hides the pet everywhere.
    std::set<std::string> visibleLayers{
        PET_LAYER_OPTIONS.begin(), PET_LAYER_OPTIONS.end()};

    // empty state images fall back to selectedimage.
    std::string idleImage;
    std::string walkImage;
    std::string sleepImage;
    std::string reactImage;

    bool  showShadow = true;
    float shadowOffsetX = 3.f;
    float shadowOffsetY = -5.f;
    int   shadowOpacity = 60;
    float shadowScale = 1.1f;

    bool  showParticles = false;
    int   particleType = 0;
    float particleRate = 5.f;
    cocos2d::ccColor3B particleColor = {255, 255, 255};
    float particleSize = 3.f;
    float particleGravity = -15.f;
    float particleLifetime = 1.5f;

    bool  enableSpeech = false;
    float speechInterval = 30.f;
    float speechDuration = 3.f;
    float speechBubbleScale = 0.5f;
    bool  speechTypewriter = true;
    cocos2d::ccColor3B speechBgColor = {14, 18, 28};
    cocos2d::ccColor3B speechBorderColor = {255, 255, 255};
    cocos2d::ccColor3B speechTextColor = {255, 255, 255};

    std::vector<PetDialogueEntry> idleMessages = makeDefaultPetDialogues(PetDialogueCategory::Idle);
    std::vector<PetDialogueEntry> levelCompleteMessages = makeDefaultPetDialogues(PetDialogueCategory::LevelComplete);
    std::vector<PetDialogueEntry> deathMessages = makeDefaultPetDialogues(PetDialogueCategory::Death);
    std::vector<PetDialogueEntry> clickMessages = makeDefaultPetDialogues(PetDialogueCategory::Click);
    std::vector<PetDialogueEntry> pettingMessages = makeDefaultPetDialogues(PetDialogueCategory::Petting);
    std::vector<PetDialogueEntry> gestureMessages = makeDefaultPetDialogues(PetDialogueCategory::Gesture);

    // Audio settings (click pool + per-gesture default/custom pools)
    bool  enableAudio = true;
    float audioVolume = 0.8f;
    float audioPitchRandom = 0.08f;
    std::vector<std::string> clickAudioPool = {"gd:chestClick.ogg", "gd:gold01.ogg"};
    std::array<std::vector<std::string>, static_cast<size_t>(PetGesture::Count)> gestureAudioPools = {
        makeDefaultGestureAudioPool(PetGesture::Happy),
        makeDefaultGestureAudioPool(PetGesture::Spin),
        makeDefaultGestureAudioPool(PetGesture::Wave),
        makeDefaultGestureAudioPool(PetGesture::Surprised),
        makeDefaultGestureAudioPool(PetGesture::Love),
        makeDefaultGestureAudioPool(PetGesture::Celebrate),
    };

    bool  enableSleep = true;
    float sleepAfterSeconds = 60.f;
    float sleepBobAmount = 3.f;

    bool  enableClickInteraction = true;
    float clickReactionDuration = 1.5f;
    float clickJumpHeight = 20.f;

    // Extra interactive features
    bool  enablePetting = true;           // rub cursor over pet for hearts + affection
    bool  enableAutoGestures = true;      // occasional spontaneous gestures while idle
    float autoGestureInterval = 45.f;
    bool  reactToMusic = false;           // bounce subtly to PaimonAudio beat pulse
    float musicBounceStrength = 0.14f;
    bool  wanderMode = false;             // roam freely on screen instead of sticking to cursor

    // Persistent pet companionship stats
    int   affectionXp = 0;
    int   totalClicks = 0;
    int   totalPets = 0;

    bool  reactToLevelComplete = true;
    bool  reactToDeath = true;
    bool  reactToPracticeExit = true;
    float reactionDuration = 2.f;
    float reactionJumpHeight = 30.f;
    float reactionSpinSpeed = 360.f;

    std::vector<PetDialogueEntry>& dialoguesFor(PetDialogueCategory cat) {
        switch (cat) {
            case PetDialogueCategory::Idle:          return idleMessages;
            case PetDialogueCategory::Click:         return clickMessages;
            case PetDialogueCategory::LevelComplete: return levelCompleteMessages;
            case PetDialogueCategory::Death:         return deathMessages;
            case PetDialogueCategory::Petting:       return pettingMessages;
            case PetDialogueCategory::Gesture:       return gestureMessages;
            default:                                 return idleMessages;
        }
    }

    std::vector<PetDialogueEntry> const& dialoguesFor(PetDialogueCategory cat) const {
        switch (cat) {
            case PetDialogueCategory::Idle:          return idleMessages;
            case PetDialogueCategory::Click:         return clickMessages;
            case PetDialogueCategory::LevelComplete: return levelCompleteMessages;
            case PetDialogueCategory::Death:         return deathMessages;
            case PetDialogueCategory::Petting:       return pettingMessages;
            case PetDialogueCategory::Gesture:       return gestureMessages;
            default:                                 return idleMessages;
        }
    }

    std::vector<std::string>& audioPoolForGesture(PetGesture g) {
        size_t idx = std::min(static_cast<size_t>(g), gestureAudioPools.size() - 1);
        return gestureAudioPools[idx];
    }

    std::vector<std::string> const& audioPoolForGesture(PetGesture g) const {
        size_t idx = std::min(static_cast<size_t>(g), gestureAudioPools.size() - 1);
        return gestureAudioPools[idx];
    }
};

class PetManager {
public:
    static PetManager& get();

    void init();
    void update(float dt);
    void attachToScene(cocos2d::CCScene* scene);
    void detachFromScene();
    void refreshVisibility();
    void releaseSharedResources();
    // release gl-owned resources before gamemanager::reloadall recreates the context.
    void onGLContextReload();

    PetConfig& config() { return m_config; }
    void loadConfig();
    // apply an already parsed snapshot on the main thread.
    void loadConfig(matjson::Value const& config);
    void saveConfig();
    void applyConfigLive();

    void setImage(std::string const& galleryFilename);
    void reloadSprite();

    std::vector<std::string> getGalleryImages() const;
    std::string addToGallery(std::filesystem::path const& srcPath);
    void removeFromGallery(std::string const& filename);
    void removeAllFromGallery();
    int cleanupInvalidImages();
    std::filesystem::path galleryDir() const;
    std::filesystem::path audioDir() const;
    std::string addAudioToLibrary(std::filesystem::path const& srcPath);
    std::vector<std::string> getAudioLibraryFiles() const;
    bool playAudioEntry(std::string const& entry, bool applyPitchJitter = true);
    bool playRandomFromPool(std::vector<std::string> const& pool, std::string const& poolKey);
    void stopActiveAudio();
    cocos2d::CCTexture2D* loadGalleryThumb(std::string const& filename) const;

    bool isAttached() const { return m_petNode != nullptr && m_petNode->getParent() != nullptr; }
    bool isWalking() const { return m_walking; }
    bool isSleeping() const { return m_sleeping; }
    bool isReacting() const { return m_reactionTimer > 0.f || m_gestureTimer > 0.f; }
    PetIconState currentIconState() const { return m_iconState; }
    bool shouldShowOnCurrentScene() const;

    int affectionLevel() const;
    int affectionLevelProgressXp() const;
    int affectionLevelNeededXp() const;

    void setIconStateImage(PetIconState state, std::string const& galleryFilename);
    std::string getIconStateImage(PetIconState state) const;

    void triggerReaction(std::string const& eventType);  // "level_complete", "death", "practice_exit"
    void triggerClickReaction(cocos2d::CCPoint clickPos);
    void registerClick(cocos2d::CCPoint clickPos);
    void triggerGesture(PetGesture gesture, bool showSpeech = true, bool playAudio = true);

    void showSpeechBubble(std::string const& message, bool bypassSpeechToggle = false);
    // Returns index chosen (-1 if none) and optionally plays the dialogue's override audio.
    int showWeightedSpeech(
        PetDialogueCategory category,
        bool* outPlayedOverrideAudio = nullptr,
        bool bypassSpeechToggle = false
    );
    void hideSpeechBubble();

private:
    PetManager() = default;
    ~PetManager();

    PetConfig m_config;

    geode::Ref<cocos2d::CCNode> m_petNode = nullptr;
    cocos2d::CCSprite* m_petSprite = nullptr;
    cocos2d::CCMotionStreak* m_trail = nullptr;
    cocos2d::CCSprite* m_shadowSprite = nullptr;
    cocos2d::CCNode* m_particleNode = nullptr;
    cocos2d::CCNode* m_speechNode = nullptr;
    cocos2d::CCLabelBMFont* m_speechLabel = nullptr;
    cocos2d::CCDrawNode* m_speechBg = nullptr;
    cocos2d::CCNode* m_sleepZzz = nullptr;

    cocos2d::CCPoint m_currentPos;
    cocos2d::CCPoint m_targetPos;
    cocos2d::CCPoint m_velocity;
    float m_idleTimer = 0.f;
    float m_walkTimer = 0.f;
    bool  m_walking = false;
    bool  m_facingRight = true;
    float m_currentTilt = 0.f;
    float m_landSquishTimer = 0.f;
    bool  m_wasWalking = false;

    PetIconState m_iconState = PetIconState::Default;
    float m_idleDuration = 0.f;

    bool  m_sleeping = false;
    float m_sleepZzzTimer = 0.f;

    float m_reactionTimer = 0.f;
    float m_reactionJumpVel = 0.f;
    float m_reactionSpinVel = 0.f;
    float m_reactionJumpOffset = 0.f;

    float m_clickReactionTimer = 0.f;
    float m_clickCooldown = 0.f;
    float m_clickJumpVel = 0.f;
    float m_clickJumpOffset = 0.f;

    float m_speechTimer = 0.f;
    float m_speechIdleAccum = 0.f;
    std::string m_speechFullText;
    float m_speechRevealProgress = 0.f;
    size_t m_speechVisibleChars = 0;

    float m_particleAccum = 0.f;

    // Gestures, petting, wander & audio state
    PetGesture m_activeGesture = PetGesture::Happy;
    float m_gestureTimer = 0.f;
    float m_gestureDuration = 0.f;
    float m_autoGestureAccum = 0.f;

    cocos2d::CCPoint m_lastPettingMousePos{-9999.f, -9999.f};
    float m_pettingRubAccum = 0.f;
    float m_pettingCooldown = 0.f;
    float m_pettingSquishTimer = 0.f;

    cocos2d::CCPoint m_wanderTarget{0.f, 0.f};
    float m_wanderHoldTimer = 0.f;
    bool m_wanderInitialized = false;

    float m_audioCooldown = 0.f;
    FMOD::Sound* m_activeSound = nullptr;
    FMOD::Channel* m_activeChannel = nullptr;
    std::unordered_map<int, int> m_lastDialogueIndex;
    std::unordered_map<std::string, int> m_lastAudioIndex;
    int m_unsavedStatAdds = 0;

    std::unordered_map<std::string, geode::Ref<cocos2d::CCTexture2D>> m_staticTextureCache;
    std::string m_activeImageFile;
    std::vector<cocos2d::CCPoint> m_pendingClicks;

    std::filesystem::path configPath() const;
    std::string resolveImageFileForState(PetIconState state) const;
    std::string resolveCurrentImageFile() const;
    void purgeCachedImage(std::string const& filename);
    cocos2d::CCSprite* createSpriteForImage(std::string const& imageFile);
    bool switchSpriteToImage(std::string const& imageFile);
    void createPetSprite();
    void createShadow();
    void updateShadow();
    void createParticleNode();
    void updateParticles(float dt);
    void emitParticle();
    void emitBurstParticles(PetParticleType type, cocos2d::ccColor4F const& col, int count);
    // normalized particle tint; final attach shared by all particle shapes.
    cocos2d::ccColor4F particleColor() const;
    void attachParticle(cocos2d::CCDrawNode* draw, cocos2d::CCPoint const& pos, float vx, float vy);
    void createSpeechBubbleNode();
    void updateSpeechBubble(float dt);
    void redrawSpeechBackground();
    void createSleepZzz();
    void updateSleepZzz(float dt);
    void updateIconState();
    void updateReaction(float dt);
    void updateClickReaction(float dt);
    void updateGesture(float dt);
    void updatePetting(float dt, cocos2d::CCPoint const& mousePos);
    void addAffectionXp(int amount);
    void updateTrail();
};
