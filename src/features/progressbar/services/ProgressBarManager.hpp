#pragma once
#include <Geode/Geode.hpp>
#include <filesystem>

enum class BarColorMode : int {
    Solid   = 0,
    Pulse   = 1,
    Rainbow = 2,
};

struct BarDecoration {
    std::string path;
    float posX = 0.f;
    float posY = 0.f;
    float scale = 1.f;
    float rotation = 0.f;
};

struct ProgressBarConfig {
    bool enabled = false;

    bool vertical = false;

    bool  useCustomPosition = false;
    float posX = 0.f;
    float posY = 0.f;

    float scaleLength = 1.f;
    float scaleThickness = 1.f;

    bool freeDragMode = false;

    int opacity = 255;

    bool useCustomFillColor = false;
    cocos2d::ccColor3B fillColor = {80, 220, 255};

    bool useCustomBgColor = false;
    cocos2d::ccColor3B bgColor = {255, 255, 255};

    bool showPercentage = true;
    float percentageScale = 1.f;
    float percentageOffsetX = 0.f;
    float percentageOffsetY = 0.f;
    bool useCustomPercentageColor = false;
    cocos2d::ccColor3B percentageColor = {255, 255, 255};

    std::string percentageFont;

    bool useCustomLabelPosition = false;
    float labelPosX = 0.f;
    float labelPosY = 0.f;

    BarColorMode fillColorMode = BarColorMode::Solid;
    BarColorMode bgColorMode   = BarColorMode::Solid;
    BarColorMode pctColorMode  = BarColorMode::Solid;

    cocos2d::ccColor3B fillColor2 = {255,  64,  64};
    cocos2d::ccColor3B bgColor2   = { 64,  64, 255};
    cocos2d::ccColor3B pctColor2  = {255, 255,  64};

    float colorAnimSpeed = 1.f;

    bool useFillTexture = false;
    bool useBgTexture   = false;
    std::string fillTexturePath;
    std::string bgTexturePath;

    std::vector<BarDecoration> decorations;

    float userRotation = 0.f;
};


class ProgressBarManager {
public:
    static ProgressBarManager& get();

    void loadConfig();
    void saveConfig();

    ProgressBarConfig& config() { return m_config; }
    ProgressBarConfig const& config() const { return m_config; }

    void applyToPlayLayer(cocos2d::CCNode* playLayer);

    void resetToDefaults();

    bool isFreeDragActive() const;
    void beginDrag(cocos2d::CCPoint startWorld);
    void updateDrag(cocos2d::CCPoint currentWorld);
    void endDrag();
    bool isDragging() const { return m_dragging; }

    void invalidateBaseline() {
        m_baselineCaptured = false;
        m_labelBaselineCaptured = false;
        m_cachedPlayLayer = nullptr;
        m_cachedBar = nullptr;
        m_cachedLabel = nullptr;
        m_barSearchCooldown = 0;
    }

    void releaseCustomTextures();

    void sanitizeConfig();

    int  addDecoration(BarDecoration const& d);
    void removeDecoration(int index);
    int  decorationCount() const { return static_cast<int>(m_config.decorations.size()); }
    cocos2d::CCNode* getDecorationNode(int index);

private:
    ProgressBarManager() = default;
    std::filesystem::path configPath() const;

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

    struct TextureBaseline {
        geode::Ref<cocos2d::CCTexture2D> texture;
        cocos2d::CCRect rect{};
        bool captured = false;
    };
    struct CustomTexture {
         std::string path;
         cocos2d::CCNode* animHost = nullptr;
         geode::Ref<cocos2d::CCTexture2D> staticTex;
         bool justChanged = false;
    };

    cocos2d::CCTexture2D* resolveCustomTexture(
        cocos2d::CCNode* host,
        CustomTexture& slot,
        std::string const& path);

    void captureSpriteBaseline(cocos2d::CCSprite* spr, TextureBaseline& tb);
    void restoreSpriteBaseline(cocos2d::CCSprite* spr, TextureBaseline& tb);

    ProgressBarConfig m_config;

    bool m_baselineCaptured = false;
    cocos2d::CCPoint m_baselinePos = {0, 0};
    float m_baselineScaleX = 1.f;
    float m_baselineScaleY = 1.f;
    float m_baselineRotation = 0.f;

    bool m_labelBaselineCaptured = false;
    cocos2d::CCPoint m_labelBaselinePos = {0, 0};
    float m_labelBaselineScale = 1.f;

    bool m_wasActive = false;

    bool m_dragging = false;
    cocos2d::CCPoint m_dragOffset = {0, 0};

    TextureBaseline m_fillBaselineTex;
    TextureBaseline m_bgBaselineTex;
    CustomTexture   m_fillCustom;
    CustomTexture   m_bgCustom;

    std::vector<cocos2d::CCNode*> m_liveDecorations;
    std::vector<std::string> m_liveDecorationPaths;

    float m_animTime = 0.f;
    long long m_lastTickNs = 0;

    cocos2d::CCNode* m_cachedPlayLayer = nullptr;
    cocos2d::CCNode* m_cachedBar = nullptr;
    cocos2d::CCNode* m_cachedLabel = nullptr;

    static constexpr int kBarSearchInterval = 30;
    int m_barSearchCooldown = 0;
};
