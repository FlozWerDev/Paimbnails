#pragma once
#include <Geode/Geode.hpp>
#include <string>
#include <vector>
#include <optional>

// TransitionManager — sistema de transiciones personalizables
//

enum class TransitionType {
    Fade,
    FadeWhite,
    FadeColor,
    CrossFade,

    SlideLeft,
    SlideRight,
    SlideUp,
    SlideDown,
    MoveInLeft,
    MoveInRight,
    MoveInTop,
    MoveInBottom,

    ZoomIn,
    ZoomOut,
    ZoomFlipYUp,
    ZoomFlipYDown,
    FlipX,
    FlipY,
    FlipAngular,
    ShrinkGrow,
    RotoZoom,
    JumpZoom,

    FadeTR,
    FadeBL,
    FadeUp,
    FadeDown,
    TurnOffTiles,
    SplitCols,
    SplitRows,
    ProgressRadialCW,
    ProgressRadialCCW,
    ProgressInOut,
    ProgressOutIn,
    ProgressHorizontal,
    ProgressVertical,

    PageForward,
    PageBackward,

    FadeBounce,
    SlideOverLeft,
    SlideOverRight,
    SlideOverUp,
    SlideOverDown,
    ZoomShrinkFade,
    SpinCW,
    SpinCCW,
    PixelateFade,
    DiamondWipe,
    DoubleDoor,
    Blinds,
    Swirl,
    Glitch,
    CinematicBars,
    FlashWhite,
    HeartIris,
    WaveSlide,
    Random,

    Custom,
    None
};


enum class CommandAction {
    FadeOut,
    FadeIn,
    Move,
    Scale,
    Rotate,
    Wait,
    Color,
    EaseIn,
    EaseOut,
    Spawn,
    Image,
    Shake,
    Bounce
};

struct TransitionCommand {
    CommandAction action = CommandAction::Wait;
    std::string target = "from";
    float duration = 0.3f;
    float fromX = 0.f, fromY = 0.f;
    float toX = 0.f, toY = 0.f;
    float fromVal = 1.f;
    float toVal = 1.f;
    int r = 0, g = 0, b = 0;
    std::string imagePath;
    int spawnCount = 0;
    float delay = 0.f;
    float intensity = 5.f;
};

struct TransitionConfig {
    TransitionType type = TransitionType::Fade;
    float duration = 0.5f;
    int colorR = 0, colorG = 0, colorB = 0;
    std::string imagePath;
    std::vector<std::string> imageList;
    std::vector<TransitionCommand> commands;
    std::string scriptPath;
};

class TransitionManager {
public:
    static TransitionManager& get();

    void loadConfig();
    void saveConfig();

    TransitionConfig getGlobalConfig() const { return m_globalConfig; }
    void setGlobalConfig(TransitionConfig const& cfg);

    TransitionConfig getLevelEntryConfig() const;
    void setLevelEntryConfig(TransitionConfig const& cfg);
    bool hasLevelEntryConfig() const { return m_hasLevelEntryConfig; }
    void clearLevelEntryConfig();

    cocos2d::CCScene* createTransition(
        TransitionConfig const& cfg,
        cocos2d::CCScene* dest,
        bool isPush = false);

    void replaceScene(cocos2d::CCScene* dest);
    void pushScene(cocos2d::CCScene* dest);

    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool v) { m_enabled = v; }

    void tripCustomSafeMode(std::string const& reason);
    bool isCustomSafeModeTripped() const { return m_customSafeModeTripped; }
    void resetCustomSafeMode();

    static TransitionType typeFromString(std::string const& s);
    static std::string typeToString(TransitionType t);
    static std::string typeDisplayName(TransitionType t);
    static std::string typeDescription(TransitionType t);
    static CommandAction actionFromString(std::string const& s);
    static std::string actionToString(CommandAction a);
    static bool isValidTarget(std::string const& target);
    static int sanitizeCommand(TransitionCommand& cmd);
    static int sanitizeCommands(std::vector<TransitionCommand>& commands);
    static int sanitizeConfig(TransitionConfig& cfg);

    static std::vector<TransitionType> const& allTypes();

    cocos2d::CCTransitionScene* createNativeTransition(TransitionConfig const& cfg, cocos2d::CCScene* dest) const;

    std::vector<TransitionCommand> buildPreviewCommands(TransitionType type, float dur) const;

private:
    TransitionManager();

    std::vector<TransitionCommand> parseScriptFile(std::string const& path) const;
    std::filesystem::path getConfigPath() const;

    TransitionConfig m_globalConfig;
    TransitionConfig m_levelEntryConfig;
    bool m_hasLevelEntryConfig = false;
    bool m_enabled = true;
    bool m_loaded = false;
    bool m_customSafeModeTripped = false;
};
