#pragma once
#include <Geode/Geode.hpp>
#include <vector>
#include <string>

// ────────────────────────────────────────────────────────────
// CustomSliderManager: replaces the default slider thumb sprite
// with the player's selected game icon, a custom image, or an
// animated GIF across configurable slider locations in the game UI.
// ────────────────────────────────────────────────────────────

namespace paimon::slider {

// Which icon type to display on the slider thumb.
enum class SliderIconType : int {
    Cube    = 0,
    Ship    = 1,
    Ball    = 2,
    Ufo     = 3,
    Wave    = 4,
    Robot   = 5,
    Spider  = 6,
    Swing   = 7,
};

// Source mode for the slider thumb visual.
enum class SliderThumbMode : int {
    Icon    = 0,   // Game icon (SimplePlayer)
    Image   = 1,   // Static image (PNG/JPG)
    Gif     = 2,   // Animated GIF
};

// Animation type when dragging the slider.
enum class SliderAnimType : int {
    None        = 0,   // No animation
    Bounce      = 1,   // Scale up/down on press
    Rotate      = 2,   // Rotate based on drag direction
    BounceRotate = 3,  // Both bounce and rotate
};

// Which slider locations should be affected.
struct SliderTargets {
    bool optionsSliders   = true;   // volume, SFX, etc. in options
    bool editorSliders    = true;   // editor UI sliders
    bool colorSliders     = true;   // HSV / color picker sliders
    bool garageSliders    = false;  // icon selection (if any)
};

struct CustomSliderConfig {
    bool enabled = false;

    // Which mode to use for the thumb visual
    SliderThumbMode thumbMode = SliderThumbMode::Icon;

    // ── Icon mode settings ──
    SliderIconType iconType = SliderIconType::Cube;
    bool usePlayerIcon = true;
    int customIconId = 1;
    bool usePlayerColors = true;
    cocos2d::ccColor3B color1 = {0, 255, 100};
    cocos2d::ccColor3B color2 = {255, 255, 255};
    bool enableGlow = false;

    // ── Image/GIF mode settings ──
    std::string customImagePath;  // absolute path to PNG/JPG/GIF

    // ── Container shape (Image/GIF only) ──
    // Wraps the image inside a shape (circle, square, etc.) so it doesn't
    // appear "alone" — same look the profile button uses.
    bool containerEnabled = true;                            // default on
    std::string containerShape = "circle";                   // see ShapeStencil.hpp
    bool containerBorderEnabled = false;                     // optional outline
    cocos2d::ccColor3B containerBorderColor = {255, 255, 255};
    float containerBorderThickness = 2.0f;                   // 1..6 px

    // ── Visual tweaks (all modes) ──
    float iconScale = 0.55f;       // 0.2 – 1.5
    float iconRotation = 0.f;      // degrees
    int   iconOpacity = 255;       // 0 – 255

    // ── Animation settings ──
    bool animateOnDrag = true;     // enable animations
    SliderAnimType animType = SliderAnimType::BounceRotate;
    float animBounceScale = 1.25f; // scale multiplier on drag (1.0 = no change)
    float animRotateDeg = 22.f;    // max rotation degrees
    float animDuration = 0.15f;    // animation duration in seconds

    // Which sliders to affect
    SliderTargets targets;
};

class CustomSliderManager {
public:
    static CustomSliderManager& get();

    void loadConfig();
    void saveConfig();

    CustomSliderConfig& config() { return m_config; }
    CustomSliderConfig const& config() const { return m_config; }

    void resetToDefaults();

    // Called by the hook to apply the custom thumb to a SliderThumb node.
    // Returns true if the thumb was modified.
    bool applyCustomThumb(cocos2d::CCNode* sliderThumb);

    // Restores the original thumb sprite (called when feature is disabled).
    void restoreOriginalThumb(cocos2d::CCNode* sliderThumb);

    // Determines if a given slider should be affected based on context.
    bool shouldAffectSlider(cocos2d::CCNode* slider);

    // Starts the drag animation on the custom icon node.
    void startDragAnimation(cocos2d::CCNode* sliderThumb);

    // Stops the drag animation (returns to rest state).
    void stopDragAnimation(cocos2d::CCNode* sliderThumb);

    // Adds the icon/image/gif to a base node (used by the hook to build
    // normalImage and selectedImage for the SliderThumb).
    // If `isSelected` is true, may apply lighter colors for pressed state.
    void addIconToNode(cocos2d::CCNode* baseNode, bool isSelected);

    // Returns the gallery directory for custom slider images.
    std::filesystem::path imagesDir() const;

private:
    CustomSliderManager() = default;
    std::filesystem::path configPath() const;

    // Creates a SimplePlayer icon node configured per current settings.
    cocos2d::CCNode* createIconNode();

    // Creates a sprite from a custom image file.
    cocos2d::CCNode* createImageNode();

    // Creates an animated GIF sprite from a file.
    cocos2d::CCNode* createGifNode();

    // Creates the appropriate thumb node based on current mode.
    cocos2d::CCNode* createThumbNode();

    CustomSliderConfig m_config;
};

} // namespace paimon::slider
