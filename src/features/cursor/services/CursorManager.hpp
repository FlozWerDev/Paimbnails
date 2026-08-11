#pragma once
#include <Geode/Geode.hpp>
#include "CursorClickFX.hpp"
#include "CursorTrailFX.hpp"
#include "CursorTransitionFX.hpp"
#include <cstdint>
#include <filesystem>
#include <vector>
#include <string>
#include <set>
#include <map>

inline std::vector<std::string> CURSOR_LAYER_OPTIONS = {
    "MenuLayer", "LevelBrowserLayer", "LevelInfoLayer",
    "CreatorLayer", "LevelSearchLayer", "GauntletSelectLayer",
    "ProfilePage", "LevelListLayer", "LevelEditorLayer"
};

inline constexpr float CURSOR_SCALE_MIN = 0.10f;
inline constexpr float CURSOR_SCALE_MAX = 3.0f;
inline constexpr float CURSOR_SCALE_DEFAULT = 0.30f;
// Keep the sprite's top-left hotspot aligned with the pointer.
inline constexpr float CURSOR_HOTSPOT_X = 0.f;
inline constexpr float CURSOR_HOTSPOT_Y = 1.0f;

// Priority: Click > Disabled > Text > Hover > Move > Idle.
enum class CursorState {
    Idle     = 0,
    Move     = 1,
    Hover    = 2,
    Click    = 3,
    Text     = 4,
    Disabled = 5,
};

inline constexpr int CURSOR_STATE_COUNT = 6;

struct CursorConfig {
    bool enabled             = false;

    std::string idleImage    = "";
    std::string moveImage    = "";
    std::string hoverImage   = "";
    std::string clickImage   = "";
    std::string textImage    = "";
    std::string disabledImage = "";

    float scale              = CURSOR_SCALE_DEFAULT;
    int   opacity            = 255;

    bool  hoverEnabled       = true;
    bool  clickEnabled       = true;
    bool  textEnabled        = true;
    bool  disabledEnabled    = true;

    bool  transitionEnabled  = true;
    int   transitionPreset   = 0;
    paimon::cursorfx::TransitionSettings transition{};

    bool  followDelayEnabled = false;
    float followDelay        = 0.5f;

    bool  trailEnabled       = false;
    int   trailPreset        = 0;
    paimon::cursorfx::TrailSettings trail{};

    bool  clickFxEnabled     = false;
    int   clickPreset        = 0;
    paimon::cursorfx::ClickSettings click{};

    std::set<std::string> visibleLayers = {
        "MenuLayer", "LevelBrowserLayer", "LevelInfoLayer",
        "CreatorLayer", "LevelSearchLayer", "GauntletSelectLayer",
        "ProfilePage", "LevelListLayer", "LevelEditorLayer"
    };
};


class CursorManager {
public:
    static CursorManager& get();

    void init();
    void update(float dt);
    void attachToOverlay();
    void detachFromScene();
    void releaseSharedResources();
    void onGLContextReload();

    CursorConfig& config() { return m_config; }
    void loadConfig();
    void saveConfig();
    void applyConfigLive();
    void applyTrailLive();
    void applyTransitionLive();
    void applyClickLive();

    std::string imageForState(CursorState state) const;
    void setImageForState(CursorState state, std::string const& filename);
    void setIdleImage(std::string const& filename)  { setImageForState(CursorState::Idle,  filename); }
    void setMoveImage(std::string const& filename)  { setImageForState(CursorState::Move,  filename); }
    void setHoverImage(std::string const& filename) { setImageForState(CursorState::Hover, filename); }
    void setClickImage(std::string const& filename) { setImageForState(CursorState::Click, filename); }
    void setTextImage(std::string const& filename)  { setImageForState(CursorState::Text,  filename); }
    void setDisabledImage(std::string const& filename) { setImageForState(CursorState::Disabled, filename); }
    void reloadSprites();
    cocos2d::CCSprite* createPreviewSprite();
    cocos2d::CCSprite* createPreviewSprite(CursorState state);

    void setMouseDown(bool down);
    void setSecondaryMouseDown(bool down);
    bool isMouseDown() const { return m_mouseDown; }
    bool isClickFxHeld() const { return m_fxHeld; }
    void setTouchPoint(cocos2d::CCPoint const& point) { m_touchPoint = point; }
    cocos2d::CCPoint pointerPos() const;

