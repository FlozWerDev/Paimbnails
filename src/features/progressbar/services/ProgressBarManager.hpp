#pragma once
#include <Geode/Geode.hpp>
#include <filesystem>

// Customization settings for the in-game progress bar (node ID "progress-bar").

// Color animation mode: Solid, Pulse (ping-pong), Rainbow (HSV hue cycle).
enum class BarColorMode : int {
    Solid   = 0,
    Pulse   = 1,
    Rainbow = 2,
};

// User-added decoration (image/GIF) in world-space pixels.
struct BarDecoration {
    std::string path;     // absolute path on disk to the image/GIF
    float posX = 0.f;
    float posY = 0.f;
    float scale = 1.f;    // uniform scale
    float rotation = 0.f; // degrees, CW positive
};

struct ProgressBarConfig {
    // Master toggle. When false, the progress bar is left vanilla.
    bool enabled = false;

    // Orientation. If vertical, the bar is rotated 90 degrees.
    bool vertical = false;

    // Position override. When useCustomPosition is false the mod
    // leaves the vanilla position untouched (useful together with
    // rotation/scale tweaks only).
    bool  useCustomPosition = false;
    float posX = 0.f;   // screen pixels (winSize)
    float posY = 0.f;

    // Size (applied independently of orientation).
    // scaleLength = along the bar axis.
    // scaleThickness = perpendicular to the bar axis.
    float scaleLength = 1.f;     // 0.1 – 3.0
    float scaleThickness = 1.f;  // 0.1 – 3.0

    // Free drag mode: when enabled the user can hold & drag the bar
    // while the pause menu is visible to relocate it freely.
    bool freeDragMode = false;

    // Overall opacity (applies to bar sprites + percentage label).
    int opacity = 255;

    // Colors
    // When useCustomFillColor is enabled the "fill" sprite of the
    // progress bar (first child that isn't the outline) is tinted.
    bool useCustomFillColor = false;
    cocos2d::ccColor3B fillColor = {80, 220, 255};

    // Tint the outline / background sprite of the bar.
    bool useCustomBgColor = false;
    cocos2d::ccColor3B bgColor = {255, 255, 255};

    // Percentage label
    bool showPercentage = true;
    float percentageScale = 1.f;         // 0.3 – 2.5 multiplier
    float percentageOffsetX = 0.f;       // extra offset from bar
    float percentageOffsetY = 0.f;
    bool useCustomPercentageColor = false;
    cocos2d::ccColor3B percentageColor = {255, 255, 255};

    // .fnt file used by the percentage label (empty = keep GD default).
    std::string percentageFont;

    // Label position override (free-edit-mode). When true the label is
    // positioned absolutely on screen rather than anchored to the bar.
    bool useCustomLabelPosition = false;
    float labelPosX = 0.f;
    float labelPosY = 0.f;

    // Color animation
    // Each colour slot (fill, bg, percentage) has its own mode and
    // secondary colour used when mode == Pulse.
    BarColorMode fillColorMode = BarColorMode::Solid;
    BarColorMode bgColorMode   = BarColorMode::Solid;
    BarColorMode pctColorMode  = BarColorMode::Solid;

    cocos2d::ccColor3B fillColor2 = {255,  64,  64};
    cocos2d::ccColor3B bgColor2   = { 64,  64, 255};
    cocos2d::ccColor3B pctColor2  = {255, 255,  64};

    // Animation speed in cycles/second (0.1 – 5.0). Shared by all
    // animated color slots so the effect stays visually coherent.
    float colorAnimSpeed = 1.f;

    // Custom textures
    // Paths to user-picked images (PNG/JPG/WebP/GIF). When useX is
    // on and the path is valid, the corresponding sprite of the bar
    // has its texture swapped. GIFs animate via AnimatedGIFSprite.
    bool useFillTexture = false;
    bool useBgTexture   = false;
    std::string fillTexturePath;
    std::string bgTexturePath;

    // Extra user-placed decorations
    // Free-floating images / GIFs that the user added via the
    // free-edit-mode "Add" button. Each is movable, scalable and
    // rotatable independently from the bar.
    std::vector<BarDecoration> decorations;

    // Extra free-form rotation applied to the progress bar on top
    // of the baseline (and on top of the 90° flip when vertical).
    float userRotation = 0.f; // degrees
};

// Singleton owning config + persistence, applies values each frame.

class ProgressBarManager {
public:
    static ProgressBarManager& get();

    // Persistence
    void loadConfig();
    void saveConfig();

    ProgressBarConfig& config() { return m_config; }
    ProgressBarConfig const& config() const { return m_config; }

    // Applies current config to the progress-bar node (found in
    // PlayLayer's UI layer by ID "progress-bar"). Should be called
    // every frame from a ticker node.
    void applyToPlayLayer(cocos2d::CCNode* playLayer);

    // Resets to a sensible default (vanilla look/position).
    void resetToDefaults();

    // Called by the free-drag touch handler.
    bool isFreeDragActive() const;
    void beginDrag(cocos2d::CCPoint startWorld);
    void updateDrag(cocos2d::CCPoint currentWorld);
    void endDrag();
    bool isDragging() const { return m_dragging; }

