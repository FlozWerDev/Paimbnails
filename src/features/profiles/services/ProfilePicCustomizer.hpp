#pragma once
#include <Geode/DefaultInclude.hpp>
#include <string>
#include <vector>
#include <unordered_map>

struct PicFrameConfig {
    cocos2d::ccColor3B color = {255, 255, 255};
    float opacity = 255.f;
    float thickness = 4.f;
};

struct PicDecoration {
    std::string spriteName = "";
    float posX = 0.f;
    float posY = 0.f;
    float scale = 1.f;
    float rotation = 0.f;
    cocos2d::ccColor3B color = {255, 255, 255};
    float opacity = 255.f;
    bool flipX = false;
    bool flipY = false;
    int zOrder = 0;
};

enum class IconColorSource {
    Custom,
    Player
};

struct PicIconConfig {
    int iconId = 0;
    int iconType = 0;
    cocos2d::ccColor3B color1 = {255, 255, 255};
    cocos2d::ccColor3B color2 = {255, 255, 255};
    bool glowEnabled = false;
    cocos2d::ccColor3B glowColor = {255, 255, 0};
    float scale = 1.f;

    IconColorSource colorSource = IconColorSource::Custom;
    IconColorSource glowColorSource = IconColorSource::Custom;

    int animationType = 0;
    float animationSpeed = 1.f;
    float animationAmount = 1.f;

    bool iconImageEnabled = false;
    std::string iconImagePath;
};

struct PicCustomIcon {
    std::string spriteName;
    std::string path;
    bool isModAsset = false;
};

// name decoration applied to the profile username label everywhere it shows.
struct PicNameConfig {
    bool enabled = false;
    cocos2d::ccColor3B color = {255, 255, 255};

    // gradient: 0 none, 1 two-stop, 2 three-stop; animation names below.
    int gradientMode = 0;
    cocos2d::ccColor3B gradA = {255, 90, 160};
    cocos2d::ccColor3B gradB = {90, 170, 255};
    cocos2d::ccColor3B gradC = {255, 230, 120};
    std::string gradAnim = "flow";   // flow, pulse, rainbow, wave, static
    float gradSpeed = 1.f;

    bool outline = false;
    cocos2d::ccColor3B outlineColor = {0, 0, 0};
    bool glow = false;
    cocos2d::ccColor3B glowColor = {120, 200, 255};

    // per-letter motion: none, wave, bounce, jitter.
    std::string letterAnim = "none";
    float letterSpeed = 1.f;
    float letterAmount = 1.f;
};

struct ProfilePicConfig {
    float scaleX = 1.f;
    float scaleY = 1.f;
    float size = 120.f;
    float rotation = 0.f;

    // custom photo picked specifically for the profile button redesign.
    // kept completely separate from the profile popup's backdrop.
    std::string photoSource = "custom";
    std::string photoPath = "";

    float imageZoom = 1.f;
    float imageRotation = 0.f;
    float imageOffsetX = 0.f;
    float imageOffsetY = 0.f;
    bool imageFlipX = false;
    bool imageFlipY = false;
    float imageOpacity = 255.f;

    bool frameEnabled = false;
    PicFrameConfig frame;

    std::string stencilSprite = "circle";

    std::vector<PicDecoration> decorations;

    std::string profileFont = "goldFont.fnt";

    // hover effect shader id for the profile picture: "none" or profile_*.
    std::string hoverShader = "none";
    float hoverIntensity = 1.f;

    PicNameConfig nameConfig;

    bool onlyIconMode = false;
    PicIconConfig iconConfig;

    std::vector<PicCustomIcon> customIcons;
    int selectedCustomIconIndex = -1;
};

struct ProfilePicPreset {
    std::string id;
    std::string displayName;
    ProfilePicConfig config;
};

struct DecorationCategory {
    std::string id;
    std::string displayName;
    std::vector<std::pair<std::string, std::string>> decorations;
};

class ProfilePicCustomizer {
public:
    static ProfilePicCustomizer& get();

    ProfilePicConfig getConfig() const;
    void setConfig(ProfilePicConfig const& config);

    void save();
    void load();
    
    bool isDirty() const { return m_dirty; }
    void setDirty(bool dirty) { m_dirty = dirty; }

    static std::vector<std::pair<std::string, std::string>> getAvailableStencils();
    static std::vector<DecorationCategory> getDecorationCategories();
    static std::vector<ProfilePicPreset> getPresets();
    static std::vector<std::pair<std::string, cocos2d::ccColor3B>> getColorPalette();

    static std::vector<std::pair<std::string, std::string>> getAvailableFonts();

    // id -> label for the hover effect picker (first is "none").
    static std::vector<std::pair<std::string, std::string>> getHoverShaders();

    // name gradient animation ids and letter animation ids.
    static std::vector<std::pair<std::string, std::string>> getNameGradientAnims();
    static std::vector<std::pair<std::string, std::string>> getNameLetterAnims();

private:
    ProfilePicCustomizer();
    ProfilePicConfig m_config;
    bool m_loaded = false;
    bool m_dirty = false;
};
