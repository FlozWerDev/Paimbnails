#include "CursorTrailFX.hpp"
#include "CursorFxCommon.hpp"

#include <Geode/utils/cocos.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::cursorfx {

namespace {

// Cap live particles to keep dense 240 Hz effects bounded.
constexpr int kMaxParticles = 220;
constexpr int kMaxEchoes    = 24;
// History is time-based, with a hard cap for high frame rates.
constexpr size_t kMaxPoints = 220;


struct EmitterSpec {
    int   tex        = TexDot;
    float rate       = 0.f;    // particles/s while moving
    float idleRate   = 0.f;    // particles/s while idle
    float speedMin   = 0.f, speedMax = 0.f;
    int   dirMode    = 0;      // 0 random, 1 against motion, 2 upward
    float spread     = kPi;    // direction spread
    float gravity    = 0.f;    // px/s²; positive is upward
    float drag       = 0.f;
    float sizeMin    = 1.f, sizeMax = 1.f;   // size multiplier
    float growth     = 1.f;    // end-of-life scale
    float spin       = 0.f;    // degrees/s
    float sway       = 0.f;    // px/s lateral motion
    float twinkle    = 0.f;    // flicker Hz
    float spiral     = 0.f;    // degrees/s around the origin
    float lifeMul    = 1.f;
    float inheritVel = 0.f;
    float fadeIn     = 0.12f;
};

EmitterSpec const& emitterFor(TrailEffect e) {
    static const std::array<EmitterSpec, kEffectCount> kSpecs = [] {
        std::array<EmitterSpec, kEffectCount> s{};
        auto set = [&](TrailEffect fx) -> EmitterSpec& { return s[static_cast<size_t>(fx)]; };

        auto& comet = set(TrailEffect::Comet);
        comet = {TexSpark, 55.f, 0.f, 20.f, 90.f, 1, 1.6f, -320.f, 1.4f,
                 0.22f, 0.45f, 0.15f, 0.f, 0.f, 0.f, 0.f, 0.55f, 0.10f, 0.05f};

        auto& sparks = set(TrailEffect::Sparks);
        sparks = {TexSpark, 95.f, 0.f, 45.f, 190.f, 1, 1.5f, -820.f, 1.6f,
                  0.30f, 0.62f, 0.12f, 0.f, 0.f, 0.f, 0.f, 0.60f, 0.18f, 0.04f};

        auto& fire = set(TrailEffect::Fire);
        fire = {TexGlow, 75.f, 26.f, 18.f, 62.f, 2, 0.9f, 210.f, 1.1f,
                0.70f, 1.25f, 0.30f, 0.f, 16.f, 0.f, 0.f, 0.75f, 0.10f, 0.08f};

        auto& frost = set(TrailEffect::Frost);
        frost = {TexFlake, 26.f, 7.f, 8.f, 42.f, 0, kPi, -70.f, 0.9f,
                 0.55f, 1.05f, 0.55f, 70.f, 24.f, 0.f, 0.f, 1.7f, 0.05f, 0.10f};

        auto& bubbles = set(TrailEffect::Bubbles);
        bubbles = {TexRing, 22.f, 6.f, 14.f, 48.f, 2, 1.2f, 55.f, 0.7f,
                   0.55f, 1.35f, 1.25f, 12.f, 28.f, 0.f, 0.f, 1.5f, 0.06f, 0.10f};

        auto& stars = set(TrailEffect::Stars);
        stars = {TexSpark, 34.f, 9.f, 6.f, 38.f, 0, kPi, 0.f, 2.1f,
                 0.55f, 1.15f, 0.25f, 40.f, 0.f, 3.2f, 0.f, 1.1f, 0.05f, 0.10f};

        auto& hearts = set(TrailEffect::Hearts);
        hearts = {TexHeart, 17.f, 4.f, 14.f, 52.f, 2, 1.3f, 40.f, 1.0f,
                  0.60f, 1.10f, 0.55f, 25.f, 22.f, 0.f, 0.f, 1.4f, 0.08f, 0.10f};

        auto& smoke = set(TrailEffect::Smoke);
        smoke = {TexPuff, 30.f, 9.f, 8.f, 38.f, 2, 1.5f, 62.f, 1.5f,
                 0.85f, 1.50f, 2.60f, 22.f, 12.f, 0.f, 0.f, 1.6f, 0.12f, 0.14f};

        auto& confetti = set(TrailEffect::Confetti);
        confetti = {TexSquare, 42.f, 0.f, 70.f, 210.f, 0, kPi, -760.f, 0.7f,
                    0.42f, 0.80f, 1.f, 430.f, 34.f, 0.f, 0.f, 1.5f, 0.10f, 0.03f};

        auto& magic = set(TrailEffect::Magic);
        magic = {TexSpark, 85.f, 22.f, 12.f, 46.f, 0, kPi, 0.f, 1.3f,
                 0.28f, 0.65f, 0.20f, 0.f, 0.f, 2.4f, 240.f, 0.95f, 0.05f, 0.06f};

        auto& galaxy = set(TrailEffect::Galaxy);
        galaxy = {TexSpark, 60.f, 16.f, 4.f, 26.f, 0, kPi, 0.f, 1.0f,
                  0.22f, 0.85f, 0.45f, 20.f, 6.f, 4.0f, 30.f, 1.6f, 0.04f, 0.10f};

        auto& orbit = set(TrailEffect::Orbit);
        orbit = {TexDot, 0.f, 0.f, 0.f, 0.f, 0, kPi, 0.f, 2.4f,
                 0.30f, 0.42f, 0.20f, 0.f, 0.f, 0.f, 0.f, 0.55f, 0.f, 0.05f};

        return s;
    }();
    int i = static_cast<int>(e);
    if (i < 0 || i >= kEffectCount) i = 0;
    return kSpecs[static_cast<size_t>(i)];
}

bool usesRibbon(TrailEffect e) {
    return e == TrailEffect::Ribbon || e == TrailEffect::Neon ||
           e == TrailEffect::Comet  || e == TrailEffect::Ink;
}

bool usesHistory(TrailEffect e) {
    return usesRibbon(e) || e == TrailEffect::Lightning;
}

bool usesParticles(TrailEffect e) {
    return emitterFor(e).rate > 0.f || emitterFor(e).idleRate > 0.f ||
           e == TrailEffect::Orbit;
}

// Only fire/smoke update color with age; other effects resolve it at spawn.
bool colorsByAge(TrailEffect e) {
    return e == TrailEffect::Fire || e == TrailEffect::Smoke;
}

}


