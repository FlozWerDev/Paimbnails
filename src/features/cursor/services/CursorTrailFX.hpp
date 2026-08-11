#pragma once

// Cursor trails for the live cursor and its preview.

#include <Geode/Geode.hpp>
#include <deque>
#include <vector>

namespace paimon::cursorfx {

class FxDrawBatch;

enum class TrailEffect : int {
    Ribbon = 0,
    Neon,
    Comet,
    Ink,
    Dots,
    Lightning,
    Sparks,
    Fire,
    Frost,
    Bubbles,
    Stars,
    Hearts,
    Smoke,
    Confetti,
    Magic,
    Galaxy,
    Orbit,
    Echo,
    Count
};

enum class TrailColorMode : int {
    Solid = 0,
    Gradient,
    RainbowCycle,
    RainbowTrail,
    Random,
    Speed,
    Count
};

inline constexpr int kEffectCount    = static_cast<int>(TrailEffect::Count);
inline constexpr int kColorModeCount = static_cast<int>(TrailColorMode::Count);

inline constexpr float kLifeMin     = 0.10f;
inline constexpr float kLifeMax     = 2.50f;
inline constexpr float kSizeMin     = 1.00f;
inline constexpr float kSizeMax     = 26.0f;
inline constexpr float kDensityMin  = 0.20f;
inline constexpr float kDensityMax  = 3.00f;
inline constexpr float kHueSpeedMin = 0.10f;
inline constexpr float kHueSpeedMax = 4.00f;

struct TrailSettings {
    TrailEffect    effect    = TrailEffect::Ribbon;
    TrailColorMode colorMode = TrailColorMode::Solid;
    cocos2d::ccColor3B color1 = {255, 255, 255};
    cocos2d::ccColor3B color2 = {  0, 190, 255};
    float life     = 0.55f;
    float size     = 5.0f;
    float density  = 1.0f;
    int   opacity  = 200;
    bool  glow     = true;
    float hueSpeed = 1.0f;
};

char const* effectName(TrailEffect e);
char const* effectDesc(TrailEffect e);
char const* colorModeName(TrailColorMode m);
char const* colorModeDesc(TrailColorMode m);

bool colorModeUsesColor1(TrailColorMode m);
bool colorModeUsesColor2(TrailColorMode m);

struct TrailPreset {
    char const*   name;
    TrailSettings settings;
};

int presetCount();
TrailPreset const& presetAt(int index);
int findPresetIndex(TrailSettings const& s);

class CursorTrailNode : public cocos2d::CCNode {
public:
    static CursorTrailNode* create();

    void applySettings(TrailSettings const& s);
    TrailSettings const& settings() const { return m_cfg; }

    void step(float dt, cocos2d::CCPoint const& pos);
    void reset();

    void setEchoSource(cocos2d::CCSprite* src);

    // Draw only during the overlay pass to avoid doubling additive glow.
    void beginOverlayPass();
    void endOverlayPass() { m_inOverlayPass = false; }

    void visit() override;

    static void releaseSharedTextures();
    static void abandonSharedTextures();

private:
    CursorTrailNode() = default;
    ~CursorTrailNode() override;
    bool init() override;

    struct TrailPoint {
        cocos2d::CCPoint pos;
        float age       = 0.f;
        float speedNorm = 0.f;
        float rnd       = 0.f;
    };

    struct Particle {
        cocos2d::CCSprite* spr = nullptr;
        cocos2d::CCPoint pos, vel;
        float life = 0.f, maxLife = 1.f;
        float baseSize = 1.f, growth = 1.f;
        float rot = 0.f, spin = 0.f;
        float swayPhase = 0.f, swayAmp = 0.f;
        float gravity = 0.f, drag = 0.f;
        float twinkle = 0.f;
        float spiral = 0.f, spiralRadius = 0.f, spiralAngle = 0.f;
        float fadeIn = 0.15f;
        float rnd = 0.f;
        cocos2d::ccColor3B color = {255, 255, 255};
        bool ageColor = false;
        bool alive = false;
    };

    struct Echo {
        cocos2d::CCSprite* spr = nullptr;
        float life = 0.f, maxLife = 0.4f;
        float baseScaleX = 1.f;
        float baseScaleY = 1.f;
        bool alive = false;
    };

    FxDrawBatch*                m_draw   = nullptr;
    cocos2d::CCSpriteBatchNode* m_batch  = nullptr;
    cocos2d::CCNode*            m_echoes = nullptr;

    TrailSettings m_cfg;
    int  m_batchTexKind = -1;

    std::deque<TrailPoint> m_points;
    std::vector<Particle>  m_particles;
    std::vector<Echo>      m_echoPool;

    cocos2d::CCPoint m_lastPos;
    cocos2d::CCPoint m_smoothVel;
    bool  m_hasLastPos   = false;
    float m_time         = 0.f;
    float m_spawnCredit  = 0.f;
    float m_echoCredit   = 0.f;
    float m_boltTimer    = 0.f;
    float m_speedNorm    = 0.f;
    unsigned int m_boltSeed = 1u;

    geode::Ref<cocos2d::CCSprite> m_echoSource = nullptr;
    cocos2d::CCSprite* m_head = nullptr;

    std::vector<cocos2d::CCPoint> m_chain;
    cocos2d::CCPoint m_orbitCenter;
    float m_orbitAngle = 0.f;

    bool m_inOverlayPass = false;
    bool m_overlayPassSeen = false;

    void rebuildForEffect();
    void ensureBatch(int texKind);
    void pushPoint(cocos2d::CCPoint const& pos, float speedNorm);
    void agePoints(float dt);
    void updateParticles(float dt);
    void updateEchoes(float dt);
    void emit(float dt, cocos2d::CCPoint const& from, cocos2d::CCPoint const& to);
    void spawnEcho(cocos2d::CCPoint const& pos);
    Particle* acquireParticle();
    void redraw();

    struct RibbonVert {
        cocos2d::CCPoint pos;
        float t = 0.f;
        float half = 1.f;
        cocos2d::ccColor3B color = {255, 255, 255};
        float alpha = 1.f;
    };
    std::vector<RibbonVert> m_ribbon;

    void buildRibbonVerts();
    void strokeRibbon(float widthMul, float alphaMul, bool forceWhite);
    void drawLightning();
    void drawDots();
    void drawOrbit();

    cocos2d::ccColor3B resolveColor(float t, float rnd, float speedNorm) const;
};

}
