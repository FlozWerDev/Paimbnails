#pragma once
#include <Geode/DefaultInclude.hpp>
#include <string>
#include <vector>
#include <unordered_map>

struct PicFrameConfig {
    std::string spriteFrame = "";
    cocos2d::ccColor3B color = {255, 255, 255};
    float opacity = 255.f;
    float thickness = 4.f;
    float offsetX = 0.f;
    float offsetY = 0.f;
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
    float iconImageScale = 1.f;
};

struct PicCustomIcon {
    std::string spriteName;
    std::string path;
    bool isModAsset = false;
};

struct ProfilePicConfig {
    float scaleX = 1.f;
    float scaleY = 1.f;
    float size = 120.f;
    float rotation = 0.f;

    // "profile" = own profile picture (profileimg), with the legacy profile
    // background image as fallback; "custom" = a locally picked file (photoPath)
    std::string photoSource = "profile";
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

    float offsetX = 0.f;
    float offsetY = 0.f;

    std::string profileFont = "goldFont.fnt";

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

    static std::vector<std::pair<std::string, std::string>> getAvailableFrames();
    static std::vector<std::pair<std::string, std::string>> getAvailableStencils();
    static std::vector<std::pair<std::string, std::string>> getAvailableDecorations();
    static std::vector<DecorationCategory> getDecorationCategories();
    static std::vector<ProfilePicPreset> getPresets();
    static std::vector<std::pair<std::string, cocos2d::ccColor3B>> getColorPalette();

    static std::vector<std::pair<int, std::string>> getAvailableGameIcons();
    static std::vector<std::pair<std::string, std::string>> getAvailableFonts();

private:
    ProfilePicCustomizer();
    ProfilePicConfig m_config;
    bool m_loaded = false;
    bool m_dirty = false;
};
