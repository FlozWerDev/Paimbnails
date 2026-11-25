#pragma once
#include "../../../ui/PaimonPopup.hpp"
#include <Geode/Geode.hpp>
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

class PlayLayer;
class PlayerObject;

namespace paimon::death_effects {

inline constexpr int kDeathStyleCount = 40;
inline constexpr int kStyleRandom = 40;
inline constexpr int kStyleCustomMedia = 41;

enum class DeathColorMode : int {
    PlayerPrimary   = 0,
    PlayerSecondary = 1,
    DualGradient    = 2,
    Custom          = 3,
    Rainbow         = 4,
};

enum class DeathParticleShape : int {
    Sparks    = 0,
    Squares   = 1,
    Stars     = 2,
    Rings     = 3,
    Lines     = 4,
    Triangles = 5,
    Confetti  = 6,
    None      = 7,
};

enum class DeathParticlePhysics : int {
    Explode = 0,
    Gravity = 1,
    FloatUp = 2,
    Swirl   = 3,
};

enum class DeathCameraZoom : int {
    None       = 0,
    PunchIn    = 1,
    RecoilOut  = 2,
    DramaticIn = 3,
};

enum class DeathCameraTilt : int {
    None     = 0,
    Subtle   = 1,
    HardKick = 2,
};

enum class DeathScreenFx : int {
    None              = 0,
    GDShockwave       = 1,
    GDShockLine       = 2,
    GDChromaticGlitch = 3,
    GDRadialBlur      = 4,
    GDBulge           = 5,
    GDPinch           = 6,
    GDInvert          = 7,
    GDLensCircle      = 8,
    GDColorShift      = 9,
    VignettePulse     = 10,
    ImpactFlash       = 11,
};

struct DeathStyleSettings {
    DeathColorMode colorMode = DeathColorMode::DualGradient;
    cocos2d::ccColor3B customPrimary = {90, 225, 255};
    cocos2d::ccColor3B customSecondary = {255, 110, 210};
    float scale = 1.0f;
    float speed = 1.0f;
    float intensity = 1.0f;
    float spin = 0.0f;
    bool keepVanillaExplosion = false;

    DeathParticleShape particleShape = DeathParticleShape::Sparks;
    DeathParticlePhysics particlePhysics = DeathParticlePhysics::Explode;
    int particleCount = 18;
    bool enableShockRing = true;
    bool enableGhostIcon = false;

    float cameraShake = 0.25f;
    DeathCameraZoom cameraZoom = DeathCameraZoom::None;
    DeathCameraTilt cameraTilt = DeathCameraTilt::None;

    DeathScreenFx screenFx = DeathScreenFx::None;
    float screenFxStrength = 0.65f;
    float screenFxDuration = 0.45f;
};

struct DeathVfxConfig {
    // -1: Original vanilla, 0..39: 40 shader+FX styles, 40: Random, 41: Custom PNG/GIF burst
    int style = -1;
    std::string customMediaPath;
    DeathStyleSettings activeSettings;
    std::unordered_map<int, DeathStyleSettings> styleOverrides;
};

inline constexpr std::array<char const*, 40> kAnimationNames = {
    // 0..7 Cosmic
    "Supernova", "Black Hole", "Solar Flare", "Plasma Vortex",
    "Nebula Burst", "Quasar Beam", "Eclipse Pulse", "Wormhole Tear",
    // 8..15 Cyber
    "Cyber Glitch", "Pixel Shatter", "VHS Static", "Matrix Rain",
    "CRT Shutdown", "Neon Cross", "Laser Grid", "Holo Fracture",
    // 16..23 Elemental
    "Inferno Embers", "Cryo Crystal", "Lightning", "Sakura Lotus",
    "Tsunami Wave", "Tornado Spiral", "Magma Crack", "Aurora Ribbon",
    // 24..31 Impact
    "Prism Shatter", "Shockwaves", "Hexagon Pulse", "Origami Fold",
    "Glass Break", "Comic Pow", "Gravity Implode", "Sonic Boom",
    // 32..39 Cinema
    "Dim. Rift", "Stardust Nova", "Void Snap", "Time Freeze",
    "Soul Ascend", "Phantom Echo", "Blood Moon", "Arcade KO"
};

DeathStyleSettings defaultStyleSettings(int style);
DeathVfxConfig& vfxConfig();
DeathStyleSettings& currentStyleSettings();
void loadVfxConfig();
void saveVfxConfig();
void resetCurrentStyleToDefault();

// Core playback API
int selectedAnimation();
int resolveAnimation(int selection);
void prewarmAnimation();
bool shouldSuppressVanillaExplosion();

bool spawnAnimation(cocos2d::CCNode* parent, cocos2d::CCPoint position, int style,
                    cocos2d::ccColor3B color, float scale = 1.f);
bool spawnAnimationDual(cocos2d::CCNode* parent, cocos2d::CCPoint position, int style,
                        cocos2d::ccColor3B primaryColor, cocos2d::ccColor3B secondaryColor,
                        float scale = 1.f);

// Full gameplay trigger: shader + particles + ghost echo + camera shake/zoom/tilt + native GD ShaderLayer + screen overlays
bool triggerGameplayDeathEffects(PlayLayer* play, PlayerObject* player, cocos2d::CCPoint positionInObjLayer);
void clearAnimations(cocos2d::CCNode* parent);
void clearGameplayDeathEffects(PlayLayer* play);

class DeathAnimationPopup : public PaimonPopup {
public:
    static DeathAnimationPopup* create();

protected:
    bool init() override;
    void onExit() override;
    void update(float dt) override;
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;

private:
    void buildStylesPane(cocos2d::CCNode* pane);
    void buildVisualPane(cocos2d::CCNode* pane);
    void buildParticlesPane(cocos2d::CCNode* pane);
    void buildCamFxPane(cocos2d::CCNode* pane);
    void buildPreviewStage();