char const* effectName(TrailEffect e) {
    switch (e) {
        case TrailEffect::Ribbon:    return "Cinta";
        case TrailEffect::Neon:      return "Neon";
        case TrailEffect::Comet:     return "Cometa";
        case TrailEffect::Ink:       return "Tinta";
        case TrailEffect::Dots:      return "Puntos";
        case TrailEffect::Lightning: return "Rayo";
        case TrailEffect::Sparks:    return "Chispas";
        case TrailEffect::Fire:      return "Fuego";
        case TrailEffect::Frost:     return "Nieve";
        case TrailEffect::Bubbles:   return "Burbujas";
        case TrailEffect::Stars:     return "Estrellas";
        case TrailEffect::Hearts:    return "Corazones";
        case TrailEffect::Smoke:     return "Humo";
        case TrailEffect::Confetti:  return "Confeti";
        case TrailEffect::Magic:     return "Magia";
        case TrailEffect::Galaxy:    return "Galaxia";
        case TrailEffect::Orbit:     return "Orbita";
        case TrailEffect::Echo:      return "Fantasma";
        default:                     return "Cinta";
    }
}

char const* effectDesc(TrailEffect e) {
    switch (e) {
        case TrailEffect::Ribbon:    return "Una cinta suave que se afina hacia la cola.";
        case TrailEffect::Neon:      return "Tubo de luz con halo y nucleo blanco.";
        case TrailEffect::Comet:     return "Cinta brillante con cabeza y chispas que caen.";
        case TrailEffect::Ink:       return "Pincelada gruesa que se desvanece despacio.";
        case TrailEffect::Dots:      return "Cadena de bolitas que persiguen al cursor.";
        case TrailEffect::Lightning: return "Rayo electrico con ramas que parpadea.";
        case TrailEffect::Sparks:    return "Chispas que saltan y caen con gravedad.";
        case TrailEffect::Fire:      return "Llamas que suben y se apagan.";
        case TrailEffect::Frost:     return "Copos de nieve que bajan flotando.";
        case TrailEffect::Bubbles:   return "Burbujas que suben meciendose.";
        case TrailEffect::Stars:     return "Estrellitas que titilan en el sitio.";
        case TrailEffect::Hearts:    return "Corazones que flotan hacia arriba.";
        case TrailEffect::Smoke:     return "Nubes de humo que crecen y se abren.";
        case TrailEffect::Confetti:  return "Papelitos que giran y caen.";
        case TrailEffect::Magic:     return "Polvo magico girando alrededor del cursor.";
        case TrailEffect::Galaxy:    return "Estrellas lejanas y polvo espacial.";
        case TrailEffect::Orbit:     return "Satelites que giran alrededor del cursor.";
        case TrailEffect::Echo:      return "Copias del propio cursor que se apagan.";
        default:                     return "";
    }
}

char const* colorModeName(TrailColorMode m) {
    switch (m) {
        case TrailColorMode::Solid:        return "Un color";
        case TrailColorMode::Gradient:     return "Degradado";
        case TrailColorMode::RainbowCycle: return "Arcoiris";
        case TrailColorMode::RainbowTrail: return "Arcoiris largo";
        case TrailColorMode::Random:       return "Al azar";
        case TrailColorMode::Speed:        return "Por velocidad";
        default:                           return "Un color";
    }
}

char const* colorModeDesc(TrailColorMode m) {
    switch (m) {
        case TrailColorMode::Solid:        return "Siempre el color 1.";
        case TrailColorMode::Gradient:     return "Del color 1 al color 2.";
        case TrailColorMode::RainbowCycle: return "Todo el arcoiris, cambiando con el tiempo.";
        case TrailColorMode::RainbowTrail: return "El arcoiris repartido a lo largo de la estela.";
        case TrailColorMode::Random:       return "Un color distinto en cada trozo.";
        case TrailColorMode::Speed:        return "Color 1 quieto, color 2 a toda velocidad.";
        default:                           return "";
    }
}

bool colorModeUsesColor1(TrailColorMode m) {
    return m == TrailColorMode::Solid || m == TrailColorMode::Gradient ||
           m == TrailColorMode::Speed;
}

bool colorModeUsesColor2(TrailColorMode m) {
    return m == TrailColorMode::Gradient || m == TrailColorMode::Speed;
}