    // Gallery paths are relative to galleryDir() and double as state IDs.

    std::vector<std::string> getPacks() const;
    std::vector<std::string> getImagesInPack(std::string const& packName) const;
    std::vector<std::string> getGalleryImages() const;

    std::string addToGallery(std::filesystem::path const& srcPath);
    std::vector<std::string> importFromFile(std::filesystem::path const& srcPath);

    // Importa bytes ya en memoria (descargas de la tienda). packName vacio deja
    // el cursor en la galeria suelta.
    std::string importData(std::vector<uint8_t> const& data,
                           std::string const& displayName,
                           std::string const& packName = "");
    // Reserva una carpeta de pack con nombre libre y devuelve ese nombre.
    std::string createPack(std::string const& baseName);
    // Importa un .zip descargado como pack nuevo.
    std::vector<std::string> importZipData(std::vector<uint8_t> const& data,
                                           std::string const& displayName);

    std::string const& lastImportError() const { return m_lastImportError; }
    std::string const& lastImportedPack() const { return m_lastImportedPack; }

    void removeFromGallery(std::string const& relPath);
    void removeAllFromGallery();
    void removePack(std::string const& packName);
    int  cleanupInvalidImages();
    std::filesystem::path galleryDir() const;
    std::filesystem::path packsDir() const;
    cocos2d::CCTexture2D* loadGalleryThumb(std::string const& relPath) const;

    bool isAttached() const { return m_cursorNode && m_cursorNode->getParent(); }
    bool shouldShowOnCurrentScene() const;

    // Draw after ImGui at VeryLate priority so the cursor stays on top.
    void renderOverlay();

    void applyTrailPreset(int index);
    void applyTransitionPreset(int index);
    void applyClickPreset(int index);

private:
    CursorManager() = default;
    ~CursorManager();

    CursorConfig m_config;

    geode::Ref<cocos2d::CCNode> m_cursorNode = nullptr;
    geode::Ref<cocos2d::CCNode> m_clickHost = nullptr;
    std::map<CursorState, cocos2d::CCSprite*> m_sprites;
    std::map<CursorState, cocos2d::CCPoint> m_spriteBaseScales;
    std::map<CursorState, paimon::cursorfx::TransitionFrame> m_transitionStartFrames;
    std::map<CursorState, paimon::cursorfx::TransitionFrame> m_renderedTransitionFrames;
    paimon::cursorfx::CursorTrailNode* m_trail = nullptr;
    paimon::cursorfx::CursorClickNode* m_clickFx = nullptr;

    CursorState m_activeState = CursorState::Idle;
    CursorState m_previousState = CursorState::Idle;
    CursorState m_cachedState = CursorState::Idle;
    int m_contextFrame = 0;
    float m_stateTransitionTime = 0.f;
    bool m_stateTransitioning = false;
    bool m_stateTransitionInterrupted = false;

    cocos2d::CCPoint m_currentPos;
    cocos2d::CCPoint m_targetPos;
    cocos2d::CCPoint m_velocity;
    bool m_isMoving  = false;
    float m_moveTimer = 0.f;
    bool m_mouseDown = false;
    bool m_rightDown = false;
    bool m_fxHeld = false;
    cocos2d::CCPoint m_touchPoint{};
    bool m_sceneVisible = false;
    int  m_clickModuleCooldown = 0;
    bool m_clickModuleOn = false;
    int  m_transitionModuleCooldown = 0;
    bool m_transitionModuleOn = false;
    std::vector<uint8_t> m_clickQueue;
    float m_clickAnimTime = 999.f;
    bool  m_clickAnimHeld = false;
    paimon::cursorfx::TransitionFrame m_clickFrame{};
    bool m_systemCursorHidden = false;
    std::string m_lastImportError;
    std::string m_lastImportedPack;

    std::filesystem::path configPath() const;
    void loadTrailConfig(matjson::Value const& j);
    void loadClickConfig(matjson::Value const& j);
    std::string& configFieldForState(CursorState state);
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
    void updateStateSprites(float dt, CursorState active);
    void finishStateTransition();
    void updateTrail();
    void updateClickFx();
    void detachClickOverlay();
    void updateClickOverlay(float dt);
    void refreshClickHold();
    void processClickEvents(cocos2d::CCPoint const& at);
};
