#pragma once

// Click effects for the live cursor and its preview.

#include <Geode/Geode.hpp>
#include "CursorFxCommon.hpp"
#include "CursorTrailFX.hpp"
#include "CursorTransitionFX.hpp"
#include <array>
#include <vector>

namespace paimon::cursorfx {

enum class ClickBurst : int {
    None = 0,
    Ripple,
    Shockwave,
    Hearts,
    Stars,
    Confetti,
    Sparks,
    Firework,
    Bubbles,
    Snow,
    Ink,
    Lightning,
    MagicCircle,
    Pixels,
    Smoke,
    Coins,
    Notes,
    Petals,
    Bloom,
    Galaxy,
    Fireball,
    Count
};

enum class ClickHold : int {
    None = 0,
    Aura,
    ChargeRing,
    Orbit,
    Vortex,
    Drip,
    Electric,
    Flame,
    Sparkle,
    Pulse,
    Count
};

enum class ClickAnim : int {
    None = 0,
    Squash,
    Pop,
    Sink,
    Tilt,
    Spin,
    Shake,
    Bounce,
    Wobble,
    Count
};

enum class ClickSound : int {
    None = 0,
    Click, Pop, Coin, Crystal, Achievement,
    Explosion, Magic, Key, Chest, Score,
    Count
};

inline constexpr int kClickBurstCount = static_cast<int>(ClickBurst::Count);
inline constexpr int kClickHoldCount  = static_cast<int>(ClickHold::Count);
inline constexpr int kClickAnimCount  = static_cast<int>(ClickAnim::Count);
inline constexpr int kClickSoundCount = static_cast<int>(ClickSound::Count);

inline constexpr float kClickSizeMin    = 0.30f;
inline constexpr float kClickSizeMax    = 3.00f;
inline constexpr float kClickAmountMin  = 0.20f;
inline constexpr float kClickAmountMax  = 3.00f;
inline constexpr float kClickLifeMin    = 0.15f;
inline constexpr float kClickLifeMax    = 2.50f;
inline constexpr float kClickSpreadMin  = 0.20f;
inline constexpr float kClickSpreadMax  = 2.50f;
inline constexpr float kClickAnimMin    = 0.10f;
inline constexpr float kClickAnimMax    = 2.00f;
inline constexpr float kClickAnimDurMin = 0.05f;
inline constexpr float kClickAnimDurMax = 0.60f;
inline constexpr float kClickPitchMin   = 0.50f;
inline constexpr float kClickPitchMax   = 2.00f;
inline constexpr float kClickTuneMin    = 0.20f;
inline constexpr float kClickTuneMax    = 4.00f;

struct ClickEffectTuning {
    float size  = 1.f;
    float speed = 1.f;
};

struct ClickSettings {
    ClickBurst press   = ClickBurst::Ripple;
    ClickBurst release = ClickBurst::None;
    ClickHold  hold    = ClickHold::None;
    ClickAnim  anim    = ClickAnim::Squash;
    ClickSound pressSound   = ClickSound::None;
    ClickSound releaseSound = ClickSound::None;

    TrailColorMode colorMode = TrailColorMode::Solid;
    cocos2d::ccColor3B color1 = {255, 120, 170};
    cocos2d::ccColor3B color2 = {110, 200, 255};
    float hueSpeed = 1.0f;

    float size    = 1.0f;
    float amount  = 1.0f;
    float life    = 0.75f;
    float spread  = 1.0f;
    int   opacity = 235;
    bool  glow    = true;

    float animStrength = 1.0f;
    float animDuration = 0.18f;

    float volume = 0.55f;
    float pitch  = 1.0f;
    bool  randomPitch = false;

    bool  rightClick = true;

