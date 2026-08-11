#pragma once
#include <Geode/Geode.hpp>
#include <vector>
#include <string>

namespace paimon::slider {

enum class SliderIconType : int {
    Cube, Ship, Ball, Ufo, Wave, Robot, Spider, Swing,
};

enum class SliderThumbMode : int {
    Icon,
    Image,
    Gif,
};

enum class SliderAnimType : int {
    None,
    Bounce,
    Rotate,
    BounceRotate,
};

struct SliderTargets {
    bool optionsSliders   = true;
    bool editorSliders    = true;
    bool colorSliders     = true;
    bool garageSliders    = false;
};

struct CustomSliderConfig {
    bool enabled = false;

    SliderThumbMode thumbMode = SliderThumbMode::Icon;

    SliderIconType iconType = SliderIconType::Cube;
    bool usePlayerIcon = true;
    int customIconId = 1;
    bool usePlayerColors = true;
    cocos2d::ccColor3B color1 = {0, 255, 100};
    cocos2d::ccColor3B color2 = {255, 255, 255};
    bool enableGlow = false;
    bool useGradients = false;

    std::string customImagePath;
    bool containerEnabled = true;
    std::string containerShape = "circle";
    bool containerBorderEnabled = false;
    cocos2d::ccColor3B containerBorderColor = {255, 255, 255};
    float containerBorderThickness = 2.0f;

    float iconScale = 0.55f;
    float iconRotation = 0.f;
    int   iconOpacity = 255;

    bool animateOnDrag = true;
    SliderAnimType animType = SliderAnimType::BounceRotate;
    float animBounceScale = 1.25f;
    float animRotateDeg = 22.f;
    float animDuration = 0.15f;

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
    void invalidateImageCache();

    bool shouldAffectSlider(cocos2d::CCNode* slider);
    cocos2d::CCNode* createThumbNode(bool isSelected = false);
    void addIconToNode(cocos2d::CCNode* baseNode, bool isSelected);
    std::filesystem::path imagesDir() const;

private:
    CustomSliderManager() = default;
    std::filesystem::path configPath() const;

    cocos2d::CCTexture2D* imageTexture();
    cocos2d::CCNode* createIconNode(bool isSelected);
    cocos2d::CCNode* createImageNode();
    cocos2d::CCNode* createGifNode(bool isSelected);

    CustomSliderConfig m_config;
    geode::Ref<cocos2d::CCTexture2D> m_imageTexture;
    std::string m_imageTexturePath;
};

} // namespace paimon::slider
