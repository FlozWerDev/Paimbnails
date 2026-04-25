#pragma once
#include <Geode/Geode.hpp>
#include <filesystem>
#include <algorithm>
#include <vector>
#include <string>
#include <set>
#include <functional>

// ────────────────────────────────────────────────────────────
// PetConfig: all pet settings, serializable to JSON
// ────────────────────────────────────────────────────────────

// Supported layer names for pet visibility
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

// Pet icon state types
enum class PetIconState : int {
    Default = 0,   // selectedImage (fallback for all)
    Idle    = 1,   // standing still
    Walk    = 2,   // moving
    Sleep   = 3,   // idle for too long
    React   = 4,   // reacting to game event
};

// Particle effect types
enum class PetParticleType : int {
    Sparkles  = 0,
    Hearts    = 1,
    Stars     = 2,
    Snowflakes = 3,
    Bubbles   = 4,
};

struct PetConfig {
    bool enabled = false;
    float scale = 0.5f;          // 0.1 – 3.0
    float sensitivity = 0.12f;   // 0.01 – 1.0  (lerp factor per frame)
    int   opacity = 220;         // 0 – 255
    float bounceHeight = 4.f;    // 0 – 20  pixels
    float bounceSpeed = 3.f;     // 0.5 – 10  cycles/sec
    float rotationDamping = 0.3f;// 0 – 1
    float maxTilt = 15.f;        // 0 – 45 degrees
    bool  flipOnDirection = true;
    bool  showTrail = false;
    float trailLength = 30.f;    // 5 – 100
    float trailWidth = 6.f;      // 1 – 20
    bool  idleAnimation = true;
    bool  bounce = true;
    float idleBreathScale = 0.04f; // 0 – 0.15
    float idleBreathSpeed = 1.5f;  // 0.5 – 5
    std::string selectedImage;     // filename in gallery dir (empty = none)

    // squish on land
    bool squishOnLand = true;
    float squishAmount = 0.15f;  // 0 – 0.5

    // offset from cursor
    float offsetX = 0.f;        // -50 – 50
    float offsetY = 25.f;       // -50 – 100  (default: above cursor)

    bool allLayers = true;
    bool showInGameplay = true;

    // layer visibility (all selected = visible everywhere)
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

    // ── Pet Icon States ──
    // Separate sprites per state; if empty, falls back to selectedImage
    std::string idleImage;       // idle state sprite
    std::string walkImage;       // walking state sprite
    std::string sleepImage;      // sleeping state sprite
    std::string reactImage;      // reaction sprite (game events)

    // ── Pet Shadow ──
    bool  showShadow = true;
    float shadowOffsetX = 3.f;   // -20 – 20
    float shadowOffsetY = -5.f;  // -20 – 20
    int   shadowOpacity = 60;    // 0 – 200
    float shadowScale = 1.1f;   // 0.5 – 2.0

    // ── Pet Particles ──
    bool  showParticles = false;
    int   particleType = 0;      // PetParticleType as int
    float particleRate = 5.f;    // 1 – 30 particles/sec
    cocos2d::ccColor3B particleColor = {255, 255, 255};
    float particleSize = 3.f;    // 1 – 10
    float particleGravity = -15.f; // -50 – 50
    float particleLifetime = 1.5f; // 0.5 – 5

    // ── Pet Speech Bubbles ──
    bool  enableSpeech = false;
    float speechInterval = 30.f;   // 5 – 120 seconds between random idle speech
    float speechDuration = 3.f;    // 1 – 10 seconds bubble visible
    float speechBubbleScale = 0.5f;// 0.2 – 1.0
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

    // ── Pet Sleep Mode ──
    bool  enableSleep = true;
    float sleepAfterSeconds = 60.f; // 10 – 300 seconds of idle before sleeping
    float sleepBobAmount = 3.f;     // 0 – 10 pixels of Zzz bob

    // ── Pet Click Interaction ──
    bool  enableClickInteraction = true;
    float clickReactionDuration = 1.5f; // 0.5 – 5 seconds
    float clickJumpHeight = 20.f;       // 5 – 50 pixels jump on click
    std::vector<std::string> clickMessages = {
        "Hey!", "That tickles!", "Stop it!", "Hehe~", "What?",
        "Paimon is not a toy!", "Excuse me?!", "Boop!"
    };

    // ── Game Event Reactions ──
    bool  reactToLevelComplete = true;
    bool  reactToDeath = true;
    bool  reactToPracticeExit = true;
    float reactionDuration = 2.f;    // 0.5 – 5 seconds
    float reactionJumpHeight = 30.f; // 5 – 60 pixels
    float reactionSpinSpeed = 360.f; // 90 – 720 degrees/sec
};