    // Forces a fresh baseline capture on the next tick. Useful when
    // transitioning into a new level or when vanilla state changed.
    void invalidateBaseline() {
        m_baselineCaptured = false;
        m_labelBaselineCaptured = false;
        m_cachedPlayLayer = nullptr;
        m_cachedBar = nullptr;
        m_cachedLabel = nullptr;
    }

    // Releases any cached custom texture / GIF. Call this between
    // levels so the invisible host nodes don't leak across scenes.
    void releaseCustomTextures();

    // Clamp / repair invariants on the config (e.g. ranges out of
    // expected bounds, decoration paths empty). Idempotent.
    void sanitizeConfig();

    // Decoration helpers (manipulated by the edit overlay).
    // Returns the index of the newly-appended decoration.
    int  addDecoration(BarDecoration const& d);
    void removeDecoration(int index);
    int  decorationCount() const { return static_cast<int>(m_config.decorations.size()); }
    // Returns the live CCNode backing decoration `index` (or nullptr).
    cocos2d::CCNode* getDecorationNode(int index);

private:
    ProgressBarManager() = default;
    std::filesystem::path configPath() const;

    // apply(): split into focused helpers
    // Each helper assumes its inputs are already validated.
    cocos2d::CCNode* findBarNode(cocos2d::CCNode* root);
    cocos2d::CCNode* findLabelNode(cocos2d::CCNode* root);

    void captureBaselineIfNeeded(cocos2d::CCNode* bar, cocos2d::CCNode* label);
    void restoreVanillaState(cocos2d::CCNode* bar, cocos2d::CCNode* label);

    void tickAnimClock();
    void applyTransform(cocos2d::CCNode* bar);
    void applyOpacity(cocos2d::CCNode* bar);
    void applySprites(cocos2d::CCNode* bar, cocos2d::CCNode* playLayerRoot);
    void applyLabel(cocos2d::CCNode* label, cocos2d::CCNode* bar);
    void applyDecorations(cocos2d::CCNode* playLayerRoot);

    // Custom texture support
    struct TextureBaseline {
        geode::Ref<cocos2d::CCTexture2D> texture;
        cocos2d::CCRect rect{};
        bool captured = false;
    };
    struct CustomTexture {
        std::string path;                              // cache key
        cocos2d::CCNode* animHost = nullptr;           // GIF host (owned by PlayLayer)
        geode::Ref<cocos2d::CCTexture2D> staticTex;    // static texture otherwise
        bool justChanged = false;                      // set true the frame the tex changed
    };

    // Loads the image/GIF at `path` if it changed since last call,
    // caches it, and returns the current frame texture (nullptr on
    // failure). `host` is the parent for invisible GIF host nodes.
    cocos2d::CCTexture2D* resolveCustomTexture(
        cocos2d::CCNode* host,
        CustomTexture& slot,
        std::string const& path);

    void captureSpriteBaseline(cocos2d::CCSprite* spr, TextureBaseline& tb);
    void restoreSpriteBaseline(cocos2d::CCSprite* spr, TextureBaseline& tb);

    // Persist state
    ProgressBarConfig m_config;

    // Cached vanilla position/scale/rotation (sampled once).
    bool m_baselineCaptured = false;
    cocos2d::CCPoint m_baselinePos = {0, 0};
    float m_baselineScaleX = 1.f;
    float m_baselineScaleY = 1.f;
    float m_baselineRotation = 0.f;

    // Same for the percentage label (sampled together with the bar).
    bool m_labelBaselineCaptured = false;
    cocos2d::CCPoint m_labelBaselinePos = {0, 0};
    float m_labelBaselineScale = 1.f;

    // Tracks whether the previous frame applied custom settings, so we
    // can restore the vanilla look exactly once on disable.
    bool m_wasActive = false;

    // Drag state
    bool m_dragging = false;
    cocos2d::CCPoint m_dragOffset = {0, 0};

    TextureBaseline m_fillBaselineTex;
    TextureBaseline m_bgBaselineTex;
    CustomTexture   m_fillCustom;
    CustomTexture   m_bgCustom;

    // Live CCNodes for each user-placed decoration. The indices track
    // m_config.decorations 1:1. A slot may legitimately hold nullptr
    // (e.g. while the node is being spawned lazily).
    std::vector<cocos2d::CCNode*> m_liveDecorations;
    // Cached paths per live node so we can detect path changes and
    // rebuild only when needed.
    std::vector<std::string> m_liveDecorationPaths;

    // Clock used for color animation (monotonic seconds). Uses a
    // member instead of a static so values reset cleanly per session.
    float m_animTime = 0.f;
    long long m_lastTickNs = 0; // 0 = uninitialized

    // Perf: cached node references to avoid getChildByIDRecursive every frame
    cocos2d::CCNode* m_cachedPlayLayer = nullptr;
    cocos2d::CCNode* m_cachedBar = nullptr;
    cocos2d::CCNode* m_cachedLabel = nullptr;
};
