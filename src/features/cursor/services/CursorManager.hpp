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
// Hotspot del cursor original (flecha): esquina superior-izquierda.
// (0,1) ancla el sprite por su esquina superior-izquierda al punto del mouse,
// de modo que el "click point" es fiel al cursor original e INDEPENDIENTE de
// la escala (no se separa al agrandar/achicar la imagen).
inline constexpr float CURSOR_HOTSPOT_X = 0.f;
inline constexpr float CURSOR_HOTSPOT_Y = 1.0f;

// ────────────────────────────────────────────────────────────────────────
// Estados del cursor (inspirado en Ecuet/Custom-Cursor).
//
// El cursor de Geometry Dash vanilla es estatico; Ecuet introduce la idea de
// reaccionar al contexto: una imagen distinta al pasar por un boton (Hover) y
// otra al mantener pulsado el click (Click/Hold). Paimbnails ya tenia Idle/Move
// (reposo vs movimiento); aqui se anaden Hover, Click, Text y Disabled,
// cubriendo los roles de cursor de Windows que GD permite detectar.
//
// Prioridad de resolucion (de mayor a menor):
//   Click > Disabled > Text > Hover > Move > Idle.
// Cualquier estado sin imagen asignada cae automaticamente a Idle.
// ────────────────────────────────────────────────────────────────────────
enum class CursorState {
    Idle     = 0,
    Move     = 1,
    Hover    = 2,   // sobre un boton interactivo (link/mano)
    Click    = 3,   // manteniendo el click izquierdo
    Text     = 4,   // sobre un campo de texto (I-beam)
    Disabled = 5,   // sobre un boton deshabilitado (no permitido)
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

    // Una imagen (filename en la galeria) por estado.
    std::string idleImage    = "";      // reposo / fallback de todos los estados
    std::string moveImage    = "";      // mientras el cursor se mueve
    std::string hoverImage   = "";      // mientras se pasa por encima de un boton
    std::string clickImage   = "";      // mientras se mantiene pulsado el click
    std::string textImage    = "";      // sobre un campo de texto (I-beam)
    std::string disabledImage = "";     // sobre un boton deshabilitado

    float scale              = CURSOR_SCALE_DEFAULT;   // 0.10 – 3.0
    int   opacity            = 255;    // 0 – 255

    // Hover/click/text/disabled reaccionan al contexto del juego. Se pueden
    // desactivar para dejar solo el comportamiento clasico Idle/Move.
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

// ────────────────────────────────────────────────────────────────────────
// CursorManager: singleton
// ────────────────────────────────────────────────────────────────────────

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

    // ── Gallery & packs ──────────────────────────────────────────────────
    // Las imagenes se identifican por una ruta RELATIVA a galleryDir():
    //   - imagenes sueltas:  "Normal.png"
    //   - dentro de un pack: "packs/Kasane Teto/Move.gif"
    // Asi un mismo string sirve como id de estado y como ruta en disco.

    // Lista de packs disponibles (nombres de subcarpeta bajo packs/). NO
    // incluye el pseudo-pack "sueltas" (loose). Orden alfabetico.
    std::vector<std::string> getPacks() const;
    // Imagenes dentro de un pack. packName == "" => imagenes sueltas (root).
    std::vector<std::string> getImagesInPack(std::string const& packName) const;
    // Compat: todas las imagenes (sueltas + de todos los packs), por ruta rel.
    std::vector<std::string> getGalleryImages() const;

    std::string addToGallery(std::filesystem::path const& srcPath);
    // Importa cualquier archivo soportado: imagenes normales, cursores de
    // Windows (.cur/.ico/.ani) y packs .zip. Un .zip crea su propio pack.
    // Devuelve la lista de rutas relativas creadas (vacio si nada se importo).
    std::vector<std::string> importFromFile(std::filesystem::path const& srcPath);
    // Motivo del ultimo importFromFile que termino sin importar nada.
    std::string const& lastImportError() const { return m_lastImportError; }
    // Nombre del pack creado por el ultimo importFromFile de un .zip ("" si no).
    std::string const& lastImportedPack() const { return m_lastImportedPack; }

    void removeFromGallery(std::string const& relPath);
    void removeAllFromGallery();
    void removePack(std::string const& packName);     // borra una carpeta de pack
    int  cleanupInvalidImages();
    std::filesystem::path galleryDir() const;
    std::filesystem::path packsDir() const;
    // Carga la miniatura de una imagen por su ruta relativa.
    cocos2d::CCTexture2D* loadGalleryThumb(std::string const& relPath) const;

    // State
    bool isAttached() const { return m_cursorNode && m_cursorNode->getParent(); }
    bool shouldShowOnCurrentScene() const;

    // 10 built-in trail presets
    static constexpr int TRAIL_PRESET_COUNT = 10;
    static const CursorTrailPreset TRAIL_PRESETS[TRAIL_PRESET_COUNT];

private:
    CursorManager() = default;
    ~CursorManager();

    CursorConfig m_config;

    geode::Ref<cocos2d::CCNode> m_cursorNode = nullptr;
    // Un sprite por estado. Idle siempre existe (usa la flecha de fallback si
    // no hay imagen). Los demas solo se crean cuando tienen imagen asignada.
    std::map<CursorState, cocos2d::CCSprite*> m_sprites;
    cocos2d::CCMotionStreak*    m_trail       = nullptr;

    cocos2d::CCPoint m_currentPos;
    cocos2d::CCPoint m_targetPos;   // actual mouse position (for follow delay lerp)
    cocos2d::CCPoint m_velocity;
    bool m_isMoving  = false;
    float m_moveTimer = 0.f;    // seconds since last significant movement
    bool m_mouseDown = false;   // left mouse button currently held
    bool m_systemCursorHidden = false;
    std::string m_lastImportError; // motivo del ultimo import fallido
    std::string m_lastImportedPack; // pack creado por el ultimo zip importado

    std::filesystem::path configPath() const;
    std::string& configFieldForState(CursorState state);
    // Importa un archivo individual (no-zip) ya leido a memoria a `destDir`.
    // `displayName` es el nombre base sugerido. Devuelve la ruta RELATIVA a
    // galleryDir() del archivo creado, o vacio si fallo / formato no soportado.
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