namespace {
const TrailPreset kPresets[] = {
    {"Clasico",        {TrailEffect::Ribbon,    TrailColorMode::Solid,        {255,255,255}, {  0,190,255}, 0.55f,  5.f, 1.0f, 205, true,  1.0f}},
    {"Arcoiris",       {TrailEffect::Ribbon,    TrailColorMode::RainbowTrail, {255,255,255}, {  0,190,255}, 0.70f,  6.f, 1.0f, 225, true,  1.0f}},
    {"Neon Rosa",      {TrailEffect::Neon,      TrailColorMode::Solid,        {255, 45,190}, {255,255,255}, 0.60f,  6.f, 1.0f, 215, true,  1.0f}},
    {"Neon Cian",      {TrailEffect::Neon,      TrailColorMode::Solid,        {  0,225,255}, {255,255,255}, 0.60f,  6.f, 1.0f, 215, true,  1.0f}},
    {"Neon Arcoiris",  {TrailEffect::Neon,      TrailColorMode::RainbowCycle, {255,255,255}, {255,255,255}, 0.65f,  7.f, 1.0f, 220, true,  1.4f}},
    {"Cometa",         {TrailEffect::Comet,     TrailColorMode::Gradient,     {255,255,255}, {  0,150,255}, 0.55f,  6.f, 1.2f, 220, true,  1.0f}},
    {"Cometa Dorado",  {TrailEffect::Comet,     TrailColorMode::Gradient,     {255,240,160}, {255,140,  0}, 0.60f,  6.f, 1.3f, 220, true,  1.0f}},
    {"Fuego",          {TrailEffect::Fire,      TrailColorMode::Gradient,     {255,225,120}, {255, 60,  0}, 0.75f,  7.f, 1.2f, 210, true,  1.0f}},
    {"Fuego Azul",     {TrailEffect::Fire,      TrailColorMode::Gradient,     {200,255,255}, { 20, 90,255}, 0.75f,  7.f, 1.2f, 210, true,  1.0f}},
    {"Chispas",        {TrailEffect::Sparks,    TrailColorMode::Gradient,     {255,250,190}, {255,130, 20}, 0.60f,  6.f, 1.1f, 230, true,  1.0f}},
    {"Rayo",           {TrailEffect::Lightning, TrailColorMode::Solid,        {255,240,120}, {255,255,255}, 0.30f,  4.f, 1.0f, 235, true,  1.0f}},
    {"Tormenta",       {TrailEffect::Lightning, TrailColorMode::Gradient,     {170,235,255}, { 90,120,255}, 0.35f,  4.f, 1.4f, 235, true,  1.0f}},
    {"Nieve",          {TrailEffect::Frost,     TrailColorMode::Gradient,     {255,255,255}, {150,225,255}, 1.60f,  7.f, 1.0f, 210, false, 1.0f}},
    {"Burbujas",       {TrailEffect::Bubbles,   TrailColorMode::Gradient,     {200,245,255}, {120,190,255}, 1.40f,  8.f, 1.0f, 190, false, 1.0f}},
    {"Estrellas",      {TrailEffect::Stars,     TrailColorMode::Gradient,     {255,255,220}, {255,205, 80}, 1.10f,  7.f, 1.0f, 230, true,  1.0f}},
    {"Galaxia",        {TrailEffect::Galaxy,    TrailColorMode::Gradient,     {215,170,255}, { 90,140,255}, 1.50f,  6.f, 1.2f, 220, true,  1.0f}},
    {"Corazones",      {TrailEffect::Hearts,    TrailColorMode::Gradient,     {255,140,190}, {255, 40, 90}, 1.40f,  8.f, 1.0f, 220, false, 1.0f}},
    {"Magia",          {TrailEffect::Magic,     TrailColorMode::RainbowCycle, {255,255,255}, {255,255,255}, 0.95f,  5.f, 1.2f, 225, true,  1.6f}},
    {"Polvo de Hada",  {TrailEffect::Magic,     TrailColorMode::Gradient,     {255,240,180}, {255,150,240}, 0.95f,  5.f, 1.4f, 225, true,  1.0f}},
    {"Humo",           {TrailEffect::Smoke,     TrailColorMode::Gradient,     {190,190,200}, { 90, 90,110}, 1.60f,  9.f, 1.0f, 140, false, 1.0f}},
    {"Confeti",        {TrailEffect::Confetti,  TrailColorMode::Random,       {255,255,255}, {255,255,255}, 1.50f,  6.f, 1.0f, 235, false, 1.0f}},
    {"Matrix",         {TrailEffect::Confetti,  TrailColorMode::Gradient,     {160,255,160}, { 10,140, 30}, 1.50f,  5.f, 1.2f, 220, true,  1.0f}},
    {"Serpiente",      {TrailEffect::Dots,      TrailColorMode::Gradient,     {120,255,150}, { 10,120, 90}, 0.55f,  8.f, 1.2f, 230, false, 1.0f}},
    {"Burbujitas",     {TrailEffect::Dots,      TrailColorMode::RainbowTrail, {255,255,255}, {255,255,255}, 0.70f,  9.f, 1.4f, 225, true,  1.0f}},
    {"Orbita",         {TrailEffect::Orbit,     TrailColorMode::RainbowCycle, {255,255,255}, {255,255,255}, 0.55f,  6.f, 1.0f, 225, true,  1.2f}},
    {"Tinta",          {TrailEffect::Ink,       TrailColorMode::Solid,        { 25, 25, 35}, {255,255,255}, 1.60f,  9.f, 1.0f, 205, false, 1.0f}},
    {"Pincel",         {TrailEffect::Ink,       TrailColorMode::RainbowTrail, {255,255,255}, {255,255,255}, 1.60f, 10.f, 1.0f, 215, false, 0.8f}},
    {"Fantasma",       {TrailEffect::Echo,      TrailColorMode::Solid,        {255,255,255}, {255,255,255}, 0.45f,  6.f, 1.0f, 150, false, 1.0f}},
    {"Eco Arcoiris",   {TrailEffect::Echo,      TrailColorMode::RainbowCycle, {255,255,255}, {255,255,255}, 0.55f,  6.f, 1.2f, 170, false, 1.6f}},
    {"Velocimetro",    {TrailEffect::Ribbon,    TrailColorMode::Speed,        { 60,140,255}, {255, 60, 60}, 0.60f,  7.f, 1.0f, 225, true,  1.0f}},
};
constexpr int kPresetCount = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));
}

int presetCount() { return kPresetCount; }

TrailPreset const& presetAt(int index) {
    if (index < 0 || index >= kPresetCount) index = 0;
    return kPresets[index];
}

int findPresetIndex(TrailSettings const& s) {
    auto sameColor = [](ccColor3B a, ccColor3B b) {
        return a.r == b.r && a.g == b.g && a.b == b.b;
    };
// "near" is a windef.h macro, hence the alternative name.
    auto sameF = [](float a, float b) { return std::fabs(a - b) < 0.01f; };
    for (int i = 0; i < kPresetCount; ++i) {
        auto const& p = kPresets[i].settings;
        if (p.effect == s.effect && p.colorMode == s.colorMode &&
            sameColor(p.color1, s.color1) && sameColor(p.color2, s.color2) &&
            sameF(p.life, s.life) && sameF(p.size, s.size) &&
            sameF(p.density, s.density) && p.opacity == s.opacity &&
            p.glow == s.glow && sameF(p.hueSpeed, s.hueSpeed)) {
            return i;
        }
    }
    return -1;
}

CursorTrailNode* CursorTrailNode::create() {
    auto* node = new CursorTrailNode();
    if (node->init()) {
        node->autorelease();
        return node;
    }
    delete node;
    return nullptr;
}

CursorTrailNode::~CursorTrailNode() = default;

bool CursorTrailNode::init() {
    if (!CCNode::init()) return false;

    m_draw = FxDrawBatch::create();
    if (m_draw) this->addChild(m_draw, 0);

    m_echoes = CCNode::create();
    if (m_echoes) this->addChild(m_echoes, -1);

    m_particles.resize(kMaxParticles);
    m_echoPool.resize(kMaxEchoes);
    rebuildForEffect();
    return true;
}

void CursorTrailNode::releaseSharedTextures() { releaseFxTextures(); }

void CursorTrailNode::abandonSharedTextures() { abandonFxTextures(); }

void CursorTrailNode::beginOverlayPass() {
    m_overlayPassSeen = true;
    m_inOverlayPass = true;
}

void CursorTrailNode::visit() {
// The cursor host is visited in both scene and overlay passes; skip the scene
// pass once the overlay pass is known to run to avoid doubled additive glow.
    if (m_overlayPassSeen && !m_inOverlayPass) return;
    CCNode::visit();
}

