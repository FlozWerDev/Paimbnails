#pragma once
#include <Geode/Geode.hpp>
#include <filesystem>
#include <vector>
#include <string>
#include <set>
#include <map>

// Layer names where the custom cursor can be shown
inline std::vector<std::string> CURSOR_LAYER_OPTIONS = {
    "MenuLayer", "LevelBrowserLayer", "LevelInfoLayer",
    "CreatorLayer", "LevelSearchLayer", "GauntletSelectLayer",
    "ProfilePage", "LevelListLayer", "LevelEditorLayer"
};

inline constexpr float CURSOR_SCALE_MIN = 0.10f;
inline constexpr float CURSOR_SCALE_MAX = 3.0f;
inline constexpr float CURSOR_SCALE_DEFAULT = 0.30f;
// Hotspot of the original arrow cursor: top-left corner.
// (0,1) anchors the sprite by its top-left to the mouse point, keeping the
// click point accurate and scale-independent.
inline constexpr float CURSOR_HOTSPOT_X = 0.f;
inline constexpr float CURSOR_HOTSPOT_Y = 1.0f;

// Cursor states. Priority (high to low):
//   Click > Disabled > Text > Hover > Move > Idle.
// Any state without an assigned image falls back to Idle.
enum class CursorState {
    Idle     = 0,
    Move     = 1,
    Hover    = 2,   // over an interactive button (hand/link)
    Click    = 3,   // left mouse button held
    Text     = 4,   // over a text field (I-beam)
    Disabled = 5,   // over a disabled button (not-allowed)
};

inline constexpr int CURSOR_STATE_COUNT = 6;

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

    // One image (gallery filename) per state.
    std::string idleImage    = "";      // rest / fallback for all states
    std::string moveImage    = "";      // while the cursor is moving
    std::string hoverImage   = "";      // while hovering over a button
    std::string clickImage   = "";      // while left mouse button is held
    std::string textImage    = "";      // over a text field (I-beam)
    std::string disabledImage = "";     // over a disabled button

    float scale              = CURSOR_SCALE_DEFAULT;   // 0.10 – 3.0
    int   opacity            = 255;    // 0 – 255

    // Hover/click/text/disabled react to game context. Can be disabled
    // to keep classic Idle/Move behavior only.
    bool  hoverEnabled       = true;
    bool  clickEnabled       = true;
    bool  textEnabled        = true;
    bool  disabledEnabled    = true;

    // Follow delay (lerp smoothing)
    bool  followDelayEnabled = false;
    float followDelay        = 0.5f;   // 0.0 (instant) – 1.0 (very slow)

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

// CursorManager: singleton

class CursorManager {
public:
    static CursorManager& get();

    // Lifecycle
    void init();
    void update(float dt);
    void attachToOverlay();
    void detachFromScene();
    void releaseSharedResources();

    // Config
    CursorConfig& config() { return m_config; }
    void loadConfig();
    void saveConfig();
    void applyConfigLive();     // push current config to live sprites

    // Per-state image assignment
    std::string imageForState(CursorState state) const;
    void setImageForState(CursorState state, std::string const& filename);
    void setIdleImage(std::string const& filename)  { setImageForState(CursorState::Idle,  filename); }
    void setMoveImage(std::string const& filename)  { setImageForState(CursorState::Move,  filename); }
    void setHoverImage(std::string const& filename) { setImageForState(CursorState::Hover, filename); }
    void setClickImage(std::string const& filename) { setImageForState(CursorState::Click, filename); }
    void setTextImage(std::string const& filename)  { setImageForState(CursorState::Text,  filename); }
    void setDisabledImage(std::string const& filename) { setImageForState(CursorState::Disabled, filename); }
    void reloadSprites();

    // Mouse hold state — fed by the global MouseInputEvent listener
    void setMouseDown(bool down) { m_mouseDown = down; }

    // Gallery & packs
    // Images are identified by a RELATIVE path under galleryDir():
    //   - loose images:       "Normal.png"
    //   - inside a pack:      "packs/Kasane Teto/Move.gif"
    // The same string serves as state ID and disk path.

