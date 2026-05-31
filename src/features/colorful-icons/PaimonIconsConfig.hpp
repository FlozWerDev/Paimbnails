#pragma once
//
// PaimonIconsConfig.hpp - Configuration data for the Paimon Icons recolor system.
//
// This is a pure data type (no Geode/Cocos dependencies beyond ccColor3B) so it
// can be serialized via matjson without pulling the whole UI graph.
//

#include <Geode/cocos/include/ccTypes.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace paimon::icons {

// ─────────────────────────────────────────────────────────────────────────────
// Strongly-typed enums. All stored as int in JSON for forward-compat.
// ─────────────────────────────────────────────────────────────────────────────

enum class ColorMode : int {
    Player        = 0,  // Use GameManager player colors (default).
    CustomRGB     = 1,  // User-defined fixed RGB triplet.
    HueShift      = 2,  // Player colors but rotated through hue space.
    SatBoost      = 3,  // Player colors with saturation/brightness multipliers.
    RandomStable  = 4,  // Deterministic per-icon color (hash-based, stable across sessions).
    Rainbow       = 5,  // Animated, time-driven rainbow cycling.
    Gradient      = 6,  // Linear interpolation between two colors over icon index.
    PerGamemode   = 7,  // Different palette per icon type (cube, ship, ball, …).
    Inverted      = 8,  // Complement of player colors (180° hue + invert).
    Monochrome    = 9,  // All icons share luminance variations of one base color.
};

enum class LockStyle : int {
    Default      = 0,  // Don't touch what GD does.
    ShowDimmed   = 1,  // Hide padlock, show real icon at reduced opacity.
    TintedLock   = 2,  // Padlock visible but recolored.
    Silhouette   = 3,  // Real icon as a flat solid-color silhouette.
    CustomMix    = 4,  // User chooses opacity + tint + lock visibility.
    HideBoth     = 5,  // Hide both padlock and icon (for placeholder UIs).
};

enum class RandomPalette : int {
    Vibrant     = 0,  // High saturation, full brightness.
    Pastel      = 1,  // Low saturation, high brightness.
    Neon        = 2,  // Vibrant with white-ish glow.
    Earthy      = 3,  // Warm browns / oranges / olives.
    Monoschemed = 4,  // Variations on the user-picked base.
};

// ─────────────────────────────────────────────────────────────────────────────
// Per-gamemode palette (used when ColorMode::PerGamemode).
// Index matches IconType (0..8: Cube, Ship, Ball, Ufo, Wave, Robot, Spider, Swing, Jetpack).
// nullopt entries fall back to the global player colors.
// ─────────────────────────────────────────────────────────────────────────────
struct GamemodePalette {
    cocos2d::ccColor3B color1{255, 255, 255};
    cocos2d::ccColor3B color2{180, 180, 180};
    cocos2d::ccColor3B glow {255, 255, 255};
};

// ─────────────────────────────────────────────────────────────────────────────
// "Apply To" toggles - which screens get recolored.
// ─────────────────────────────────────────────────────────────────────────────
struct ApplyToFlags {
    bool kit          = true;   // GJGarageLayer icon selection.
    bool shops        = true;   // Diamond/Mech/Path/Community/Gauntlet shops.
    bool achievements = true;   // AchievementsLayer rewards.
    bool rewards      = true;   // Chest/path reward popups.
    bool profiles     = false;  // Other-player profile pages (off by default - personal preference).
    bool comments     = false;  // CommentCell author icons.
    bool levelCells   = false;  // LevelCell author icons.
};

// ─────────────────────────────────────────────────────────────────────────────
// Animation toggles. Each one independent; some animations cost CPU per frame
// so they are off by default.
// ─────────────────────────────────────────────────────────────────────────────
struct AnimationFlags {
    bool pulseLocked       = false;  // Locked icons pulse opacity.
    float pulseSpeed       = 1.0f;   // Multiplier on the pulse cycle.
    bool hoverGlow         = false;  // Mouse-hover (PC) brightens the outline.
    bool idleFloat         = false;  // ±2px vertical sine bob.
    float floatAmount      = 2.0f;
    bool selectedHighlight = true;   // Currently-equipped icon gets a tinted ring.
    cocos2d::ccColor3B highlightColor{255, 215, 0};
    bool beatSync          = false;  // Pulse on FMOD spectrum peaks (CPU heavy).
};

// ─────────────────────────────────────────────────────────────────────────────
// Saved preset. Stored as raw matjson::Value (passed by string here so this
// header doesn't need to include matjson). The Store layer handles conversion.
// ─────────────────────────────────────────────────────────────────────────────
struct PresetEntry {
    std::string name;
    std::string serialized;  // matjson dump of the rest of the config (no presets in presets).
};

// ─────────────────────────────────────────────────────────────────────────────
// Top-level config blob. ALL fields default to a sane "vanilla-ish" state,
// so a fresh install behaves like Colorful Icons + a small UI nicety.
// ─────────────────────────────────────────────────────────────────────────────
struct PaimonIconConfig {
    int schemaVersion = 1;

    // Color
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
    float rainbowSpeed         = 1.0f;   // Hz multiplier.
    float rainbowSpread        = 60.0f;  // Per-icon hue offset spread (deg).
    std::array<std::optional<GamemodePalette>, 9> perMode{};

    // Locked
    LockStyle lockStyle               = LockStyle::ShowDimmed;
    int dimOpacity                    = 120;          // 0..255
    bool dimUnobtainable              = true;
    int unobtainableOpacity           = 30;
    cocos2d::ccColor3B unobtainableTint   {1, 1, 1};
    cocos2d::ccColor3B lockTint           {180, 180, 180};
    cocos2d::ccColor3B silhouetteColor    {41, 41, 41};

    // Animations
    AnimationFlags anim{};

    // Apply-to
    ApplyToFlags apply{};

    // Presets
    std::vector<PresetEntry> presets;
};

}  // namespace paimon::icons