void CursorTrailNode::applySettings(TrailSettings const& s) {
    bool effectChanged = s.effect != m_cfg.effect;
    bool glowChanged   = s.glow != m_cfg.glow;
    m_cfg = s;
    m_cfg.life     = std::clamp(m_cfg.life, kLifeMin, kLifeMax);
    m_cfg.size     = std::clamp(m_cfg.size, kSizeMin, kSizeMax);
    m_cfg.density  = std::clamp(m_cfg.density, kDensityMin, kDensityMax);
    m_cfg.hueSpeed = std::clamp(m_cfg.hueSpeed, kHueSpeedMin, kHueSpeedMax);
    m_cfg.opacity  = std::clamp(m_cfg.opacity, 0, 255);

    if (effectChanged) {
        reset();
        rebuildForEffect();
    } else if (glowChanged) {
        ccBlendFunc blend = m_cfg.glow ? ccBlendFunc{GL_SRC_ALPHA, GL_ONE}
                                       : ccBlendFunc{GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA};
        if (m_batch) m_batch->setBlendFunc(blend);
        if (m_head)  m_head->setBlendFunc(blend);
        if (m_draw)  m_draw->setAdditive(m_cfg.glow);
    }
}

void CursorTrailNode::rebuildForEffect() {
    auto const& spec = emitterFor(m_cfg.effect);

    ccBlendFunc spriteBlend = m_cfg.glow ? ccBlendFunc{GL_SRC_ALPHA, GL_ONE}
                                         : ccBlendFunc{GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA};
    if (m_draw) m_draw->setAdditive(m_cfg.glow);

// Batch particles by effect texture into one draw call.
    int wanted = usesParticles(m_cfg.effect) ? spec.tex : -1;
    if (wanted != m_batchTexKind) {
        for (auto& p : m_particles) { p.spr = nullptr; p.alive = false; }
        if (m_batch) {
            m_batch->removeFromParent();
            m_batch = nullptr;
        }
        m_batchTexKind = -1;
        if (wanted >= 0) ensureBatch(wanted);
    }
    if (m_batch) m_batch->setBlendFunc(spriteBlend);

    if (m_cfg.effect == TrailEffect::Comet) {
        if (!m_head) {
            if (auto* tex = fxTexture(TexGlow)) {
                m_head = CCSprite::createWithTexture(tex);
                if (m_head) {
                    m_head->setAnchorPoint({0.5f, 0.5f});
                    this->addChild(m_head, 2);
                }
            }
        }
        if (m_head) m_head->setBlendFunc(spriteBlend);
    } else if (m_head) {
        m_head->removeFromParent();
        m_head = nullptr;
    }

    if (m_cfg.effect != TrailEffect::Echo) {
        for (auto& e : m_echoPool) {
            if (e.spr) e.spr->removeFromParent();
            e.spr = nullptr;
            e.alive = false;
        }
    }
}

void CursorTrailNode::ensureBatch(int texKind) {
    auto* tex = fxTexture(texKind);
    if (!tex) return;
    m_batch = CCSpriteBatchNode::createWithTexture(tex, kMaxParticles);
    if (!m_batch) return;
    m_batchTexKind = texKind;
    this->addChild(m_batch, 1);
}

void CursorTrailNode::reset() {
    m_points.clear();
    m_ribbon.clear();
    m_chain.clear();
    m_hasLastPos = false;
    m_spawnCredit = 0.f;
    m_echoCredit = 0.f;
    m_smoothVel = CCPointZero;
    m_speedNorm = 0.f;
    for (auto& p : m_particles) {
        p.alive = false;
        if (p.spr) p.spr->setVisible(false);
    }
// Ghosts are one-shot clones rather than pooled sprites.
    for (auto& e : m_echoPool) {
        e.alive = false;
        if (e.spr) {
            e.spr->removeFromParent();
            e.spr = nullptr;
        }
    }
    if (m_draw) m_draw->clear();
    if (m_head) m_head->setVisible(false);
}

void CursorTrailNode::setEchoSource(CCSprite* src) {
    m_echoSource = src;
}


void CursorTrailNode::step(float dt, CCPoint const& pos) {
    dt = std::clamp(dt, 0.f, 0.05f);   // prevent a lag spike from jumping the trail
    m_time += dt;
// Wrap long-running time to keep float precision stable without a visible seam.
    if (m_time > 3600.f) m_time -= 3600.f;

    CCPoint prev = m_hasLastPos ? m_lastPos : pos;
    if (!m_hasLastPos) {
        m_lastPos = pos;
        m_hasLastPos = true;
        m_orbitCenter = pos;
    }

    CCPoint delta = ccp(pos.x - prev.x, pos.y - prev.y);
    float dist = ccpLength(delta);
    float inst = dt > 0.0001f ? dist / dt : 0.f;
// Smooth speed so velocity-based color and width do not jitter.
    float k = std::min(1.f, dt * 12.f);
    m_smoothVel.x += (delta.x / std::max(dt, 0.0001f) - m_smoothVel.x) * k;
    m_smoothVel.y += (delta.y / std::max(dt, 0.0001f) - m_smoothVel.y) * k;
    m_speedNorm = std::clamp(inst / 1400.f, 0.f, 1.f);

    if (usesHistory(m_cfg.effect)) {
// Add points only after movement; duplicates break ribbon normals.
        if (dist > 0.9f || m_points.empty()) {
            pushPoint(pos, m_speedNorm);
        } else if (!m_points.empty()) {
            m_points.front().pos = pos;
        }
        agePoints(dt);
    } else {
        m_points.clear();
    }

    if (usesParticles(m_cfg.effect) && m_cfg.effect != TrailEffect::Orbit) {
        emit(dt, prev, pos);
    }

    switch (m_cfg.effect) {
        case TrailEffect::Dots: {
            int n = std::clamp(static_cast<int>(5.f + m_cfg.density * 7.f), 4, 26);
            if (static_cast<int>(m_chain.size()) != n) m_chain.assign(static_cast<size_t>(n), pos);
            m_chain[0] = pos;
            float follow = std::clamp(9.f * dt / std::max(0.12f, m_cfg.life), 0.f, 1.f);
            for (size_t i = 1; i < m_chain.size(); ++i) {
                m_chain[i].x += (m_chain[i - 1].x - m_chain[i].x) * follow;
                m_chain[i].y += (m_chain[i - 1].y - m_chain[i].y) * follow;
            }
            break;
        }
        case TrailEffect::Orbit: {
            float lag = std::clamp(7.f * dt / std::max(0.12f, m_cfg.life), 0.f, 1.f);
            m_orbitCenter.x += (pos.x - m_orbitCenter.x) * lag;
            m_orbitCenter.y += (pos.y - m_orbitCenter.y) * lag;
            m_orbitAngle = std::fmod(m_orbitAngle + dt * (1.9f + m_cfg.density * 0.5f),
                                     2.f * kPi);
            break;
        }
        case TrailEffect::Echo: {
            m_echoCredit += dt;
            float period = std::max(0.02f, 0.10f / std::max(0.2f, m_cfg.density));
            if (dist > 1.2f) {
                while (m_echoCredit >= period) {
                    m_echoCredit -= period;
                    spawnEcho(pos);
                }
            } else {
                m_echoCredit = std::min(m_echoCredit, period);
            }
            break;
        }
        case TrailEffect::Lightning:
            m_boltTimer -= dt;
            if (m_boltTimer <= 0.f) {
                m_boltTimer = 1.f / 24.f;
                m_boltSeed = m_boltSeed * 1664525u + 1013904223u;
            }
            break;
        default: break;
    }

    updateParticles(dt);
    updateEchoes(dt);
    redraw();

    m_lastPos = pos;
}