    // Available packs (subfolder names under packs/). Does NOT include the
    // "loose" pseudo-pack. Alphabetical order.
    std::vector<std::string> getPacks() const;
    // Images inside a pack. packName == "" => loose images (root).
    std::vector<std::string> getImagesInPack(std::string const& packName) const;
    // Compat: all images (loose + all packs), by relative path.
    std::vector<std::string> getGalleryImages() const;

    std::string addToGallery(std::filesystem::path const& srcPath);
    // Imports any supported file: normal images, Windows cursors
    // (.cur/.ico/.ani), and .zip packs. A .zip creates its own pack.
    // Returns the list of relative paths created (empty if nothing imported).
    std::vector<std::string> importFromFile(std::filesystem::path const& srcPath);
    // Reason the last importFromFile failed (empty on success).
    std::string const& lastImportError() const { return m_lastImportError; }
    // Pack name created by the last importFromFile of a .zip ("" if none).
    std::string const& lastImportedPack() const { return m_lastImportedPack; }

    void removeFromGallery(std::string const& relPath);
    void removeAllFromGallery();
    void removePack(std::string const& packName);     // deletes a pack folder
    int  cleanupInvalidImages();
    std::filesystem::path galleryDir() const;
    std::filesystem::path packsDir() const;
    // Loads the thumbnail for an image by its relative path.
    cocos2d::CCTexture2D* loadGalleryThumb(std::string const& relPath) const;

    // State
    bool isAttached() const { return m_cursorNode && m_cursorNode->getParent(); }
    bool shouldShowOnCurrentScene() const;

    // Re-visit the cursor node so it draws on top of ImGui-based menus
    // (e.g. EclipseMenu via imgui-cocos). Called from CCEGLView::swapBuffers
    // with Priority::VeryLate, which runs AFTER imgui-cocos has finished its
    // ImGui render pass but BEFORE the actual GL buffer swap. The double-
    // visit (once in cocos2d's normal scene traversal, once here) is cheap
    // (one sprite + optional motion streak) and lets the cursor stay above
    // any ImGui overlay without depending on imgui-cocos directly.
    void renderOverlay();

    // 10 built-in trail presets
    static constexpr int TRAIL_PRESET_COUNT = 10;
    static const CursorTrailPreset TRAIL_PRESETS[TRAIL_PRESET_COUNT];

private:
    CursorManager() = default;
    ~CursorManager();

    CursorConfig m_config;

    geode::Ref<cocos2d::CCNode> m_cursorNode = nullptr;
    // One sprite per state. Idle always exists (uses the fallback arrow if
    // no image is assigned). Others are only created when they have an image.
    std::map<CursorState, cocos2d::CCSprite*> m_sprites;
    cocos2d::CCMotionStreak*    m_trail       = nullptr;

    cocos2d::CCPoint m_currentPos;
    cocos2d::CCPoint m_targetPos;   // actual mouse position (for follow delay lerp)
    cocos2d::CCPoint m_velocity;
    bool m_isMoving  = false;
    float m_moveTimer = 0.f;    // seconds since last significant movement
    bool m_mouseDown = false;   // left mouse button currently held
    bool m_systemCursorHidden = false;
    std::string m_lastImportError; // reason for the last failed import
    std::string m_lastImportedPack; // pack created by the last .zip import

    std::filesystem::path configPath() const;
    std::string& configFieldForState(CursorState state);
    // Imports a single non-zip file already read into memory to `destDir`.
    // `displayName` is the suggested base name. Returns the RELATIVE path
    // within galleryDir() of the file created, or "" on failure/unsupported format.
    std::string importSingleData(std::vector<uint8_t> const& data,
                                 std::string const& displayName,
                                 std::filesystem::path const& destDir,
                                 std::string const& relPrefix);
    cocos2d::CCSprite* loadSprite(std::string const& relPath);
    cocos2d::CCSprite* createFallbackSprite();
    cocos2d::CCSprite* spriteForState(CursorState state) const;
    bool hasLoadedCursorVisual() const;
    bool sceneMatchesVisibleLayers(cocos2d::CCScene* scene) const;
    bool isCursorOverButton(cocos2d::CCPoint const& worldPos) const;
    CursorState resolveActiveState(cocos2d::CCPoint const& mouseWorld) const;
    void syncSystemCursorVisibility(bool hideSystemCursor);
    void updateTrail();
};