// ────────────────────────────────────────────────────────────
// PetManager: singleton
// ────────────────────────────────────────────────────────────

class PetManager {
public:
    static PetManager& get();

    // lifecycle
    void init();
    void update(float dt);
    void attachToScene(cocos2d::CCScene* scene);
    void detachFromScene();
    void releaseSharedResources();

    // config
    PetConfig& config() { return m_config; }
    void loadConfig();
    void saveConfig();
    void applyConfigLive();   // push current config to sprite

    // image
    void setImage(std::string const& galleryFilename);
    void reloadSprite();

    // gallery
    std::vector<std::string> getGalleryImages() const;
    std::string addToGallery(std::filesystem::path const& srcPath);   // returns filename
    void removeFromGallery(std::string const& filename);
    void removeAllFromGallery();
    int cleanupInvalidImages();  // removes non-image files, returns count removed
    std::filesystem::path galleryDir() const;
    cocos2d::CCTexture2D* loadGalleryThumb(std::string const& filename) const;

    // state (read-only)
    bool isAttached() const { return m_petNode != nullptr && m_petNode->getParent() != nullptr; }
    bool isWalking() const { return m_walking; }
    bool isSleeping() const { return m_sleeping; }
    bool isReacting() const { return m_reactionTimer > 0.f; }
    PetIconState currentIconState() const { return m_iconState; }
    bool shouldShowOnCurrentScene() const;

    // icon states
    void setIconStateImage(PetIconState state, std::string const& galleryFilename);
    std::string getIconStateImage(PetIconState state) const;
    void switchToIconState(PetIconState state);

    // game event reactions
    void triggerReaction(std::string const& eventType);  // "level_complete", "death", "practice_exit"
    void triggerClickReaction(cocos2d::CCPoint clickPos);

    // speech bubbles
    void showSpeechBubble(std::string const& message);
    void showRandomSpeech(std::vector<std::string> const& messages);
    void hideSpeechBubble();

private:
    PetManager() = default;

    PetConfig m_config;

    // scene node tree: hostNode -> petSprite / trailNode / shadow / particles / speech
    geode::Ref<cocos2d::CCNode> m_petNode = nullptr;        // host node added to scene
    cocos2d::CCSprite* m_petSprite = nullptr;     // the actual image (child of m_petNode)
    cocos2d::CCMotionStreak* m_trail = nullptr;   // child of m_petNode
    cocos2d::CCSprite* m_shadowSprite = nullptr;  // shadow beneath pet
    cocos2d::CCNode* m_particleNode = nullptr;    // particle system container
    cocos2d::CCNode* m_speechNode = nullptr;      // speech bubble container
    cocos2d::CCLabelBMFont* m_speechLabel = nullptr; // text in speech bubble
    cocos2d::CCDrawNode* m_speechBg = nullptr;    // speech bubble background
    cocos2d::CCNode* m_sleepZzz = nullptr;        // Zzz indicator when sleeping

    // physics state
    cocos2d::CCPoint m_currentPos;
    cocos2d::CCPoint m_targetPos;
    cocos2d::CCPoint m_velocity;
    float m_idleTimer = 0.f;
    float m_walkTimer = 0.f;
    bool  m_walking = false;
    bool  m_facingRight = true;
    float m_currentTilt = 0.f;
    float m_landSquishTimer = 0.f;  // >0 while squishing
    bool  m_wasWalking = false;

    // icon state
    PetIconState m_iconState = PetIconState::Default;
    float m_idleDuration = 0.f;     // accumulated idle time (for sleep detection)

    // sleep state
    bool  m_sleeping = false;
    float m_sleepZzzTimer = 0.f;

    // reaction state
    float m_reactionTimer = 0.f;    // >0 while reacting
    float m_reactionJumpVel = 0.f;  // vertical velocity for reaction jump
    float m_reactionSpinVel = 0.f;  // spin speed for reaction
    float m_reactionBaseY = 0.f;    // Y position before reaction jump

    // click reaction
    float m_clickReactionTimer = 0.f;
    float m_clickJumpVel = 0.f;
    float m_clickBaseY = 0.f;

    // speech bubble state
    float m_speechTimer = 0.f;      // >0 while speech bubble visible
    float m_speechIdleAccum = 0.f;  // accumulator for random idle speech

    // particle state
    float m_particleAccum = 0.f;    // accumulator for particle emission

    // click detection state
    bool  m_mouseWasDown = false;   // tracks previous frame mouse state for click detection

    // helpers
    std::filesystem::path configPath() const;
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