void CursorTrailNode::pushPoint(CCPoint const& pos, float speedNorm) {
    TrailPoint p;
    p.pos = pos;
    p.age = 0.f;
    p.speedNorm = speedNorm;
    p.rnd = frand();
    m_points.push_front(p);
    while (m_points.size() > kMaxPoints) m_points.pop_back();
}

void CursorTrailNode::agePoints(float dt) {
    float life = std::max(0.05f, m_cfg.life);
    for (auto& p : m_points) p.age += dt;
    while (!m_points.empty() && m_points.back().age > life) m_points.pop_back();
}

ccColor3B CursorTrailNode::resolveColor(float t, float rnd, float speedNorm) const {
    switch (m_cfg.colorMode) {
        case TrailColorMode::Solid:
            return m_cfg.color1;
        case TrailColorMode::Gradient:
            return mixColor(m_cfg.color1, m_cfg.color2, t);
        case TrailColorMode::RainbowCycle:
            return hsv(m_time * m_cfg.hueSpeed * 0.18f, 0.85f, 1.f);
        case TrailColorMode::RainbowTrail:
            return hsv(m_time * m_cfg.hueSpeed * 0.18f + t * 0.85f, 0.85f, 1.f);
        case TrailColorMode::Random:
            return hsv(rnd, 0.80f, 1.f);
        case TrailColorMode::Speed:
            return mixColor(m_cfg.color1, m_cfg.color2, speedNorm);
        default:
            return m_cfg.color1;
    }
}

CursorTrailNode::Particle* CursorTrailNode::acquireParticle() {
    Particle* oldest = nullptr;
    float oldestT = -1.f;
    for (auto& p : m_particles) {
        if (!p.alive) return &p;
        float t = p.life / std::max(0.01f, p.maxLife);
        if (t > oldestT) { oldestT = t; oldest = &p; }
    }
    return oldest;
}

void CursorTrailNode::emit(float dt, CCPoint const& from, CCPoint const& to) {
    auto const& spec = emitterFor(m_cfg.effect);
    CCPoint delta = ccp(to.x - from.x, to.y - from.y);
    float dist = ccpLength(delta);
    bool moving = dist > 0.8f;

    float rate = (moving ? spec.rate : spec.idleRate) * m_cfg.density;
    if (rate <= 0.f) return;

    m_spawnCredit += rate * dt;
    int count = static_cast<int>(m_spawnCredit);
    if (count <= 0) return;
    m_spawnCredit -= static_cast<float>(count);
    count = std::min(count, 12);

    float moveAngle = std::atan2(delta.y, delta.x);
    for (int i = 0; i < count; ++i) {
        Particle* p = acquireParticle();
        if (!p) return;

// Distribute spawns across the traveled segment so fast motion stays continuous.
        float f = count > 1 ? static_cast<float>(i) / static_cast<float>(count) : frand();
        p->pos = ccp(from.x + delta.x * f, from.y + delta.y * f);

        float angle;
        switch (spec.dirMode) {
            case 1:  angle = moveAngle + kPi + frand(-spec.spread, spec.spread); break;
            case 2:  angle = kPi * 0.5f + frand(-spec.spread, spec.spread); break;
            default: angle = frand(0.f, 2.f * kPi); break;
        }
        float speed = frand(spec.speedMin, spec.speedMax);
        p->vel = ccp(std::cos(angle) * speed, std::sin(angle) * speed);
        p->vel.x += m_smoothVel.x * spec.inheritVel;
        p->vel.y += m_smoothVel.y * spec.inheritVel;

        p->life = 0.f;
        p->maxLife = std::max(0.08f, m_cfg.life * spec.lifeMul * frand(0.8f, 1.2f));
        p->baseSize = m_cfg.size * 2.2f * frand(spec.sizeMin, spec.sizeMax);
        p->growth = spec.growth;
        p->gravity = spec.gravity;
        p->drag = spec.drag;
        p->spin = frand(-spec.spin, spec.spin);
        p->rot = frand(0.f, 360.f);
        p->swayAmp = spec.sway * frand(0.5f, 1.f) * (frand() < 0.5f ? -1.f : 1.f);
        p->swayPhase = frand(0.f, 2.f * kPi);
        p->twinkle = spec.twinkle;
        p->fadeIn = spec.fadeIn;
        p->spiral = spec.spiral * frand(0.6f, 1.4f) * (frand() < 0.5f ? -1.f : 1.f);
        p->spiralRadius = spec.spiral > 0.f ? m_cfg.size * frand(0.6f, 2.6f) : 0.f;
        p->spiralAngle = frand(0.f, 2.f * kPi);

        p->rnd = frand();
        p->ageColor = colorsByAge(m_cfg.effect);
        p->color = resolveColor(p->ageColor ? 0.f : p->rnd, p->rnd, m_speedNorm);

        if (!p->spr && m_batch) {
            if (auto* tex = fxTexture(m_batchTexKind)) {
// Create outside the batch; setTexture cannot recalculate the blend function in it.
                auto* spr = CCSprite::createWithTexture(tex);
                if (spr) {
                    spr->setAnchorPoint({0.5f, 0.5f});
                    m_batch->addChild(spr);
                    p->spr = spr;
                }
            }
        }
        p->alive = p->spr != nullptr;
        if (p->spr) p->spr->setVisible(true);
    }
}