    void rebuildStyleGrid();
    void refreshUI();
    void triggerPreviewAt(cocos2d::CCPoint localInStage);

    void onSubTabSwitch(cocos2d::CCObject* sender);
    void onSelectCategory(cocos2d::CCObject* sender);
    void onSelectStyle(cocos2d::CCObject* sender);
    void onReplayPreview(cocos2d::CCObject*);
    void onToggleAutoLoop(cocos2d::CCObject*);
    void onRandomizeAll(cocos2d::CCObject*);
    void onOpenDeathSfx(cocos2d::CCObject*);
    void onResetStyle(cocos2d::CCObject*);

    // Visual pane
    void onCycleColorMode(cocos2d::CCObject*);
    void onPickPrimary(cocos2d::CCObject*);
    void onPickSecondary(cocos2d::CCObject*);
    void onScaleChanged(cocos2d::CCObject*);
    void onSpeedChanged(cocos2d::CCObject*);
    void onIntensityChanged(cocos2d::CCObject*);
    void onSpinChanged(cocos2d::CCObject*);
    void onToggleKeepVanilla(cocos2d::CCObject*);
    void onPickCustomMedia(cocos2d::CCObject*);
    void onClearCustomMedia(cocos2d::CCObject*);

    // Particles pane
    void onCyclePartShape(cocos2d::CCObject*);
    void onCyclePartPhysics(cocos2d::CCObject*);
    void onPartCountChanged(cocos2d::CCObject*);
    void onToggleShockRing(cocos2d::CCObject*);
    void onToggleGhostIcon(cocos2d::CCObject*);

    // Cam & Shader FX pane
    void onCamShakeChanged(cocos2d::CCObject*);
    void onCycleCamZoom(cocos2d::CCObject*);
    void onCycleCamTilt(cocos2d::CCObject*);
    void onCycleScreenFx(cocos2d::CCObject*);
    void onScreenFxStrengthChanged(cocos2d::CCObject*);
    void onScreenFxDurationChanged(cocos2d::CCObject*);

    int m_subTab = 0;
    int m_categoryFilter = 0; // 0=All, 1=Cosmic, 2=Cyber, 3=Elem, 4=Impact, 5=Cine
    std::array<CCMenuItemSpriteExtra*, 4> m_subTabBtns{};
    std::array<cocos2d::CCNode*, 4> m_subPanes{};
    std::vector<CCMenuItemSpriteExtra*> m_catBtns;

    geode::ScrollLayer* m_styleScroll = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_styleButtons;

    cocos2d::CCClippingNode* m_stageClip = nullptr;
    cocos2d::CCNode* m_stageCamNode = nullptr;
    cocos2d::CCNode* m_stageNode = nullptr;
    cocos2d::CCRect m_stageRect{};
    cocos2d::CCLabelBMFont* m_previewHint = nullptr;
    cocos2d::CCLabelBMFont* m_previewFxBadge = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    bool m_autoLoop = true;
    float m_loopTimer = 0.f;
    float m_stageShakeTime = 0.f;
    float m_stageShakeMag = 0.f;

    // Visual controls
    CCMenuItemSpriteExtra* m_colorModeBtn = nullptr;
    cocos2d::CCSprite* m_primarySwatch = nullptr;
    cocos2d::CCSprite* m_secondarySwatch = nullptr;
    Slider* m_scaleSlider = nullptr;     cocos2d::CCLabelBMFont* m_scaleLbl = nullptr;
    Slider* m_speedSlider = nullptr;     cocos2d::CCLabelBMFont* m_speedLbl = nullptr;
    Slider* m_intensitySlider = nullptr; cocos2d::CCLabelBMFont* m_intensityLbl = nullptr;
    Slider* m_spinSlider = nullptr;      cocos2d::CCLabelBMFont* m_spinLbl = nullptr;
    CCMenuItemToggler* m_keepVanillaToggle = nullptr;
    cocos2d::CCLabelBMFont* m_mediaPathLbl = nullptr;

    // Particles controls
    CCMenuItemSpriteExtra* m_partShapeBtn = nullptr;
    CCMenuItemSpriteExtra* m_partPhysBtn = nullptr;
    Slider* m_partCountSlider = nullptr; cocos2d::CCLabelBMFont* m_partCountLbl = nullptr;
    CCMenuItemToggler* m_shockRingToggle = nullptr;
    CCMenuItemToggler* m_ghostIconToggle = nullptr;

    // Cam & FX controls
    Slider* m_camShakeSlider = nullptr;  cocos2d::CCLabelBMFont* m_camShakeLbl = nullptr;
    CCMenuItemSpriteExtra* m_camZoomBtn = nullptr;
    CCMenuItemSpriteExtra* m_camTiltBtn = nullptr;
    CCMenuItemSpriteExtra* m_screenFxBtn = nullptr;
    Slider* m_fxStrengthSlider = nullptr; cocos2d::CCLabelBMFont* m_fxStrengthLbl = nullptr;
    Slider* m_fxDurSlider = nullptr;      cocos2d::CCLabelBMFont* m_fxDurLbl = nullptr;
};

} // namespace paimon::death_effects
