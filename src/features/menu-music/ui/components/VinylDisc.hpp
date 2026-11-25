#pragma once

#include <Geode/Geode.hpp>
#include <string>
#include <vector>
#include <optional>

namespace paimon::menumusic {

enum class VinylStyle {
    ClassicBlack = 0,
    GoldRecord = 1,
    PlatinumChrome = 2,
    RetroCD = 3,
    CyberpunkNeon = 4,
    GlassClear = 5,
    RubyMarbled = 6,
    AuroraCosmic = 7,
    Count = 8
};

class VinylDisc : public cocos2d::CCNode {
public:
    using cocos2d::CCNode::init;
    static VinylDisc* create(float radius, std::optional<VinylStyle> style = std::nullopt);
    ~VinylDisc() override;

    void setCoverFromPath(const std::string& absolutePath);
    void clearCover();

    void startSpinning();
    void stopSpinning();
    bool isSpinning() const { return m_spinning; }

    void setSpinSpeed(float degPerSec) { m_spinSpeed = degPerSec; }
    float getSpinSpeed() const { return m_spinSpeed; }

    void setPausedAppearance(bool paused);

    void setStyle(VinylStyle style);
    VinylStyle getStyle() const { return m_style; }

    void setAnisotropicGleam(bool enabled);
    bool isAnisotropicGleam() const { return m_anisotropicGleam; }

    void setDetailedGrooves(bool enabled);
    bool isDetailedGrooves() const { return m_detailedGrooves; }

    void setLabelSizeRatio(float ratio);
    float getLabelSizeRatio() const { return m_labelRatio; }

    void reloadAppearance();

    void onExit() override;

    static VinylStyle getSavedStyle();
    static void setSavedStyle(VinylStyle style);
    static std::vector<std::string> const& getStyleNames();
    static char const* getStyleDescription(VinylStyle style);
    static void reloadAllInstances();
    static void clearTextureCache();

protected:
    bool init(float radius, std::optional<VinylStyle> style);
    void tick(float dt);
    void applyCoverTexture(cocos2d::CCTexture2D* texture);

    void buildGeometry();
    void buildAnisotropicSheen();

    float m_radius = 60.f;
    float m_spinSpeed = 40.f;
    float m_currentSpinSpeed = 0.f;
    bool m_spinning = false;
    VinylStyle m_style = VinylStyle::ClassicBlack;
    bool m_anisotropicGleam = true;
    bool m_detailedGrooves = true;
    float m_labelRatio = 0.46f;

    cocos2d::CCNode* m_shadowNode = nullptr;
    cocos2d::CCNode* m_rotating = nullptr;
    cocos2d::CCNode* m_discBody = nullptr;
    cocos2d::CCClippingNode* m_coverClip = nullptr;
    cocos2d::CCSprite* m_coverSprite = nullptr;
    cocos2d::CCNode* m_spindleNode = nullptr;
    cocos2d::CCNode* m_sheenNode = nullptr;

    std::string m_coverPath;
    cocos2d::CCTexture2D* m_currentCoverTexture = nullptr;
    std::uint64_t m_coverGeneration = 0;
    bool m_coverLoading = false;
    bool m_paused = false;
};

} // namespace paimon::menumusic