void CursorTrailNode::updateParticles(float dt) {
    float baseAlpha = m_cfg.opacity / 255.f;
    for (auto& p : m_particles) {
        if (!p.alive) continue;
        p.life += dt;
        if (p.life >= p.maxLife) {
            p.alive = false;
            if (p.spr) p.spr->setVisible(false);
            continue;
        }
        float t = p.life / p.maxLife;

        p.vel.y += p.gravity * dt;
        float damp = std::exp(-p.drag * dt);
        p.vel.x *= damp;
        p.vel.y *= damp;
        p.pos.x += p.vel.x * dt;
        p.pos.y += p.vel.y * dt;
        if (p.swayAmp != 0.f) {
            p.pos.x += std::sin(m_time * 3.4f + p.swayPhase) * p.swayAmp * dt;
        }

        CCPoint draw = p.pos;
        if (p.spiral != 0.f) {
            p.spiralAngle += p.spiral * kPi / 180.f * dt;
            float r = p.spiralRadius * (0.35f + t);
            draw.x += std::cos(p.spiralAngle) * r;
            draw.y += std::sin(p.spiralAngle) * r;
        }

        float fade = p.fadeIn > 0.001f ? std::min(1.f, t / p.fadeIn) : 1.f;
        float out = std::pow(std::max(0.f, 1.f - t), 1.25f);
        float alpha = baseAlpha * fade * out;
        if (p.twinkle > 0.f) {
            alpha *= 0.55f + 0.45f * std::sin(m_time * p.twinkle * 6.28f + p.swayPhase);
        }

        float scale = p.baseSize * (1.f + (p.growth - 1.f) * t) / 64.f;
        p.rot += p.spin * dt;
        if (p.ageColor) p.color = resolveColor(t, p.rnd, m_speedNorm);

        if (p.spr) {
            p.spr->setPosition(draw);
            p.spr->setScale(std::max(0.f, scale));
            p.spr->setRotation(p.rot);
            p.spr->setColor(p.color);
            p.spr->setOpacity(static_cast<GLubyte>(std::clamp(alpha, 0.f, 1.f) * 255.f));
        }
    }
}

