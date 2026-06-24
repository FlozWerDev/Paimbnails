#pragma once

#include <Geode/cocos/include/ccTypes.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace paimon::icons {

// All stored as int in JSON for forward-compat.
enum class ColorMode : int {
    Player        = 0,
    CustomRGB     = 1,
    HueShift      = 2,
    SatBoost      = 3,
    RandomStable  = 4,
    Rainbow       = 5,
    Gradient      = 6,
    PerGamemode   = 7,
    Inverted      = 8,
    Monochrome    = 9,
};

enum class LockStyle : int {
    Default      = 0,
    ShowDimmed   = 1,
    TintedLock   = 2,
    Silhouette   = 3,
    CustomMix    = 4,
    HideBoth     = 5,
};

enum class RandomPalette : int {
    Vibrant     = 0,
    Pastel      = 1,
    Neon        = 2,
    Earthy      = 3,
    Monoschemed = 4,
};

// Index matches IconType (0..8). nullopt entries fall back to global player colors.
struct GamemodePalette {
    cocos2d::ccColor3B color1{255, 255, 255};
    cocos2d::ccColor3B color2{180, 180, 180};
    cocos2d::ccColor3B glow {255, 255, 255};
};

struct ApplyToFlags {
    bool kit          = true;
    bool shops        = true;
    bool achievements = true;
    bool rewards      = true;
    bool profiles     = false;
    bool comments     = false;
    bool levelCells   = false;
};

struct AnimationFlags {
    bool pulseLocked       = false;
    float pulseSpeed       = 1.0f;
    bool hoverGlow         = false;
    bool idleFloat         = false;
    float floatAmount      = 2.0f;
    bool selectedHighlight = true;
    cocos2d::ccColor3B highlightColor{255, 215, 0};
    bool beatSync          = false;
};

struct PresetEntry {
    std::string name;
    std::string serialized;
};

struct PaimonIconConfig {
    int schemaVersion = 1;

    ColorMode mode             = ColorMode::Player;
    cocos2d::ccColor3B custom1     {255, 255, 255};
    cocos2d::ccColor3B custom2     {180, 180, 180};
    cocos2d::ccColor3B customGlow  {255, 255, 255};
    float hueShiftDegrees      = 0.0f;
    float saturationMul        = 1.0f;
    float brightnessMul        = 1.0f;
    cocos2d::ccColor3B gradientStart{255,  64,  64};
    cocos2d::ccColor3B gradientEnd  { 64, 128, 255};
    cocos2d::ccColor3B monochromeBase{220, 100, 255};
    RandomPalette randomPalette = RandomPalette::Vibrant;
    float rainbowSpeed         = 1.0f;
    float rainbowSpread        = 60.0f;
    std::array<std::optional<GamemodePalette>, 9> perMode{};

    LockStyle lockStyle               = LockStyle::ShowDimmed;
    int dimOpacity                    = 120;
    bool dimUnobtainable              = true;
    int unobtainableOpacity           = 30;
    cocos2d::ccColor3B unobtainableTint   {1, 1, 1};
    cocos2d::ccColor3B lockTint           {180, 180, 180};
    cocos2d::ccColor3B silhouetteColor    {41, 41, 41};

    AnimationFlags anim{};
    ApplyToFlags apply{};
    std::vector<PresetEntry> presets;
};

}  // namespace paimon::icons
