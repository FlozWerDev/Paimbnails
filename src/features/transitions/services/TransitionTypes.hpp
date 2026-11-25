#pragma once
#include <string>
#include <vector>

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
    Stinger,
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
    Bounce,
    Skew,
    Overlay,
    FollowCursor
};

// easing applied to every interpolated command (OBS-style curve per clip).
enum class TransitionEasing {
    Linear,
    EaseIn,
    EaseOut,
    EaseInOut,
    Sine,
    Back,
    Bounce,
    Elastic
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
    TransitionEasing easing = TransitionEasing::Linear;
    bool enabled = true;
    // object system: clips may animate an Image object instead of a scene.
    // targetObj = index (in the same command list) of an Image command, -1 = scene.
    // for Image commands, fromX/fromY are the object position as screen fractions.
    int targetObj = -1;
    float objScale = 1.f; // Image: size relative to the full-screen fit
    float startTime = -1.f; // negative = legacy sequential timing; otherwise absolute seconds
    bool solidObject = false; // Image command rendered as a colored rectangle
    bool stretchMedia = false;
    std::string objectName;
};

struct TransitionConfig {
    TransitionType type = TransitionType::Fade;
    float duration = 0.5f;
    int colorR = 0, colorG = 0, colorB = 0;
    std::vector<TransitionCommand> commands;
    std::string scriptPath;
    std::string mediaPath; // prepared .pttransition manifest
    float cutPoint = .5f; // normalized time, independent of media duration
    std::string customName; // studio: user name for a Custom timeline
    bool sceneCut = false; // switch A/B at cutPoint; legacy timelines animate scene opacity
};