void CursorTrailNode::spawnEcho(CCPoint const& pos) {
    auto* src = m_echoSource.data();
    if (!src || !src->getTexture() || !m_echoes) return;

    Echo* slot = nullptr;
    for (auto& e : m_echoPool) {
        if (!e.alive) { slot = &e; break; }
    }
    if (!slot) {
        float worst = -1.f;
        for (auto& e : m_echoPool) {
            float t = e.life / std::max(0.01f, e.maxLife);
            if (t > worst) { worst = t; slot = &e; }
        }
    }
    if (!slot) return;

    if (slot->spr) {
        slot->spr->removeFromParent();
        slot->spr = nullptr;
    }
    auto* clone = CCSprite::createWithTexture(src->getTexture(), src->getTextureRect());
    if (!clone) return;
    clone->setAnchorPoint(src->getAnchorPoint());
    clone->setFlipX(src->isFlipX());
    clone->setFlipY(src->isFlipY());
    clone->setPosition(pos);
    clone->setScaleX(src->getScaleX());
    clone->setScaleY(src->getScaleY());
    clone->setRotation(src->getRotation());
    clone->setSkewX(src->getSkewX());
    clone->setSkewY(src->getSkewY());
    clone->setBlendFunc(m_cfg.glow ? ccBlendFunc{GL_SRC_ALPHA, GL_ONE}
                                   : ccBlendFunc{GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
    if (m_cfg.colorMode != TrailColorMode::Solid ||
        m_cfg.color1.r != 255 || m_cfg.color1.g != 255 || m_cfg.color1.b != 255) {
        clone->setColor(resolveColor(0.f, frand(), m_speedNorm));
    }
    m_echoes->addChild(clone);

    slot->spr = clone;
    slot->alive = true;
    slot->life = 0.f;
    slot->maxLife = std::max(0.12f, m_cfg.life);
    slot->baseScaleX = src->getScaleX();
    slot->baseScaleY = src->getScaleY();
}

void CursorTrailNode::updateEchoes(float dt) {
    float baseAlpha = m_cfg.opacity / 255.f;
    for (auto& e : m_echoPool) {
        if (!e.alive) continue;
        e.life += dt;
        if (e.life >= e.maxLife) {
            e.alive = false;
            if (e.spr) {
                e.spr->removeFromParent();
                e.spr = nullptr;
            }
            continue;
        }
        float t = e.life / e.maxLife;
        if (e.spr) {
            e.spr->setOpacity(static_cast<GLubyte>(
                std::clamp(baseAlpha * std::pow(1.f - t, 1.4f), 0.f, 1.f) * 255.f));
            float scale = 1.f - 0.28f * t;
            e.spr->setScaleX(e.baseScaleX * scale);
            e.spr->setScaleY(e.baseScaleY * scale);
        }
    }
}


void CursorTrailNode::buildRibbonVerts() {
    m_ribbon.clear();
    size_t n = m_points.size();
    if (n < 2) return;

    float life = std::max(0.05f, m_cfg.life);
    float half = m_cfg.size * 0.5f;
    bool isInk = m_cfg.effect == TrailEffect::Ink;

    auto at = [&](long i) -> TrailPoint const& {
        long clamped = std::clamp<long>(i, 0, static_cast<long>(n) - 1);
        return m_points[static_cast<size_t>(clamped)];
    };

    auto push = [&](CCPoint const& pos, float age, float speedNorm, float rnd) {
        RibbonVert v;
        v.pos = pos;
        v.t = std::clamp(age / life, 0.f, 1.f);
        v.color = resolveColor(v.t, rnd, speedNorm);
        v.alpha = std::pow(std::max(0.f, 1.f - v.t), 0.9f);
        float taper = isInk ? (0.55f + 0.45f * (1.f - speedNorm))
                            : std::pow(std::max(0.f, 1.f - v.t), 0.55f);
        v.half = std::max(0.35f, half * taper);
        m_ribbon.push_back(v);
    };

// Catmull-Rom keeps the ribbon smooth across cursor jumps. Subdivision and
// point skipping cap the cost on high-refresh displays.
    constexpr size_t kMaxRibbonVerts = 128;
    size_t segs = n - 1;
    size_t stride = (segs + kMaxRibbonVerts - 1) / kMaxRibbonVerts;
    if (stride < 1) stride = 1;
    int budget = std::clamp(static_cast<int>(kMaxRibbonVerts * stride / segs), 1, 6);

    for (size_t i = 0; i + 1 < n; i += stride) {
        long li = static_cast<long>(i);
        long ls = static_cast<long>(stride);
        TrailPoint const& p0 = at(li - ls);
        TrailPoint const& p1 = at(li);
        TrailPoint const& p2 = at(li + ls);
        TrailPoint const& p3 = at(li + 2 * ls);

        float segLen = ccpLength(ccp(p2.pos.x - p1.pos.x, p2.pos.y - p1.pos.y));
        int steps = std::clamp(static_cast<int>(segLen / 7.f), 1, budget);
        for (int s = 0; s < steps; ++s) {
            float u = static_cast<float>(s) / static_cast<float>(steps);
            float u2 = u * u, u3 = u2 * u;
            CCPoint pos = ccp(
                0.5f * ((2.f * p1.pos.x) + (-p0.pos.x + p2.pos.x) * u +
                        (2.f * p0.pos.x - 5.f * p1.pos.x + 4.f * p2.pos.x - p3.pos.x) * u2 +
                        (-p0.pos.x + 3.f * p1.pos.x - 3.f * p2.pos.x + p3.pos.x) * u3),
                0.5f * ((2.f * p1.pos.y) + (-p0.pos.y + p2.pos.y) * u +
                        (2.f * p0.pos.y - 5.f * p1.pos.y + 4.f * p2.pos.y - p3.pos.y) * u2 +
                        (-p0.pos.y + 3.f * p1.pos.y - 3.f * p2.pos.y + p3.pos.y) * u3));
            push(pos, p1.age + (p2.age - p1.age) * u,
                 p1.speedNorm + (p2.speedNorm - p1.speedNorm) * u, p1.rnd);
        }
    }
    TrailPoint const& last = m_points.back();
    push(last.pos, last.age, last.speedNorm, last.rnd);
}

void CursorTrailNode::strokeRibbon(float widthMul, float alphaMul, bool forceWhite) {
    size_t n = m_ribbon.size();
    if (n < 2 || !m_draw) return;

    float opa = m_cfg.opacity / 255.f * alphaMul;
    std::vector<CCPoint> left(n), right(n);
    for (size_t i = 0; i < n; ++i) {
        CCPoint dir;
        if (i == 0)          dir = ccp(m_ribbon[1].pos.x - m_ribbon[0].pos.x, m_ribbon[1].pos.y - m_ribbon[0].pos.y);
        else if (i == n - 1) dir = ccp(m_ribbon[n - 1].pos.x - m_ribbon[n - 2].pos.x, m_ribbon[n - 1].pos.y - m_ribbon[n - 2].pos.y);
        else                 dir = ccp(m_ribbon[i + 1].pos.x - m_ribbon[i - 1].pos.x, m_ribbon[i + 1].pos.y - m_ribbon[i - 1].pos.y);
        float len = ccpLength(dir);
        if (len < 0.0001f) dir = ccp(1.f, 0.f);
        else               dir = ccp(dir.x / len, dir.y / len);
        CCPoint nrm = ccp(-dir.y, dir.x);
        float hw = m_ribbon[i].half * widthMul;
        left[i]  = ccp(m_ribbon[i].pos.x + nrm.x * hw, m_ribbon[i].pos.y + nrm.y * hw);
        right[i] = ccp(m_ribbon[i].pos.x - nrm.x * hw, m_ribbon[i].pos.y - nrm.y * hw);
    }

    for (size_t i = 0; i + 1 < n; ++i) {
        ccColor3B c = forceWhite
            ? ccc3(255, 255, 255)
            : mixColor(m_ribbon[i].color, m_ribbon[i + 1].color, 0.5f);
        float a = (m_ribbon[i].alpha + m_ribbon[i + 1].alpha) * 0.5f * opa;
        if (a <= 0.004f) continue;
        m_draw->quad(left[i], left[i + 1], right[i + 1], right[i], pma(c, a));
    }

    auto const& head = m_ribbon.front();
    float headAlpha = head.alpha * opa;
    if (headAlpha > 0.004f) {
        m_draw->circle(head.pos, head.half * widthMul,
                       pma(forceWhite ? ccc3(255, 255, 255) : head.color, headAlpha), 14);
    }
}

void CursorTrailNode::drawLightning() {
    if (m_ribbon.empty() || !m_draw) return;

    constexpr int kNodes = 16;
    size_t n = m_ribbon.size();
    std::array<CCPoint, kNodes> pts{};
    std::array<ccColor3B, kNodes> cols{};
    std::array<float, kNodes> alphas{};

    float amp = m_cfg.size * 2.2f;
    for (int i = 0; i < kNodes; ++i) {
        float u = static_cast<float>(i) / static_cast<float>(kNodes - 1);
        size_t idx = static_cast<size_t>(u * static_cast<float>(n - 1));
        auto const& v = m_ribbon[idx];
        float wobble = amp * std::sin(u * kPi) * 1.6f;
        float ox = hashNoise(m_boltSeed, i * 2) * wobble;
        float oy = hashNoise(m_boltSeed, i * 2 + 1) * wobble;
        pts[i] = ccp(v.pos.x + ox, v.pos.y + oy);
        cols[i] = v.color;
        alphas[i] = v.alpha;
    }

    float opa = m_cfg.opacity / 255.f;
    float core = std::max(1.f, m_cfg.size * 0.35f);
    for (int i = 0; i + 1 < kNodes; ++i) {
        float a = (alphas[i] + alphas[i + 1]) * 0.5f;
        if (a <= 0.01f) continue;
        ccColor3B c = mixColor(cols[i], cols[i + 1], 0.5f);
        m_draw->thickLine(pts[i], pts[i + 1], core * 3.4f, pma(c, a * opa * 0.22f));
        m_draw->thickLine(pts[i], pts[i + 1], core * 1.6f, pma(c, a * opa * 0.55f));
        m_draw->thickLine(pts[i], pts[i + 1], core * 0.7f,
                          pma(ccc3(255, 255, 255), a * opa * 0.95f));
        if (i > 0) {
            m_draw->circle(pts[i], core * 0.8f, pma(c, a * opa * 0.55f), 8);
            m_draw->circle(pts[i], core * 0.35f, pma(ccc3(255, 255, 255), a * opa * 0.95f), 8);
        }
    }

    for (int b = 0; b < 3; ++b) {
        int origin = 2 + static_cast<int>((hashNoise(m_boltSeed, 90 + b) * 0.5f + 0.5f) * (kNodes - 5));
        origin = std::clamp(origin, 1, kNodes - 3);
        CCPoint from = pts[origin];
        CCPoint dir = ccp(pts[origin + 1].x - pts[origin - 1].x, pts[origin + 1].y - pts[origin - 1].y);
        float len = ccpLength(dir);
        if (len < 0.01f) continue;
        dir = ccp(dir.x / len, dir.y / len);
        CCPoint nrm = ccp(-dir.y, dir.x);
        float side = hashNoise(m_boltSeed, 120 + b) > 0.f ? 1.f : -1.f;
        float a = alphas[origin] * opa * 0.5f;
        if (a <= 0.01f) continue;
        for (int s = 0; s < 2; ++s) {
            float reach = amp * (1.4f - 0.5f * s) * (0.6f + 0.4f * std::fabs(hashNoise(m_boltSeed, 150 + b * 4 + s)));
            CCPoint to = ccp(from.x + (nrm.x * side + dir.x * 0.6f) * reach,
                             from.y + (nrm.y * side + dir.y * 0.6f) * reach);
            m_draw->thickLine(from, to, core * 0.9f, pma(cols[origin], a * (1.f - 0.4f * s)));
            from = to;
            side *= -1.f;
        }
    }
}

void CursorTrailNode::drawDots() {
    if (m_chain.size() < 2 || !m_draw) return;
    float opa = m_cfg.opacity / 255.f;
    size_t n = m_chain.size();
    for (size_t i = 0; i < n; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(n - 1);
        float radius = m_cfg.size * 0.6f * (1.f - 0.72f * t);
        if (radius < 0.4f) continue;
        ccColor3B c = resolveColor(t, static_cast<float>(i) * 0.137f, m_speedNorm);
        float a = opa * std::pow(1.f - t, 0.55f);
        if (m_cfg.glow) {
            m_draw->circle(m_chain[i], radius * 1.9f, pma(c, a * 0.20f), 16);
        }
        m_draw->circle(m_chain[i], radius, pma(c, a), 20);
    }
}

void CursorTrailNode::drawOrbit() {
    if (!m_draw) return;
    int count = std::clamp(static_cast<int>(2.f + m_cfg.density * 1.5f), 2, 6);
    float radius = m_cfg.size * 2.6f;
    float opa = m_cfg.opacity / 255.f;

    for (int i = 0; i < count; ++i) {
        float ang = m_orbitAngle + 2.f * kPi * static_cast<float>(i) / static_cast<float>(count);
        CCPoint p = ccp(m_orbitCenter.x + std::cos(ang) * radius,
                        m_orbitCenter.y + std::sin(ang) * radius * 0.72f);
    float depth = 0.55f + 0.45f * std::sin(ang);
        ccColor3B c = resolveColor(static_cast<float>(i) / static_cast<float>(count),
                                   static_cast<float>(i) * 0.31f, m_speedNorm);
        float r = m_cfg.size * (0.42f + 0.22f * depth);
        m_draw->circle(p, r * 2.4f, pma(c, opa * 0.18f * depth), 16);
        m_draw->circle(p, r, pma(c, opa * depth), 20);

        if (frand() < 0.55f) {
            if (Particle* dust = acquireParticle()) {
                auto const& spec = emitterFor(TrailEffect::Orbit);
                dust->pos = p;
                dust->vel = ccp(frand(-14.f, 14.f), frand(-14.f, 14.f));
                dust->life = 0.f;
                dust->maxLife = std::max(0.1f, m_cfg.life * spec.lifeMul);
                dust->baseSize = m_cfg.size * 2.2f * frand(spec.sizeMin, spec.sizeMax);
                dust->growth = spec.growth;
                dust->gravity = 0.f;
                dust->drag = spec.drag;
                dust->spin = 0.f;
                dust->swayAmp = 0.f;
                dust->twinkle = 0.f;
                dust->spiral = 0.f;
                dust->fadeIn = spec.fadeIn;
                dust->rnd = 0.f;
                dust->ageColor = false;
                dust->color = c;
                if (!dust->spr && m_batch) {
                    if (auto* tex = fxTexture(m_batchTexKind)) {
                        auto* spr = CCSprite::createWithTexture(tex);
                        if (spr) {
                            spr->setAnchorPoint({0.5f, 0.5f});
                            m_batch->addChild(spr);
                            dust->spr = spr;
                        }
                    }
                }
                dust->alive = dust->spr != nullptr;
                if (dust->spr) dust->spr->setVisible(true);
            }
        }
    }
}

void CursorTrailNode::redraw() {
    if (!m_draw) return;
    m_draw->clear();

    if (usesRibbon(m_cfg.effect) || m_cfg.effect == TrailEffect::Lightning) {
        buildRibbonVerts();
    }

    switch (m_cfg.effect) {
        case TrailEffect::Ribbon:
            if (m_cfg.glow) strokeRibbon(2.3f, 0.18f, false);
            strokeRibbon(1.f, 1.f, false);
            break;

        case TrailEffect::Neon:
            strokeRibbon(3.2f, 0.13f, false);
            strokeRibbon(1.7f, 0.34f, false);
            strokeRibbon(1.f, 0.85f, false);
            strokeRibbon(0.38f, 1.f, true);
            break;

        case TrailEffect::Comet:
            strokeRibbon(2.6f, 0.16f, false);
            strokeRibbon(1.f, 0.95f, false);
            strokeRibbon(0.42f, 0.9f, true);
            break;

        case TrailEffect::Ink:
            strokeRibbon(1.f, 1.f, false);
            break;

        case TrailEffect::Lightning:
            drawLightning();
            break;

        case TrailEffect::Dots:
            drawDots();
            break;

        case TrailEffect::Orbit:
            drawOrbit();
            break;

        default:
            break;
    }

    if (m_head) {
        bool show = !m_ribbon.empty();
        m_head->setVisible(show);
        if (show) {
            float pulse = 0.86f + 0.14f * std::sin(m_time * 7.f);
            m_head->setPosition(m_ribbon.front().pos);
            m_head->setColor(m_ribbon.front().color);
            m_head->setScale(m_cfg.size * 3.6f * pulse / 64.f);
            m_head->setOpacity(static_cast<GLubyte>(
                std::clamp(m_cfg.opacity / 255.f * 0.9f, 0.f, 1.f) * 255.f));
        }
    }
}

}
