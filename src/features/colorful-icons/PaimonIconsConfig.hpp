#pragma once

#include <Geode/cocos/include/ccTypes.h>

namespace paimon::icons {

// Stored as int in JSON. Numeric values are frozen for save-compat; the gaps
// (3 = SatBoost, 7 = PerGamemode) are legacy modes removed in the redesign.
enum class ColorMode : int {
    Player       = 0,
    CustomRGB    = 1,
    HueShift     = 2,
    RandomStable = 4,
    Rainbow      = 5,
    Gradient     = 6,
    Inverted     = 8,
    Monochrome   = 9,
};

// The gap (4 = CustomMix) is a legacy style folded into ShowDimmed.
enum class LockStyle : int {
    Default    = 0,
    ShowDimmed = 1,
    TintedLock = 2,
    Silhouette = 3,
    HideBoth   = 5,
};

enum class RandomPalette : int {
    Vibrant = 0,
    Pastel  = 1,
    Neon    = 2,
    Earthy  = 3,
};

// Only areas that actually have a recolor hook (garage kit, shops).
struct ApplyToFlags {
    bool kit   = true;
    bool shops = true;
};

struct PaimonIconConfig {
    int schemaVersion = 1;

    ColorMode mode = ColorMode::Player;
    cocos2d::ccColor3B custom1   {255, 255, 255};
    cocos2d::ccColor3B custom2   {180, 180, 180};
    cocos2d::ccColor3B customGlow{255, 255, 255};
    float hueShiftDegrees = 0.0f;
    cocos2d::ccColor3B gradientStart{255,  64,  64};
    cocos2d::ccColor3B gradientEnd  { 64, 128, 255};
    cocos2d::ccColor3B monochromeBase{220, 100, 255};
    RandomPalette randomPalette = RandomPalette::Vibrant;
    float rainbowSpeed  = 1.0f;
    float rainbowSpread = 60.0f;

    LockStyle lockStyle     = LockStyle::Default;
    int dimOpacity          = 120;
    bool dimUnobtainable    = true;
    int unobtainableOpacity = 30;
    cocos2d::ccColor3B unobtainableTint{1, 1, 1};
    cocos2d::ccColor3B lockTint        {180, 180, 180};
    cocos2d::ccColor3B silhouetteColor { 41,  41,  41};

    ApplyToFlags apply{};
};

}  // namespace paimon::icons
