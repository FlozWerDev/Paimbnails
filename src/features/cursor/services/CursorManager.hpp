#pragma once
#include <Geode/Geode.hpp>
#include <filesystem>
#include <vector>
#include <string>
#include <set>

// Layer names where the custom cursor can be shown
inline std::vector<std::string> CURSOR_LAYER_OPTIONS = {
    "MenuLayer", "LevelBrowserLayer", "LevelInfoLayer",
    "CreatorLayer", "LevelSearchLayer", "GauntletSelectLayer",
    "ProfilePage", "LevelListLayer", "LevelEditorLayer"
};

inline constexpr float CURSOR_SCALE_MIN = 0.10f;
inline constexpr float CURSOR_SCALE_MAX = 3.0f;
inline constexpr float CURSOR_SCALE_DEFAULT = 0.30f;
inline constexpr float CURSOR_HOTSPOT_X = 0.f;
inline constexpr float CURSOR_HOTSPOT_Y = 0.92f;

struct CursorTrailPreset {
    const char* name;
    cocos2d::ccColor3B color;
    float length;   // CCMotionStreak fade time * 60
    float width;    // stroke width
    int fadeType;   // 0=linear, 1=sine, 2=none
    int opacity;    // 0-255
};

struct CursorConfig {
    bool enabled             = false;
    std::string idleImage    = "";      // filename in cursor gallery
    std::string moveImage    = "";      // filename in cursor gallery
    float scale              = CURSOR_SCALE_DEFAULT;   // 0.10 – 3.0
    int   opacity            = 255;    // 0 – 255

    // Trail
    bool  trailEnabled       = false;
    int   trailR             = 255;
    int   trailG             = 255;
    int   trailB             = 255;
    float trailLength        = 80.f;   // 5 – 300
    float trailWidth         = 4.f;    // 1 – 12
    int   trailFadeType      = 0;      // 0=linear 1=sine 2=none
    int   trailOpacity       = 200;    // 0 – 255
    int   trailPreset        = -1;     // -1=custom, 0-9=preset index

    // Visible layers (all selected = show everywhere, empty = hide everywhere)
    std::set<std::string> visibleLayers = {
        "MenuLayer", "LevelBrowserLayer", "LevelInfoLayer",
        "CreatorLayer", "LevelSearchLayer", "GauntletSelectLayer",
        "ProfilePage", "LevelListLayer", "LevelEditorLayer"
    };
};

// ────────────────────────────────────────────────────────────────────────
// CursorManager: singleton
// ────────────────────────────────────────────────────────────────────────

class CursorManager {
public:
    static CursorManager& get();

    // Lifecycle
    void init();
    void update(float dt);
    void attachToScene(cocos2d::CCScene* scene);
    void detachFromScene();
    void releaseSharedResources();

    // Config
    CursorConfig& config() { return m_config; }
    void loadConfig();
    void saveConfig();
    void applyConfigLive();     // push current config to live sprites

    // Sprites
    void setIdleImage(std::string const& filename);
    void setMoveImage(std::string const& filename);
    void reloadSprites();

    // Gallery (shared pool for idle/move slots)
    std::vector<std::string> getGalleryImages() const;
    std::string addToGallery(std::filesystem::path const& srcPath);
    void removeFromGallery(std::string const& filename);
    void removeAllFromGallery();
    int  cleanupInvalidImages();
    std::filesystem::path galleryDir() const;
    cocos2d::CCTexture2D* loadGalleryThumb(std::string const& filename) const;

    // State
    bool isAttached() const { return m_cursorNode && m_cursorNode->getParent(); }
    bool isAttachedToScene(cocos2d::CCScene* scene) const {
        return scene && m_cursorNode && m_cursorNode->getParent() == scene;
    }
    bool shouldShowOnCurrentScene() const;

    // 10 built-in trail presets
    static constexpr int TRAIL_PRESET_COUNT = 10;
    static const CursorTrailPreset TRAIL_PRESETS[TRAIL_PRESET_COUNT];

private:
    CursorManager() = default;

    CursorConfig m_config;

    geode::Ref<cocos2d::CCNode> m_cursorNode = nullptr;
    cocos2d::CCSprite*          m_idleSprite  = nullptr;
    cocos2d::CCSprite*          m_moveSprite  = nullptr;
    cocos2d::CCMotionStreak*    m_trail       = nullptr;

    cocos2d::CCPoint m_currentPos;
    cocos2d::CCPoint m_velocity;
    bool m_isMoving  = false;
    float m_moveTimer = 0.f;    // seconds since last significant movement
    bool m_systemCursorHidden = false;

    std::filesystem::path configPath() const;
    cocos2d::CCSprite* loadSprite(std::string const& filename);
    cocos2d::CCSprite* createFallbackSprite();
    bool sceneMatchesVisibleLayers(cocos2d::CCScene* scene) const;
    void syncSystemCursorVisibility(bool hideSystemCursor);
    void updateTrail();
};
