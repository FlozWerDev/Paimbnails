#pragma once
#include <Geode/Geode.hpp>
#include <filesystem>
#include <vector>

class CustomProgressBarNode;

enum class BarStyle : int {
    VanillaGD   = 0,
    Sleek       = 1,
    Classic     = 2,
    Minimal     = 3,
    NeonGlow    = 4,
    Segmented   = 5,
    Glass       = 6,
    Striped     = 7,
    CustomImage = 8,
};

enum class BarColorMode : int {
    Solid            = 0,
    Pulse            = 1,
    Rainbow          = 2,
    Gradient2        = 3,
    Gradient3        = 4,
    ProgressReactive = 5,
};

enum class BarLabelFormat : int {
    Percent     = 0,
    PercentBest = 1,
    RunSegment  = 2,
    Compact     = 3,
    AutoTime    = 4,
};

enum class BarLabelPlacement : int {
    InsideCenter  = 0,
    RightOfBar    = 1,
    LeftOfBar     = 2,
    AboveBar      = 3,
    BelowBar      = 4,
    FollowTip     = 5,
    CustomFreePos = 6,
};

struct BarDecoration {
    std::string path;
    float posX = 0.f;
    float posY = 0.f;
    float scale = 1.f;
    float rotation = 0.f;
};

struct ProgressBarTelemetry {
    float rawPercent = 0.f;
    float smoothedPercent = 0.f;
    float bestPercent = 0.f;
    float startPosPercent = 0.f;
    float lastDeathPercent = -1.f;
    double elapsedSeconds = 0.0;
    bool isPractice = false;
    bool isPlatformer = false;
    bool newBestReached = false;
    float newBestFlashTimer = 0.f;
    std::vector<float> checkpoints;
};

struct ProgressBarConfig {
    bool enabled = false;

    BarStyle style = BarStyle::VanillaGD;
    int presetId = -1;

    bool vertical = false;
    bool useCustomPosition = false;
    float posX = 0.f;
    float posY = 0.f;

    float barWidth = 210.f;
    float barHeight = 10.f;
    float scaleLength = 1.f;
    float scaleThickness = 1.f;
    float borderWidth = 1.4f;
    float cornerRadius = 5.f;
    bool shadowEnabled = false;

    float smoothing = 0.0f; // 0 = instant 1:1 vanilla feel
    bool tipGlow = false;
    bool showPbMarker = false;
    bool showCheckpoints = true;
    bool showMilestones = false;
    bool showDeathMarker = false;
    bool newBestFlash = false;

    bool freeDragMode = false;
    int opacity = 255;

    bool useCustomFillColor = false;
    cocos2d::ccColor3B fillColor = {0, 255, 40};
    cocos2d::ccColor3B fillColor2 = {160, 100, 255};
    cocos2d::ccColor3B fillColor3 = {255, 195, 70};

    bool useCustomBgColor = false;
    cocos2d::ccColor3B bgColor = {18, 22, 34};
    cocos2d::ccColor3B bgColor2 = {40, 48, 72};
    cocos2d::ccColor3B borderColor = {235, 245, 255};

    bool hideInPractice = false;

    bool showPercentage = true;
    int decimalPrecision = 0; // 0, 1, 2
    BarLabelFormat labelFormat = BarLabelFormat::Percent;
    BarLabelPlacement labelPlacement = BarLabelPlacement::RightOfBar;
    float percentageScale = 1.f;
    float percentageOffsetX = 0.f;
    float percentageOffsetY = 0.f;
    bool useCustomPercentageColor = false;
    cocos2d::ccColor3B percentageColor = {255, 255, 255};
    cocos2d::ccColor3B pctColor2 = {255, 230, 100};
    std::string percentageFont;

    bool useCustomLabelPosition = false;
    float labelPosX = 0.f;
    float labelPosY = 0.f;

    BarColorMode fillColorMode = BarColorMode::Solid;
    BarColorMode bgColorMode   = BarColorMode::Solid;
    BarColorMode pctColorMode  = BarColorMode::Solid;

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

    ProgressBarTelemetry const& telemetry() const { return m_telemetry; }

    void applyToPlayLayer(cocos2d::CCNode* playLayer, bool isPracticeMode = false);

    // Telemetry callbacks from PlayLayer hooks
    void notifyLevelReset(float startPercent, float bestPercent, bool isPlatformer, bool isPractice);
    void notifyDeath(float deathPercent);

    void applyPreset(int presetIndex);
    static int presetCount();
    static char const* presetName(int index);

    void resetToDefaults();

    bool isFreeDragActive() const;
    void beginDrag(cocos2d::CCPoint startWorld);
    void updateDrag(cocos2d::CCPoint currentWorld);
    void endDrag();

    cocos2d::CCNode* getActiveBarNode(cocos2d::CCNode* playLayerRoot = nullptr);
    cocos2d::CCNode* getActiveLabelNode(cocos2d::CCNode* playLayerRoot = nullptr);

    void invalidateBaseline();

    void releaseCustomTextures();

    void sanitizeConfig();

    int  addDecoration(BarDecoration const& d);
    void removeDecoration(int index);
    int  decorationCount() const { return static_cast<int>(m_config.decorations.size()); }
    cocos2d::CCNode* getDecorationNode(int index);

    static void applyFontIfNeeded(cocos2d::CCLabelBMFont* lb, std::string const& targetFont);

private:
    ProgressBarManager() = default;
    std::filesystem::path configPath() const;

    cocos2d::CCNode* findBarNode(cocos2d::CCNode* root);
    cocos2d::CCNode* findLabelNode(cocos2d::CCNode* root);

    void captureBaselineIfNeeded(cocos2d::CCNode* bar, cocos2d::CCNode* label);
    void restoreVanillaState(cocos2d::CCNode* bar, cocos2d::CCNode* label);

    float tickAnimClock();
    void updateTelemetryFromPlayLayer(cocos2d::CCNode* playLayerRoot, float dt, bool isPractice);
    void ensureCustomNode(cocos2d::CCNode* playLayerRoot, cocos2d::CCNode* vanillaBar);
    void applyDecorations(cocos2d::CCNode* playLayerRoot);

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

    ProgressBarConfig m_config;
    ProgressBarTelemetry m_telemetry;

    bool m_baselineCaptured = false;
    cocos2d::CCPoint m_baselinePos = {0, 0};
    float m_baselineScaleX = 1.f;
    float m_baselineScaleY = 1.f;
    float m_baselineRotation = 0.f;
    int m_baselineZOrder = 10;

    bool m_labelBaselineCaptured = false;
    cocos2d::CCPoint m_labelBaselinePos = {0, 0};
    float m_labelBaselineScale = 1.f;
    std::string m_labelBaselineFont;
    bool m_practiceHidden = false;

    bool m_wasActive = false;

    bool m_dragging = false;
    cocos2d::CCPoint m_dragOffset = {0, 0};

    CustomTexture m_fillCustom;
    CustomTexture m_bgCustom;

    std::vector<cocos2d::CCNode*> m_liveDecorations;
    std::vector<std::string> m_liveDecorationPaths;

    float m_animTime = 0.f;
    long long m_lastTickNs = 0;

    cocos2d::CCNode* m_cachedPlayLayer = nullptr;
    cocos2d::CCNode* m_cachedBar = nullptr;
    cocos2d::CCNode* m_cachedLabel = nullptr;
    CustomProgressBarNode* m_customNode = nullptr;

    static constexpr int kBarSearchInterval = 30;
    int m_barSearchCooldown = 0;
};