    std::array<ClickEffectTuning, kClickBurstCount> burstTuning{};
    std::array<ClickEffectTuning, kClickHoldCount>  holdTuning{};
};

char const* clickBurstName(ClickBurst effect);
char const* clickBurstDesc(ClickBurst effect);
char const* clickHoldName(ClickHold effect);
char const* clickHoldDesc(ClickHold effect);
char const* clickAnimName(ClickAnim anim);
char const* clickAnimDesc(ClickAnim anim);
char const* clickSoundName(ClickSound sound);

struct ClickPreset {
    char const*   name;
    ClickSettings settings;
};

int clickPresetCount();
ClickPreset const& clickPresetAt(int index);
int findClickPreset(ClickSettings const& settings);

void playClickSound(ClickSound sound, float volume, float pitch, bool randomPitch);

TransitionFrame sampleClickAnim(ClickAnim anim, float t, float duration,
                                float strength, bool held);

class CursorClickNode : public cocos2d::CCNode {
public:
    static CursorClickNode* create();

    void applySettings(ClickSettings const& s);
    ClickSettings const& settings() const { return m_cfg; }

    void press(cocos2d::CCPoint const& pos);
    void release(cocos2d::CCPoint const& pos);
    void step(float dt, cocos2d::CCPoint const& pos, bool held);
    void reset();

    // Draw only during the overlay pass to avoid doubling additive glow.
    void beginOverlayPass();
    void endOverlayPass() { m_inOverlayPass = false; }
    void visit() override;

private:
    CursorClickNode() = default;
    ~CursorClickNode() override = default;
    bool init() override;

    struct Particle {
        cocos2d::CCSprite* spr = nullptr;
        int   texKind = -1;
        cocos2d::CCPoint pos, vel;
        float life = 0.f, maxLife = 1.f;
        float baseSize = 1.f, growth = 1.f;
        float rot = 0.f, spin = 0.f;
        float swayPhase = 0.f, swayAmp = 0.f;
        float gravity = 0.f, drag = 0.f;
        float twinkle = 0.f;
        float spiral = 0.f, spiralRadius = 0.f, spiralAngle = 0.f;
        float fadeIn = 0.05f;
        float rnd = 0.f;
        bool  alignVel = false;
        bool  homing = false;
        cocos2d::ccColor3B color = {255, 255, 255};
        bool  alive = false;
    };

    struct Ring {
        cocos2d::CCPoint pos;
        float age = 0.f, maxAge = 0.5f;
        float startR = 2.f, endR = 40.f;
        float thickness = 3.f;
        float spin = 0.f;
        float scale = 1.f;
        int   kind = 0;
        cocos2d::ccColor3B color = {255, 255, 255};
        bool  alive = false;
    };

    struct Bolt {
        cocos2d::CCPoint pos;
        float age = 0.f, maxAge = 0.2f;
        float angle = 0.f, length = 40.f;
        float scale = 1.f;
        unsigned int seed = 1u;
        cocos2d::ccColor3B color = {255, 255, 255};
        bool  alive = false;
    };

    FxDrawBatch* m_draw = nullptr;
    std::array<cocos2d::CCSpriteBatchNode*, TexCount> m_batches{};

    ClickSettings m_cfg;

    std::vector<Particle> m_particles;
    std::vector<Ring>     m_rings;
    std::vector<Bolt>     m_bolts;

    cocos2d::CCPoint m_pos;
    float m_time      = 0.f;
    float m_holdTime  = 0.f;
    float m_holdCredit = 0.f;
    float m_pulseTimer = 0.f;
    float m_chargeTime = 0.f;
    unsigned int m_boltSeed = 1u;
    bool  m_held      = false;

    bool m_inOverlayPass   = false;
    bool m_overlayPassSeen = false;

    cocos2d::ccBlendFunc spriteBlend() const;
    cocos2d::CCSpriteBatchNode* ensureBatch(int texKind);
    void refreshBlend();

    Particle* acquire(int texKind);
    Ring*     acquireRing();
    Bolt*     acquireBolt();

    void spawnBurst(ClickBurst effect, cocos2d::CCPoint const& pos);
    void stepHold(float dt);
    void updateParticles(float dt);
    void updateRings(float dt);
    void updateBolts(float dt);
    void redraw();

    cocos2d::ccColor3B resolveColor(float t, float rnd) const;
    float holdTuneSize() const;
    float holdTuneSpeed() const;
};

}
