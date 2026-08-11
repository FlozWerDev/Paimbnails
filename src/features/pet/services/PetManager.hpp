#pragma once
#include <Geode/Geode.hpp>
#include <filesystem>
#include <algorithm>
#include <vector>
#include <string>
#include <set>
#include <unordered_map>
#include <functional>

// PetConfig stores the serializable pet settings.
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
    Default = 0,   // selectedImage (fallback for all)
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

    // Empty means the pet follows the global layer setting.
    std::set<std::string> visibleLayers = {
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

    // Empty state images fall back to selectedImage.
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
    std::vector<std::string> idleMessages = {
        "Hmm...", "I'm bored!", "Watcha doing?", "Hehe~", "La la la~",
        "So sleepy...", "Any levels to play?", "Paimon is hungry!"
    };
    std::vector<std::string> levelCompleteMessages = {
        "Amazing!", "You did it!", "Woohoo!", "So cool!", "NICE!",
        "Paimon is impressed!", "GG!", "That was awesome!"
    };
    std::vector<std::string> deathMessages = {
        "Ouch!", "You'll get it!", "Try again!", "Don't give up!",
        "So close!", "Paimon believes in you!", "Oops!"
    };

    bool  enableSleep = true;
    float sleepAfterSeconds = 60.f;
    float sleepBobAmount = 3.f;

    bool  enableClickInteraction = true;
    float clickReactionDuration = 1.5f;
    float clickJumpHeight = 20.f;
    std::vector<std::string> clickMessages = {
        "Hey!", "That tickles!", "Stop it!", "Hehe~", "What?",
        "Paimon is not a toy!", "Excuse me?!", "Boop!"
    };

    bool  reactToLevelComplete = true;
    bool  reactToDeath = true;
    bool  reactToPracticeExit = true;
    float reactionDuration = 2.f;
    float reactionJumpHeight = 30.f;
    float reactionSpinSpeed = 360.f;
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
    // Release GL-owned resources before GameManager::reloadAll recreates the context.
    void onGLContextReload();

    PetConfig& config() { return m_config; }
    void loadConfig();
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
    cocos2d::CCTexture2D* loadGalleryThumb(std::string const& filename) const;

    bool isAttached() const { return m_petNode != nullptr && m_petNode->getParent() != nullptr; }
    bool isWalking() const { return m_walking; }
    bool isSleeping() const { return m_sleeping; }
    bool isReacting() const { return m_reactionTimer > 0.f; }
    PetIconState currentIconState() const { return m_iconState; }
    bool shouldShowOnCurrentScene() const;

    void setIconStateImage(PetIconState state, std::string const& galleryFilename);
    std::string getIconStateImage(PetIconState state) const;
    void switchToIconState(PetIconState state);

    void triggerReaction(std::string const& eventType);  // "level_complete", "death", "practice_exit"
    void triggerClickReaction(cocos2d::CCPoint clickPos);
    void registerClick(cocos2d::CCPoint clickPos);

    void showSpeechBubble(std::string const& message);
    void showRandomSpeech(std::vector<std::string> const& messages);
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
    float m_reactionBaseY = 0.f;

    float m_clickReactionTimer = 0.f;
    float m_clickJumpVel = 0.f;
    float m_clickBaseY = 0.f;

    float m_speechTimer = 0.f;
    float m_speechIdleAccum = 0.f;

    float m_particleAccum = 0.f;

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
    void createSpeechBubbleNode();
    void updateSpeechBubble(float dt);
    void createSleepZzz();
    void updateSleepZzz(float dt);
    void updateIconState();
    void updateReaction(float dt);
    void updateClickReaction(float dt);
    void updateIdleAnimation(float dt);
    void updateWalkAnimation(float dt);
    void updateTrail();
};
