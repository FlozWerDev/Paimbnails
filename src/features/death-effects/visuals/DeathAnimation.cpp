#include "DeathAnimation.hpp"
#include "../ui/DeathEffectPopup.hpp"
#include "../../colorful-icons/services/IconColorMath.hpp"
#include "../../gameplay-performance/GameplayPerformance.hpp"
#include "../../../utils/AnimatedGIFSprite.hpp"
#include "../../../utils/AtomicFileWrite.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/FilesystemPath.hpp"
#include "../../../utils/GLSLLoader.hpp"
#include "../../../utils/ImageLoadHelper.hpp"
#include "../../../utils/LocalAssetStore.hpp"
#include "../../../utils/PaimonDrawNode.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../ui/PaimonUI.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/binding/ShaderLayer.hpp>
#include <Geode/binding/SliderThumb.hpp>
#include <Geode/ui/ColorPickPopup.hpp>
#include <fmt/format.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <random>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::death_effects {
namespace {

constexpr auto kLegacySelectionKey = "death-animation-style";
constexpr auto kContainerId = "death-animation-container"_spr;
constexpr auto kScreenOverlayId = "death-screen-overlay-container"_spr;
constexpr int kCameraTiltActionTag = 0x50414954;

bool s_gdShaderTriggeredOnDeath = false;

std::filesystem::path vfxConfigPath() {
    return paimon::paths::toStdPath(Mod::get()->getSaveDir()) / "death_vfx_config.json";
}

CCGLProgram* program() {
    return paimon::shaders::loadShader(
        "paimon-death-animation-v3", "position.vert", "death_animation.fsh", nullptr, nullptr);
}

ccColor3B pickRainbowAtTime() {
    static float phase = 0.f;
    phase = std::fmod(phase + 47.f, 360.f);
    return paimon::icons::math::fromHSV({phase, 0.92f, 1.f});
}

ccColor3B makeComplementary(ccColor3B c) {
    return {
        static_cast<GLubyte>(255 - c.r / 2),
        static_cast<GLubyte>(std::min(255, static_cast<int>(c.g) + 50)),
        static_cast<GLubyte>(255 - c.b / 3),
    };
}

void resolveEffectColors(
    DeathStyleSettings const& s,
    ccColor3B playerPrimary,
    ccColor3B playerSecondary,
    ccColor3B& out1,
    ccColor3B& out2
) {
    switch (s.colorMode) {
        case DeathColorMode::PlayerPrimary:
            out1 = playerPrimary;
            out2 = makeComplementary(playerPrimary);
            break;
        case DeathColorMode::PlayerSecondary:
            out1 = playerSecondary;
            out2 = playerPrimary;
            break;
        case DeathColorMode::DualGradient:
            out1 = playerPrimary;
            out2 = playerSecondary;
            break;
        case DeathColorMode::Custom:
            out1 = s.customPrimary;
            out2 = s.customSecondary;
            break;
        case DeathColorMode::Rainbow:
            out1 = pickRainbowAtTime();
            out2 = makeComplementary(out1);
            break;
    }
}

class AnimationSprite : public CCSprite {
    float m_elapsed = 0.f;
    float m_duration = 0.85f;
    int m_style = 0;
    ccColor3B m_tint1{};
    ccColor3B m_tint2{};
    float m_intensity = 1.f;
    float m_seed = 0.f;
    float m_spin = 0.f;

    GLint m_progressLoc = -1;
    GLint m_styleLoc = -1;
    GLint m_tintLoc = -1;
    GLint m_tint2Loc = -1;
    GLint m_intensityLoc = -1;
    GLint m_seedLoc = -1;
    GLint m_spinLoc = -1;

public:
    static AnimationSprite* create(
        int style, ccColor3B tint1, ccColor3B tint2,
        float durationSec, float intensity, float seed, float spin
    ) {
        auto* shader = program();
        if (!shader) return nullptr;

        auto* texture = new CCTexture2D();
        unsigned char white[] = {255, 255, 255, 255};
        if (!texture->initWithData(white, kCCTexture2DPixelFormat_RGBA8888, 1, 1, {1, 1})) {
            texture->release();
            return nullptr;
        }
        auto* node = new AnimationSprite();
        bool ok = node->initWithTexture(texture);
        texture->release();
        if (!ok) {
            delete node;
            return nullptr;
        }
        node->autorelease();
        node->m_style = style;
        node->m_tint1 = tint1;
        node->m_tint2 = tint2;
        node->m_duration = std::clamp(durationSec, 0.25f, 2.5f);
        node->m_intensity = std::clamp(intensity, 0.2f, 2.0f);
        node->m_seed = seed;
        node->m_spin = spin;
        node->setShaderProgram(shader);
        node->m_progressLoc  = shader->getUniformLocationForName("u_progress");
        node->m_styleLoc     = shader->getUniformLocationForName("u_style");
        node->m_tintLoc      = shader->getUniformLocationForName("u_tint");
        node->m_tint2Loc     = shader->getUniformLocationForName("u_tint2");
        node->m_intensityLoc = shader->getUniformLocationForName("u_intensity");
        node->m_seedLoc      = shader->getUniformLocationForName("u_seed");
        node->m_spinLoc      = shader->getUniformLocationForName("u_spin");
        node->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
        node->setScale(190.f);
        node->scheduleUpdate();
        return node;
    }

    void update(float dt) override {
        m_elapsed += std::max(0.f, dt);
        if (m_elapsed >= m_duration) {
            removeFromParentAndCleanup(true);
        }
    }

    void draw() override {
        auto* shader = getShaderProgram();
        if (!shader) return;
        shader->use();
        if (m_progressLoc  >= 0) shader->setUniformLocationWith1f(m_progressLoc, m_elapsed / m_duration);
        if (m_styleLoc     >= 0) shader->setUniformLocationWith1f(m_styleLoc, static_cast<float>(m_style));
        if (m_tintLoc >= 0) {
            shader->setUniformLocationWith3f(m_tintLoc,
                0.15f + 0.85f * m_tint1.r / 255.f,
                0.15f + 0.85f * m_tint1.g / 255.f,
                0.15f + 0.85f * m_tint1.b / 255.f);
        }
        if (m_tint2Loc >= 0) {
            shader->setUniformLocationWith3f(m_tint2Loc,
                0.15f + 0.85f * m_tint2.r / 255.f,
                0.15f + 0.85f * m_tint2.g / 255.f,
                0.15f + 0.85f * m_tint2.b / 255.f);
        }
        if (m_intensityLoc >= 0) shader->setUniformLocationWith1f(m_intensityLoc, m_intensity);
        if (m_seedLoc >= 0) shader->setUniformLocationWith1f(m_seedLoc, m_seed);
        if (m_spinLoc >= 0) shader->setUniformLocationWith1f(m_spinLoc, m_spin);
        CCSprite::draw();
    }
};

// Multi-shape particle + physics emitter built on PaimonDrawNode.
class RichParticlesNode : public CCNode {
    struct Particle {
        float angle = 0.f;
        float speed = 90.f;
        float size = 4.f;
        float spin = 0.f;
        ccColor3B col{};
    };
    std::vector<Particle> m_particles;
    CCDrawNode* m_draw = nullptr;
    DeathParticleShape m_shape = DeathParticleShape::Sparks;
    DeathParticlePhysics m_physics = DeathParticlePhysics::Explode;
    bool m_shockRing = false;
    ccColor3B m_ringCol{};
    float m_ringMaxRadius = 80.f;
    float m_elapsed = 0.f;
    float m_duration = 0.7f;

public:
    static RichParticlesNode* create(
        DeathStyleSettings const& s, ccColor3B c1, ccColor3B c2, float effScale, float durationSec
    ) {
        auto* ret = new RichParticlesNode();
        if (ret && ret->initNode(s, c1, c2, effScale, durationSec)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    bool initNode(
        DeathStyleSettings const& s, ccColor3B c1, ccColor3B c2, float effScale, float durationSec
    ) {
        if (!CCNode::init()) return false;
        m_shape = s.particleShape;
        m_physics = s.particlePhysics;
        m_shockRing = s.enableShockRing;
        m_ringCol = c1;
        m_ringMaxRadius = 92.f * effScale;
        m_duration = std::clamp(durationSec * 0.9f, 0.25f, 2.0f);

        m_draw = PaimonDrawNode::create();
        if (!m_draw) return false;
        this->addChild(m_draw);

        int count = (m_shape == DeathParticleShape::None) ? 0 : std::clamp(s.particleCount, 0, 48);
        if (count > 0) {
            static std::mt19937 rng(std::random_device{}());
            std::uniform_real_distribution<float> angDist(0.f, 2.f * static_cast<float>(M_PI));
            std::uniform_real_distribution<float> spdDist(45.f * effScale, 138.f * effScale);
            std::uniform_real_distribution<float> szDist(2.2f * effScale, 5.8f * effScale);
            std::uniform_real_distribution<float> spinDist(-5.f, 5.f);

            m_particles.resize(count);
            for (int i = 0; i < count; ++i) {
                ccColor3B col = (i % 2 == 0) ? c1 : c2;
                if (m_shape == DeathParticleShape::Confetti) {
                    col = paimon::icons::math::fromHSV({static_cast<float>((i * 47) % 360), 0.92f, 1.f});
                }
                m_particles[i] = Particle{angDist(rng), spdDist(rng), szDist(rng), spinDist(rng), col};
            }
        }
        this->scheduleUpdate();
        return true;
    }

    void update(float dt) override {
        m_elapsed += std::max(0.f, dt);
        float t = m_elapsed / m_duration;
        if (t >= 1.f) {
            this->removeFromParentAndCleanup(true);
            return;
        }
        float ease = 1.f - std::pow(1.f - t, 2.4f);
        float alpha = 1.f - std::clamp((t - 0.35f) / 0.65f, 0.f, 1.f);
        m_draw->clear();

        if (m_shockRing) {
            float rad = m_ringMaxRadius * ease;
            ccColor4F rCol = {m_ringCol.r / 255.f, m_ringCol.g / 255.f, m_ringCol.b / 255.f, alpha * 0.72f};
            constexpr int kSeg = 28;
            for (int i = 0; i < kSeg; ++i) {
                float a0 = (2.f * static_cast<float>(M_PI) * i) / kSeg;
                float a1 = (2.f * static_cast<float>(M_PI) * (i + 1)) / kSeg;
                m_draw->drawSegment(
                    ccp(std::cos(a0) * rad, std::sin(a0) * rad),
                    ccp(std::cos(a1) * rad, std::sin(a1) * rad),
                    std::max(0.6f, 1.8f * (1.f - t)),
                    rCol
                );
            }
        }

        for (auto const& p : m_particles) {
            float dist = p.speed * ease;
            float ang = p.angle;
            if (m_physics == DeathParticlePhysics::Swirl) {
                ang += t * 3.8f;
            }
            CCPoint pos = ccp(std::cos(ang) * dist, std::sin(ang) * dist);
            if (m_physics == DeathParticlePhysics::Gravity) {
                pos.y -= 130.f * t * t;
            } else if (m_physics == DeathParticlePhysics::FloatUp) {
                pos = ccp(pos.x * 0.55f + std::sin(t * 8.f + p.angle) * 6.f,
                          std::abs(pos.y) * 0.45f + t * p.speed * 0.95f);
            }

            float sz = p.size * (1.f - t * 0.35f);
            ccColor4F col = {p.col.r / 255.f, p.col.g / 255.f, p.col.b / 255.f, alpha * 0.95f};
            float rot = p.angle + p.spin * t;

            switch (m_shape) {
                case DeathParticleShape::Sparks: {
                    CCPoint tail = pos - ccp(std::cos(ang) * sz * 2.1f, std::sin(ang) * sz * 2.1f);
                    m_draw->drawSegment(tail, pos, std::max(0.7f, sz * 0.36f), col);
                    m_draw->drawDot(pos, std::max(0.7f, sz * 0.42f), col);
                    break;
                }
                case DeathParticleShape::Lines: {
                    CCPoint d = ccp(std::cos(ang) * sz * 2.6f, std::sin(ang) * sz * 2.6f);
                    m_draw->drawSegment(pos - d * 0.5f, pos + d * 0.5f, std::max(0.7f, sz * 0.28f), col);
                    break;
                }
                case DeathParticleShape::Squares:
                case DeathParticleShape::Confetti: {
                    float h = sz * (m_shape == DeathParticleShape::Confetti ? 0.5f : 0.78f);
                    CCPoint pts[4];
                    float c = std::cos(rot), sn = std::sin(rot);
                    CCPoint corners[4] = {{-sz, -h}, {sz, -h}, {sz, h}, {-sz, h}};
                    for (int k = 0; k < 4; ++k) {
                        pts[k] = pos + ccp(corners[k].x * c - corners[k].y * sn,
                                           corners[k].x * sn + corners[k].y * c);
                    }
                    m_draw->drawPolygon(pts, 4, col, 0.f, col);
                    break;
                }
                case DeathParticleShape::Triangles: {
                    CCPoint pts[3];
                    for (int k = 0; k < 3; ++k) {
                        float a = rot + k * (2.f * static_cast<float>(M_PI) / 3.f);
                        pts[k] = pos + ccp(std::cos(a) * sz, std::sin(a) * sz);
                    }
                    m_draw->drawPolygon(pts, 3, col, 0.f, col);
                    break;
                }
                case DeathParticleShape::Stars: {
                    CCPoint a1 = ccp(std::cos(rot) * sz * 1.4f, std::sin(rot) * sz * 1.4f);
                    CCPoint a2 = ccp(-std::sin(rot) * sz * 1.4f, std::cos(rot) * sz * 1.4f);
                    m_draw->drawSegment(pos - a1, pos + a1, std::max(0.6f, sz * 0.25f), col);
                    m_draw->drawSegment(pos - a2, pos + a2, std::max(0.6f, sz * 0.25f), col);
                    break;
                }
                case DeathParticleShape::Rings: {
                    m_draw->drawDot(pos, sz * 0.72f, {col.r, col.g, col.b, alpha * 0.35f});
                    m_draw->drawDot(pos, sz * 0.32f, col);
                    break;
                }
                case DeathParticleShape::None:
                    break;
            }
        }
    }
};

void spawnGhostEcho(CCNode* container, CCPoint pos, ccColor3B col, float scale, float durationSec) {
    auto* ghost = paimon::SpriteHelper::safeCreateWithFrameName("player_01_001.png");
    if (!ghost) ghost = paimon::SpriteHelper::safeCreateWithFrameName("GJ_square01.png");
    if (!ghost) return;
    ghost->setPosition(pos);
    ghost->setColor(col);
    ghost->setOpacity(180);
    ghost->setScale(scale * 0.9f);
    ghost->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
    ghost->runAction(CCSequence::create(
        CCSpawn::create(
            CCEaseOut::create(CCScaleTo::create(durationSec * 0.85f, scale * 2.1f), 2.f),
            CCMoveBy::create(durationSec * 0.85f, ccp(0.f, 22.f * scale)),
            CCFadeOut::create(durationSec * 0.85f),
            nullptr
        ),
        CCRemoveSelf::create(),
        nullptr
    ));
    container->addChild(ghost, 4);
}

void spawnScreenOverlayFx(CCNode* root, CCSize size, DeathScreenFx fx, ccColor3B col, float strength, float dur) {
    if (!root || fx == DeathScreenFx::None) return;
    if (auto* old = root->getChildByID(kScreenOverlayId)) old->removeFromParent();

    auto* holder = CCNode::create();
    holder->setID(kScreenOverlayId);
    holder->setContentSize(size);
    root->addChild(holder, 9999);

    float const hold = std::max(0.05f, dur * 0.25f);
    float const fade = std::max(0.12f, dur * 0.75f);

    if (fx == DeathScreenFx::VignettePulse) {
        GLubyte const alpha = static_cast<GLubyte>(std::clamp(strength * 135.f, 35.f, 200.f));
        ccColor4B const vCol = {
            static_cast<GLubyte>(col.r / 3),
            static_cast<GLubyte>(col.g / 3),
            static_cast<GLubyte>(col.b / 3),
            alpha
        };
        float const topH = size.height * 0.16f;
        float const sideW = size.width * 0.14f;
        struct Strip { float x; float y; float w; float h; };
        Strip const strips[4] = {
            {0.f, size.height - topH, size.width, topH},
            {0.f, 0.f, size.width, topH},
            {0.f, topH, sideW, std::max(1.f, size.height - 2.f * topH)},
            {size.width - sideW, topH, sideW, std::max(1.f, size.height - 2.f * topH)},
        };
        for (auto const& st : strips) {
            auto* strip = CCLayerColor::create(vCol, st.w, st.h);
            strip->setPosition(ccp(st.x, st.y));
            strip->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
            strip->runAction(CCSequence::create(
                CCDelayTime::create(hold),
                CCFadeOut::create(fade),
                nullptr
            ));
            holder->addChild(strip);
        }
    } else {
        GLubyte alpha = static_cast<GLubyte>(std::clamp(strength * 145.f, 20.f, 190.f));
        ccColor3B flashCol = col;
        if (fx == DeathScreenFx::GDInvert) flashCol = {255, 255, 255};
        else if (fx == DeathScreenFx::GDChromaticGlitch) flashCol = {255, 70, 180};
        auto* layer = CCLayerColor::create({flashCol.r, flashCol.g, flashCol.b, alpha}, size.width, size.height);
        layer->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
        layer->runAction(CCSequence::create(
            CCDelayTime::create(hold),
            CCFadeOut::create(fade),
            nullptr
        ));
        holder->addChild(layer);
    }

    holder->runAction(CCSequence::create(
        CCDelayTime::create(hold + fade + 0.02f),
        CCRemoveSelf::create(),
        nullptr
    ));
}

CCSprite* makeSwatch(ccColor3B col) {
    auto* spr = CCSprite::createWithSpriteFrameName("GJ_colorBtn_001.png");
    spr->setColor(col);
    spr->setScale(16.f / std::max(1.f, spr->getContentSize().height));
    return spr;
}

char const* colorModeLabel(DeathColorMode m) {
    switch (m) {
        case DeathColorMode::PlayerPrimary:   return "Player 1";
        case DeathColorMode::PlayerSecondary: return "Player 2";
        case DeathColorMode::DualGradient:    return "Dual P1+P2";
        case DeathColorMode::Custom:          return "Custom";
        case DeathColorMode::Rainbow:         return "Rainbow";
    }
    return "Dual P1+P2";
}

char const* partShapeLabel(DeathParticleShape s) {
    switch (s) {
        case DeathParticleShape::Sparks:    return "Sparks";
        case DeathParticleShape::Squares:   return "Squares";
        case DeathParticleShape::Stars:     return "Stars";
        case DeathParticleShape::Rings:     return "Bubbles";
        case DeathParticleShape::Lines:     return "Streaks";
        case DeathParticleShape::Triangles: return "Shards";
        case DeathParticleShape::Confetti:  return "Confetti";
        case DeathParticleShape::None:      return "None";
    }
    return "Sparks";
}

char const* partPhysicsLabel(DeathParticlePhysics p) {
    switch (p) {
        case DeathParticlePhysics::Explode: return "Explode";
        case DeathParticlePhysics::Gravity: return "Gravity";
        case DeathParticlePhysics::FloatUp: return "Float Up";
        case DeathParticlePhysics::Swirl:   return "Swirl";
    }
    return "Explode";
}

char const* camZoomLabel(DeathCameraZoom z) {
    switch (z) {
        case DeathCameraZoom::None:       return "Off";
        case DeathCameraZoom::PunchIn:    return "Punch In";
        case DeathCameraZoom::RecoilOut:  return "Recoil Out";
        case DeathCameraZoom::DramaticIn: return "Dramatic";
    }
    return "Off";
}

char const* camTiltLabel(DeathCameraTilt t) {
    switch (t) {
        case DeathCameraTilt::None:     return "Off";
        case DeathCameraTilt::Subtle:   return "Subtle";
        case DeathCameraTilt::HardKick: return "Hard Kick";
    }
    return "Off";
}

char const* screenFxLabel(DeathScreenFx f) {
    switch (f) {
        case DeathScreenFx::None:              return "None";
        case DeathScreenFx::GDShockwave:       return "GD Shockwave";
        case DeathScreenFx::GDShockLine:       return "GD ShockLine";
        case DeathScreenFx::GDChromaticGlitch: return "GD Chromatic";
        case DeathScreenFx::GDRadialBlur:      return "GD RadialBlur";
        case DeathScreenFx::GDBulge:           return "GD Bulge";
        case DeathScreenFx::GDPinch:           return "GD Pinch";
        case DeathScreenFx::GDInvert:          return "GD Invert";
        case DeathScreenFx::GDLensCircle:      return "GD Lens";
        case DeathScreenFx::GDColorShift:      return "GD TintShift";
        case DeathScreenFx::VignettePulse:     return "Vignette";
        case DeathScreenFx::ImpactFlash:       return "Flash Overlay";
    }
    return "None";
}

std::string shortFileLabel(std::string const& p) {
    if (p.empty()) return "(none)";
    auto name = paimon::paths::toUtf8(std::filesystem::path(p).filename());
    if (name.size() > 16) name = name.substr(0, 14) + "..";
    return name;
}

template <class Cb>
void openColorPicker(ccColor3B cur, Cb&& cb) {
    auto* pop = geode::ColorPickPopup::create(ccc4(cur.r, cur.g, cur.b, 255));
    if (!pop) return;
    pop->setCallback([cb = std::forward<Cb>(cb)](ccColor4B const& col) {
        cb(ccColor3B{col.r, col.g, col.b});
    });
    pop->show();
}

} // namespace

DeathStyleSettings defaultStyleSettings(int style) {
    DeathStyleSettings s;
    if (style < 0 || style >= kDeathStyleCount) {
        if (style == kStyleCustomMedia) {
            s.particleShape = DeathParticleShape::None;
            s.enableShockRing = false;
            s.cameraShake = 0.f;
        }
        return s;
    }

    int const fam = style / 8;
    int const sub = style % 8;

    if (fam == 0) { // Cosmic (0..7)
        s.particleShape = (sub == 1 || sub == 6) ? DeathParticleShape::Stars : DeathParticleShape::Sparks;
        s.particlePhysics = (sub == 1 || sub == 3 || sub == 7) ? DeathParticlePhysics::Swirl : DeathParticlePhysics::Explode;
        s.cameraShake = 0.35f;
        s.cameraZoom = (sub == 1 || sub == 7) ? DeathCameraZoom::PunchIn : DeathCameraZoom::None;
        s.screenFx = (sub == 0) ? DeathScreenFx::GDRadialBlur :
                     (sub == 1 || sub == 7) ? DeathScreenFx::GDPinch :
                     (sub == 6) ? DeathScreenFx::GDLensCircle : DeathScreenFx::GDShockwave;
    } else if (fam == 1) { // Cyber (8..15)
        s.particleShape = (sub == 1 || sub == 5 || sub == 6) ? DeathParticleShape::Squares : DeathParticleShape::Lines;
        s.particlePhysics = DeathParticlePhysics::Explode;
        s.cameraShake = 0.45f;
        s.cameraTilt = (sub == 0 || sub == 4 || sub == 7) ? DeathCameraTilt::HardKick : DeathCameraTilt::Subtle;
        s.screenFx = (sub == 0 || sub == 2 || sub == 7) ? DeathScreenFx::GDChromaticGlitch :
                     (sub == 4) ? DeathScreenFx::GDInvert : DeathScreenFx::GDShockLine;
    } else if (fam == 2) { // Elemental (16..23)
        s.particleShape = (sub == 3 || sub == 7) ? DeathParticleShape::Rings :
                          (sub == 2 || sub == 6) ? DeathParticleShape::Triangles : DeathParticleShape::Sparks;
        s.particlePhysics = (sub == 0 || sub == 3 || sub == 6) ? DeathParticlePhysics::FloatUp :
                            (sub == 5 ? DeathParticlePhysics::Swirl : DeathParticlePhysics::Explode);
        s.cameraShake = (sub == 2 || sub == 4 || sub == 6) ? 0.5f : 0.25f;
        s.screenFx = (sub == 2) ? DeathScreenFx::ImpactFlash :
                     (sub == 4 || sub == 5) ? DeathScreenFx::GDBulge :
                     (sub == 3 || sub == 7) ? DeathScreenFx::GDLensCircle : DeathScreenFx::GDColorShift;
    } else if (fam == 3) { // Impact (24..31)
        s.particleShape = (sub == 5) ? DeathParticleShape::Confetti : DeathParticleShape::Triangles;
        s.particlePhysics = (sub == 4) ? DeathParticlePhysics::Gravity : DeathParticlePhysics::Explode;
        s.cameraShake = 0.65f;
        s.cameraZoom = (sub == 6 || sub == 7) ? DeathCameraZoom::PunchIn : DeathCameraZoom::RecoilOut;
        s.cameraTilt = DeathCameraTilt::HardKick;
        s.screenFx = (sub == 6) ? DeathScreenFx::GDPinch : DeathScreenFx::GDShockwave;
    } else { // Cinema (32..39)
        s.particleShape = (sub == 4) ? DeathParticleShape::Rings : DeathParticleShape::Stars;
        s.particlePhysics = (sub == 4 || sub == 6) ? DeathParticlePhysics::FloatUp : DeathParticlePhysics::Explode;
        s.cameraShake = (sub == 7) ? 0.6f : 0.3f;
        s.cameraZoom = DeathCameraZoom::DramaticIn;
        s.enableGhostIcon = (sub == 4 || sub == 5);
        s.screenFx = (sub == 6) ? DeathScreenFx::GDColorShift :
                     (sub == 7) ? DeathScreenFx::GDInvert : DeathScreenFx::VignettePulse;
    }
    return s;
}

namespace {
DeathVfxConfig g_vfxConfig;
bool g_vfxLoaded = false;

void serializeStyle(matjson::Value& obj, DeathStyleSettings const& s) {
    obj["colorMode"] = static_cast<int>(s.colorMode);
    obj["p1R"] = static_cast<int>(s.customPrimary.r);
    obj["p1G"] = static_cast<int>(s.customPrimary.g);
    obj["p1B"] = static_cast<int>(s.customPrimary.b);
    obj["p2R"] = static_cast<int>(s.customSecondary.r);
    obj["p2G"] = static_cast<int>(s.customSecondary.g);
    obj["p2B"] = static_cast<int>(s.customSecondary.b);
    obj["scale"] = s.scale;
    obj["speed"] = s.speed;
    obj["intensity"] = s.intensity;
    obj["spin"] = s.spin;
    obj["keepVanilla"] = s.keepVanillaExplosion;
    obj["partShape"] = static_cast<int>(s.particleShape);
    obj["partPhys"] = static_cast<int>(s.particlePhysics);
    obj["partCount"] = s.particleCount;
    obj["shockRing"] = s.enableShockRing;
    obj["ghostIcon"] = s.enableGhostIcon;
    obj["camShake"] = s.cameraShake;
    obj["camZoom"] = static_cast<int>(s.cameraZoom);
    obj["camTilt"] = static_cast<int>(s.cameraTilt);
    obj["screenFx"] = static_cast<int>(s.screenFx);
    obj["fxStrength"] = s.screenFxStrength;
    obj["fxDuration"] = s.screenFxDuration;
}

DeathStyleSettings parseStyle(matjson::Value const& j, DeathStyleSettings base) {
    auto clampB = [](int v) { return static_cast<GLubyte>(std::clamp(v, 0, 255)); };
    base.colorMode = static_cast<DeathColorMode>(std::clamp<int>(static_cast<int>(j["colorMode"].asInt().unwrapOr(static_cast<int>(base.colorMode))), 0, 4));
    base.customPrimary = {
        clampB(static_cast<int>(j["p1R"].asInt().unwrapOr(base.customPrimary.r))),
        clampB(static_cast<int>(j["p1G"].asInt().unwrapOr(base.customPrimary.g))),
        clampB(static_cast<int>(j["p1B"].asInt().unwrapOr(base.customPrimary.b))),
    };
    base.customSecondary = {
        clampB(static_cast<int>(j["p2R"].asInt().unwrapOr(base.customSecondary.r))),
        clampB(static_cast<int>(j["p2G"].asInt().unwrapOr(base.customSecondary.g))),
        clampB(static_cast<int>(j["p2B"].asInt().unwrapOr(base.customSecondary.b))),
    };
    base.scale = std::clamp(static_cast<float>(j["scale"].asDouble().unwrapOr(base.scale)), 0.35f, 2.5f);
    base.speed = std::clamp(static_cast<float>(j["speed"].asDouble().unwrapOr(base.speed)), 0.4f, 2.2f);
    base.intensity = std::clamp(static_cast<float>(j["intensity"].asDouble().unwrapOr(base.intensity)), 0.3f, 1.8f);
    base.spin = std::clamp(static_cast<float>(j["spin"].asDouble().unwrapOr(base.spin)), -3.0f, 3.0f);
    base.keepVanillaExplosion = j["keepVanilla"].asBool().unwrapOr(base.keepVanillaExplosion);
    base.particleShape = static_cast<DeathParticleShape>(std::clamp<int>(static_cast<int>(j["partShape"].asInt().unwrapOr(static_cast<int>(base.particleShape))), 0, 7));
    base.particlePhysics = static_cast<DeathParticlePhysics>(std::clamp<int>(static_cast<int>(j["partPhys"].asInt().unwrapOr(static_cast<int>(base.particlePhysics))), 0, 3));
    base.particleCount = std::clamp<int>(static_cast<int>(j["partCount"].asInt().unwrapOr(base.particleCount)), 0, 48);
    base.enableShockRing = j["shockRing"].asBool().unwrapOr(base.enableShockRing);
    base.enableGhostIcon = j["ghostIcon"].asBool().unwrapOr(base.enableGhostIcon);
    base.cameraShake = std::clamp(static_cast<float>(j["camShake"].asDouble().unwrapOr(base.cameraShake)), 0.f, 1.f);
    base.cameraZoom = static_cast<DeathCameraZoom>(std::clamp<int>(static_cast<int>(j["camZoom"].asInt().unwrapOr(static_cast<int>(base.cameraZoom))), 0, 3));
    base.cameraTilt = static_cast<DeathCameraTilt>(std::clamp<int>(static_cast<int>(j["camTilt"].asInt().unwrapOr(static_cast<int>(base.cameraTilt))), 0, 2));
    base.screenFx = static_cast<DeathScreenFx>(std::clamp<int>(static_cast<int>(j["screenFx"].asInt().unwrapOr(static_cast<int>(base.screenFx))), 0, 11));
    base.screenFxStrength = std::clamp(static_cast<float>(j["fxStrength"].asDouble().unwrapOr(base.screenFxStrength)), 0.1f, 1.5f);
    base.screenFxDuration = std::clamp(static_cast<float>(j["fxDuration"].asDouble().unwrapOr(base.screenFxDuration)), 0.15f, 1.2f);
    return base;
}
} // namespace

DeathVfxConfig& vfxConfig() {
    if (!g_vfxLoaded) loadVfxConfig();
    return g_vfxConfig;
}

DeathStyleSettings& currentStyleSettings() {
    auto& cfg = vfxConfig();
    int key = cfg.style;
    auto it = cfg.styleOverrides.find(key);
    if (it != cfg.styleOverrides.end()) {
        return it->second;
    }
    auto [inserted, _] = cfg.styleOverrides.emplace(key, defaultStyleSettings(key));
    return inserted->second;
}

void resetCurrentStyleToDefault() {
    auto& cfg = vfxConfig();
    cfg.styleOverrides[cfg.style] = defaultStyleSettings(cfg.style);
    saveVfxConfig();
}

void loadVfxConfig() {
    g_vfxLoaded = true;
    auto path = vfxConfigPath();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        int legacy = Mod::get()->getSavedValue<int>(kLegacySelectionKey, -1);
        g_vfxConfig.style = (legacy >= 0 && legacy < kDeathStyleCount) ? legacy : -1;
        return;
    }

    auto raw = geode::utils::file::readString(paimon::paths::toSdkPath(path));
    if (!raw) return;
    auto parsed = matjson::parse(raw.unwrap());
    if (parsed.isErr()) return;
    auto j = parsed.unwrap();

    g_vfxConfig.style = std::clamp<int>(static_cast<int>(j["style"].asInt().unwrapOr(-1)), -1, kStyleCustomMedia);
    g_vfxConfig.customMediaPath = j["customMediaPath"].asString().unwrapOr("");
    g_vfxConfig.styleOverrides.clear();

    if (j["overrides"].isObject()) {
        for (auto const& [kStr, val] : j["overrides"]) {
            auto parsedKey = geode::utils::numFromString<int>(kStr);
            if (!parsedKey) continue;
            int k = parsedKey.unwrap();
            if (k < -1 || k > kStyleCustomMedia) continue;
            g_vfxConfig.styleOverrides[k] = parseStyle(val, defaultStyleSettings(k));
        }
    }
}

void saveVfxConfig() {
    auto const& c = vfxConfig();
    Mod::get()->setSavedValue(kLegacySelectionKey, c.style);

    matjson::Value j = matjson::makeObject({});
    j["style"] = c.style;
    j["customMediaPath"] = c.customMediaPath;
    matjson::Value ov = matjson::makeObject({});
    for (auto const& [k, s] : c.styleOverrides) {
        matjson::Value entry = matjson::makeObject({});
        serializeStyle(entry, s);
        ov[std::to_string(k)] = std::move(entry);
    }
    j["overrides"] = std::move(ov);

    auto path = vfxConfigPath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    paimon::file::writeAtomically(path, std::string_view(j.dump()));
}

int selectedAnimation() {
    return vfxConfig().style;
}

int resolveAnimation(int selection) {
    if (selection != kStyleRandom) return selection;
    static std::mt19937 rng(std::random_device{}());
    static int previous = -1;
    int next = std::uniform_int_distribution<int>(0, previous < 0 ? kDeathStyleCount - 1 : kDeathStyleCount - 2)(rng);
    if (previous >= 0 && next >= previous) ++next;
    previous = next;
    return next;
}

void prewarmAnimation() {
    if (selectedAnimation() >= 0) (void)program();
}

bool shouldSuppressVanillaExplosion() {
    auto const& cfg = vfxConfig();
    if (cfg.style < 0) return false;
    return !currentStyleSettings().keepVanillaExplosion;
}

bool spawnAnimation(CCNode* parent, CCPoint position, int style, ccColor3B color, float scale) {
    return spawnAnimationDual(parent, position, style, color, makeComplementary(color), scale);
}

bool spawnAnimationDual(
    CCNode* parent,
    CCPoint position,
    int style,
    ccColor3B primaryColor,
    ccColor3B secondaryColor,
    float scale
) {
    if (!parent || style < 0) return false;
    auto const& cfg = vfxConfig();
    auto const& s = currentStyleSettings();

    auto* container = parent->getChildByID(kContainerId);
    if (!container) {
        container = CCNode::create();
        container->setID(kContainerId);
        parent->addChild(container, 2500);
    } else {
        container->setZOrder(2500);
    }
    if (container->getChildrenCount() >= 8) {
        container->removeAllChildrenWithCleanup(true);
    }

    float const effScale = std::clamp(scale * s.scale, 0.25f, 3.0f);
    float const durationSec = std::clamp(0.85f / s.speed, 0.3f, 2.2f);
    ccColor3B col1{}, col2{};
    resolveEffectColors(s, primaryColor, secondaryColor, col1, col2);

    bool spawned = false;
    if (style == kStyleCustomMedia) {
        if (!cfg.customMediaPath.empty()) {
            std::filesystem::path fsPath = paimon::assets::pathFromUtf8(cfg.customMediaPath);
            std::error_code ec;
            if (std::filesystem::exists(fsPath, ec)) {
                CCNode* mediaNode = nullptr;
                if (ImageLoadHelper::isAnimatedImage(fsPath)) {
                    mediaNode = AnimatedGIFSprite::create(cfg.customMediaPath);
                } else {
                    auto img = ImageLoadHelper::loadStaticImage(fsPath, 16);
                    if (img.success && img.texture) {
                        mediaNode = CCSprite::createWithTexture(img.texture);
                        img.texture->release();
                    }
                }
                if (mediaNode) {
                    auto sz = mediaNode->getContentSize();
                    float targetSize = 110.f * effScale;
                    float baseScale = targetSize / std::max(1.f, std::max(sz.width, sz.height));
                    mediaNode->setPosition(position);
                    mediaNode->setScale(baseScale * 0.25f);
                    mediaNode->runAction(CCSequence::create(
                        CCSpawn::create(
                            CCEaseBackOut::create(CCScaleTo::create(durationSec * 0.35f, baseScale)),
                            CCFadeIn::create(durationSec * 0.15f),
                            nullptr
                        ),
                        CCDelayTime::create(durationSec * 0.35f),
                        CCSpawn::create(
                            CCScaleTo::create(durationSec * 0.30f, baseScale * 1.25f),
                            CCFadeOut::create(durationSec * 0.30f),
                            nullptr
                        ),
                        CCRemoveSelf::create(),
                        nullptr
                    ));
                    container->addChild(mediaNode, 2);
                    spawned = true;
                }
            }
        }
        if (!spawned) style = 0;
    }

    if (!spawned && style >= 0 && style < kDeathStyleCount) {
        static std::mt19937 seedRng(std::random_device{}());
        float seed = std::uniform_real_distribution<float>(0.f, 100.f)(seedRng);
        if (auto* effect = AnimationSprite::create(style, col1, col2, durationSec, s.intensity, seed, s.spin)) {
            effect->setPosition(position);
            effect->setScale(190.f * effScale);
            container->addChild(effect, 2);
            spawned = true;
        }
    }

    if (spawned && (s.enableShockRing || (s.particleShape != DeathParticleShape::None && s.particleCount > 0))) {
        if (auto* parts = RichParticlesNode::create(s, col1, col2, effScale, durationSec)) {
            parts->setPosition(position);
            container->addChild(parts, 3);
        }
    }

    if (spawned && s.enableGhostIcon) {
        spawnGhostEcho(container, position, col1, effScale, durationSec);
    }

    return spawned;
}

bool triggerGameplayDeathEffects(PlayLayer* play, PlayerObject* player, CCPoint positionInObjLayer) {
    if (!play || !play->m_objectLayer || !player) return false;
    int sel = selectedAnimation();
    if (sel < 0) return false;

    int resolved = resolveAnimation(sel);
    auto const& s = currentStyleSettings();
    ccColor3B c1{}, c2{};
    resolveEffectColors(s, player->getColor(), player->getSecondColor(), c1, c2);

    bool ok = spawnAnimationDual(play->m_objectLayer, positionInObjLayer, resolved,
        player->getColor(), player->getSecondColor(), 1.f);
    if (!ok) return false;

    // 1. Camera FX: Shake, Zoom, Tilt
    float const dur = std::clamp(s.screenFxDuration, 0.15f, 1.2f);
    if (s.cameraShake > 0.02f) {
        play->shakeCamera(std::clamp(dur * 0.7f, 0.12f, 0.55f), 8.f * s.cameraShake, 0.f);
    }
    if (s.cameraZoom != DeathCameraZoom::None) {
        float targetZoom = 1.0f;
        if (s.cameraZoom == DeathCameraZoom::PunchIn) targetZoom = 1.14f;
        else if (s.cameraZoom == DeathCameraZoom::RecoilOut) targetZoom = 0.88f;
        else if (s.cameraZoom == DeathCameraZoom::DramaticIn) targetZoom = 1.24f;
        play->updateZoom(targetZoom, std::clamp(dur * 0.45f, 0.08f, 0.35f), 2, 2.f, 0, 0);
    }
    if (s.cameraTilt != DeathCameraTilt::None) {
        float deg = (s.cameraTilt == DeathCameraTilt::HardKick) ? 6.5f : 2.8f;
        static std::mt19937 rng(std::random_device{}());
        if (std::uniform_int_distribution<int>(0, 1)(rng) == 1) deg = -deg;
        play->stopActionByTag(kCameraTiltActionTag);
        play->setRotation(deg);
        auto* tiltAct = CCEaseOut::create(CCRotateTo::create(dur * 0.8f, 0.f), 2.f);
        tiltAct->setTag(kCameraTiltActionTag);
        play->runAction(tiltAct);
    }

    // 2. Native GD Shaders & Screen Post-FX
    float const st = std::clamp(s.screenFxStrength, 0.1f, 1.5f);
    CCPoint worldPos = play->m_objectLayer->convertToWorldSpace(positionInObjLayer);
    auto winSize = CCDirector::get()->getWinSize();
    float const normX = winSize.width > 1.f ? std::clamp(worldPos.x / winSize.width, 0.05f, 0.95f) : 0.5f;
    float const normY = winSize.height > 1.f ? std::clamp(worldPos.y / winSize.height, 0.05f, 0.95f) : 0.5f;

    bool const isGdShader = s.screenFx >= DeathScreenFx::GDShockwave && s.screenFx <= DeathScreenFx::GDColorShift;
    bool const allowGdShaders = !paimon::gameplayperf::isOptionActive(paimon::gameplayperf::kShadersModuleId);
    auto* sh = allowGdShaders ? play->m_shaderLayer : nullptr;
    if (sh && isGdShader && !sh->m_state.m_usesShaders) {
        sh->setupShader(false);
    }

    bool triggeredShader = false;
    switch (s.screenFx) {
        case DeathScreenFx::GDShockwave:
            if (sh) {
                sh->triggerShockWave(1.6f, 0.07f * st, 0.25f * st, 0.45f, 0.02f, dur * 0.6f,
                    0.f, false, 0.f, normX, normY, 0, false, false, false, 1.2f, 1.4f, true, dur, 1, 2.f);
                triggeredShader = true;
            }
            break;
        case DeathScreenFx::GDShockLine:
            if (sh) {
                sh->triggerShockLine(1.8f, 0.08f * st, false, false, false, false, 0.25f * st, 0.4f,
                    0.02f, dur * 0.6f, 0.f, normY, 0, false, false, false, 1.2f, true, dur, 1, 2.f);
                triggeredShader = true;
            }
            break;
        case DeathScreenFx::GDChromaticGlitch:
            if (sh) {
                sh->triggerChromaticGlitch(true, dur, 24.f, 0.45f * st, 0.022f * st, 0.04f, 0.6f, 0.08f, 1, 2.f, false, false);
                triggeredShader = true;
            }
            break;
        case DeathScreenFx::GDRadialBlur:
            if (sh) {
                sh->triggerRadialBlur(dur * 0.55f, 0.35f * st, 0.6f * st, dur * 0.45f, 0, normX, normY, false, 0, 2, 2.f, false);
                triggeredShader = true;
            }
            break;
        case DeathScreenFx::GDBulge:
            if (sh) {
                sh->triggerBulge(dur * 0.45f, 0.35f * st, normX, normY, 0.45f, 0, 2, 2.f, false);
                triggeredShader = true;
            }
            break;
        case DeathScreenFx::GDPinch:
            if (sh) {
                sh->triggerPinchX(dur * 0.45f, -0.35f * st, normX, 0.45f, 1.f, false, 0, 2, 2.f, false);
                sh->triggerPinchY(dur * 0.45f, -0.35f * st, normY, 0.45f, 1.f, false, 0, 2, 2.f, false);
                triggeredShader = true;
            }
            break;
        case DeathScreenFx::GDInvert:
            if (sh) {
                sh->triggerInvertColor(dur * 0.35f, 0.85f * std::min(1.f, st), 1.f, 1.f, 1.f, true, true, true, 1, 2.f);
                triggeredShader = true;
            }
            break;
        case DeathScreenFx::GDLensCircle:
            if (sh) {
                sh->triggerLensCircle(dur * 0.45f, 0.55f, 0.35f, 0.55f * st, 0, 0, normX, normY, 2, 2.f, false);
                triggeredShader = true;
            }
            break;
        case DeathScreenFx::GDColorShift:
            if (sh) {
                sh->triggerColorChange(dur * 0.4f, c1.r / 255.f, c1.g / 255.f, c1.b / 255.f,
                    c2.r / 255.f * 0.2f, c2.g / 255.f * 0.2f, c2.b / 255.f * 0.2f, 1, 2.f);
                triggeredShader = true;
            }
            break;
        case DeathScreenFx::VignettePulse:
        case DeathScreenFx::ImpactFlash:
            spawnScreenOverlayFx(play, winSize, s.screenFx, c1, st, dur);
            break;
        case DeathScreenFx::None:
            break;
    }

    if (triggeredShader && sh) {
        sh->updateZLayer(-10, 15, false);
        s_gdShaderTriggeredOnDeath = true;
    } else if (isGdShader) {
        spawnScreenOverlayFx(play, winSize, s.screenFx, c1, st * 0.7f, dur);
    }

    return true;
}

void clearAnimations(CCNode* parent) {
    if (parent) parent->removeChildByID(kContainerId);
}

void clearGameplayDeathEffects(PlayLayer* play) {
    if (!play) return;
    if (play->m_objectLayer) {
        clearAnimations(play->m_objectLayer);
    }
    play->stopActionByTag(kCameraTiltActionTag);
    play->setRotation(0.f);
    play->removeChildByID(kScreenOverlayId);
    if (s_gdShaderTriggeredOnDeath && play->m_shaderLayer) {
        play->m_shaderLayer->resetAllShaders();
        s_gdShaderTriggeredOnDeath = false;
    }
}

// ============================================================================
// Death VFX Studio Popup (40 Editable Styles + Camera FX + Shader FX)
// ============================================================================

DeathAnimationPopup* DeathAnimationPopup::create() {
    auto* ret = new DeathAnimationPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool DeathAnimationPopup::init() {
    if (!PaimonPopup::init(520.f, 326.f)) return false;
    loadVfxConfig();
    setTitle("Death VFX Studio", "goldFont.fnt", 0.68f);
    setID("death-animation-popup"_spr);
    this->addInfoButton("Death VFX Studio",
        "40 editable death styles organized across 5 families, with full control over\n"
        "<cg>Particles</c>, <cy>Camera Shake/Zoom/Tilt</c>, and <cl>Native GD Shaders</c>.\n"
        "Tap anywhere inside the preview stage to test!");
    this->setTouchEnabled(true);

    auto content = m_mainLayer->getContentSize();

    // 4 Left Sub-tabs
    auto* tabMenu = CCMenu::create();
    tabMenu->setPosition(ccp(0.f, 0.f));
    m_mainLayer->addChild(tabMenu, 10);

    char const* tabNames[4] = {"Styles", "Visual", "Particles", "Cam & FX"};
    std::vector<CCMenuItem*> tabItems;
    for (int i = 0; i < 4; ++i) {
        auto* btn = paimon::ui::makeButton(
            tabNames[i],
            [this, i]() {
                if (m_subTabBtns[i]) onSubTabSwitch(m_subTabBtns[i]);
            },
            paimon::ui::Btn::Gray, 62.f, 0.60f, "bigFont.fnt"
        );
        if (!btn) continue;
        btn->setTag(i);
        tabMenu->addChild(btn);
        m_subTabBtns[i] = btn;
        tabItems.push_back(btn);
    }
    paimon::ui::matchButtonLabels(
        std::vector<CCMenuItemSpriteExtra*>(m_subTabBtns.begin(), m_subTabBtns.end()));
    paimon::ui::layoutButtonRow(tabItems, 16.f, 286.f, content.height - 46.f, 6.f, paimon::ui::RowAlign::Center);

    for (int i = 0; i < 4; ++i) {
        auto* pane = CCNode::create();
        pane->setContentSize({290.f, 260.f});
        pane->setPosition(ccp(0.f, 0.f));
        pane->setVisible(i == 0);
        m_mainLayer->addChild(pane, 4);
        m_subPanes[i] = pane;
    }

    buildStylesPane(m_subPanes[0]);
    buildVisualPane(m_subPanes[1]);
    buildParticlesPane(m_subPanes[2]);
    buildCamFxPane(m_subPanes[3]);
    buildPreviewStage();

    m_statusLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_statusLabel->setScale(0.29f);
    m_statusLabel->setPosition(ccp(content.width * 0.5f, 14.f));
    m_mainLayer->addChild(m_statusLabel, 6);

    onSubTabSwitch(m_subTabBtns[0]);
    refreshUI();
    triggerPreviewAt(ccp(0.f, 0.f));
    this->scheduleUpdate();
    return true;
}

void DeathAnimationPopup::onExit() {
    this->unscheduleUpdate();
    saveVfxConfig();
    Popup::onExit();
}

void DeathAnimationPopup::buildStylesPane(CCNode* pane) {
    // 6 category filter chips cleanly aligned above the ScrollLayer
    auto* catMenu = CCMenu::create();
    catMenu->setPosition(ccp(0.f, 0.f));
    pane->addChild(catMenu, 6);

    char const* catNames[6] = {"All", "Cosmic", "Cyber", "Elem", "Impact", "Cine"};
    m_catBtns.clear();
    std::vector<CCMenuItem*> catItems;
    for (int i = 0; i < 6; ++i) {
        auto* btn = paimon::ui::makeButton(
            catNames[i],
            [this, i]() {
                if (i >= 0 && i < static_cast<int>(m_catBtns.size())) {
                    onSelectCategory(m_catBtns[i]);
                }
            },
            paimon::ui::Btn::Gray, 41.f, 0.56f, "bigFont.fnt"
        );
        if (!btn) continue;
        btn->setTag(i);
        catMenu->addChild(btn);
        m_catBtns.push_back(btn);
        catItems.push_back(btn);
    }
    paimon::ui::matchButtonLabels(m_catBtns);
    paimon::ui::layoutButtonRow(catItems, 16.f, 286.f, 249.f, 4.f, paimon::ui::RowAlign::Center);

    float const listX = 18.f;
    float const listY = 30.f;
    float const listW = 264.f;
    float const listH = 196.f;

    if (auto* inset = paimon::ui::makeInset({listW + 6.f, listH + 6.f}, 95)) {
        inset->setPosition(ccp(listX - 3.f, listY - 3.f));
        pane->addChild(inset, 0);
    }

    m_styleScroll = ScrollLayer::create({listW, listH});
    m_styleScroll->setAnchorPoint(ccp(0.f, 0.f));
    m_styleScroll->setPosition(ccp(listX, listY));
    pane->addChild(m_styleScroll, 2);

    auto* borders = ListBorders::create();
    borders->setContentSize({listW + 4.f, listH + 2.f});
    borders->setAnchorPoint(ccp(0.f, 0.f));
    borders->setPosition(ccp(listX - 2.f, listY - 1.f));
    pane->addChild(borders, 3);

    rebuildStyleGrid();
}

void DeathAnimationPopup::rebuildStyleGrid() {
    if (!m_styleScroll) return;
    auto* content = m_styleScroll->m_contentLayer;
    content->removeAllChildrenWithCleanup(true);
    m_styleButtons.clear();

    struct ItemDef { char const* name; int tag; };
    std::vector<ItemDef> items;
    if (m_categoryFilter == 0) {
        items.push_back({"Original", -1});
        items.push_back({"Random FX", kStyleRandom});
        items.push_back({"Custom Media", kStyleCustomMedia});
        for (int i = 0; i < kDeathStyleCount; ++i) {
            items.push_back({kAnimationNames[i], i});
        }
    } else {
        int start = (m_categoryFilter - 1) * 8;
        int end = std::min(kDeathStyleCount, start + 8);
        for (int i = start; i < end; ++i) {
            items.push_back({kAnimationNames[i], i});
        }
    }

    float const listW = 264.f;
    float const listH = 196.f;
    float const rowH = 32.f;
    int const rows = static_cast<int>((items.size() + 1) / 2);
    float const totalH = std::max(listH, rows * rowH + 8.f);
    content->setContentSize({listW, totalH});

    for (std::size_t idx = 0; idx < items.size(); ++idx) {
        int col = static_cast<int>(idx % 2);
        int row = static_cast<int>(idx / 2);
        float yRow = totalH - 4.f - (row + 1) * rowH;

        auto* rowNode = CCNode::create();
        rowNode->setContentSize({listW * 0.5f, rowH});
        rowNode->setPosition(ccp(col * (listW * 0.5f), yRow));
        content->addChild(rowNode);

        auto* menu = CCMenu::create();
        menu->ignoreAnchorPointForPosition(true);
        menu->setAnchorPoint(ccp(0.f, 0.f));
        menu->setPosition(ccp(0.f, 0.f));
        menu->setContentSize({listW * 0.5f, rowH});
        rowNode->addChild(menu);

        auto* spr = ButtonSprite::create(items[idx].name, 102, true, "bigFont.fnt", "GJ_button_04.png", 20.f, 0.32f);
        auto* btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(DeathAnimationPopup::onSelectStyle));
        btn->setTag(items[idx].tag);
        float const maxBtnW = listW * 0.5f - 8.f;
        float const btnW = btn->getScaledContentSize().width;
        if (btnW > maxBtnW && btnW > 0.f) {
            float const s = btn->getScale() * (maxBtnW / btnW);
            btn->setScale(s);
            btn->m_baseScale = s;
        }
        btn->setPosition(ccp(listW * 0.25f, rowH * 0.5f));
        menu->addChild(btn);
        m_styleButtons.push_back(btn);
    }

    m_styleScroll->scrollToTop();
}

void DeathAnimationPopup::buildVisualPane(CCNode* pane) {
    if (auto* bg = paimon::ui::makeInset({268.f, 224.f}, 90)) {
        bg->setPosition(ccp(16.f, 30.f));
        pane->addChild(bg, -1);
    }
    auto* menu = CCMenu::create();
    menu->setPosition(ccp(0.f, 0.f));
    pane->addChild(menu, 3);

    auto const& s = currentStyleSettings();

    auto* cLbl = CCLabelBMFont::create("Color", "bigFont.fnt");
    cLbl->setScale(0.32f); cLbl->setAnchorPoint(ccp(0.f, 0.5f)); cLbl->setPosition(ccp(26.f, 236.f));
    pane->addChild(cLbl);

    auto* cmSpr = ButtonSprite::create(colorModeLabel(s.colorMode), 84, true, "bigFont.fnt", "GJ_button_04.png", 18.f, 0.32f);
    m_colorModeBtn = CCMenuItemSpriteExtra::create(cmSpr, this, menu_selector(DeathAnimationPopup::onCycleColorMode));
    menu->addChild(m_colorModeBtn);

    m_primarySwatch = makeSwatch(s.customPrimary);
    auto* p1Btn = CCMenuItemSpriteExtra::create(m_primarySwatch, this, menu_selector(DeathAnimationPopup::onPickPrimary));
    menu->addChild(p1Btn);

    m_secondarySwatch = makeSwatch(s.customSecondary);
    auto* p2Btn = CCMenuItemSpriteExtra::create(m_secondarySwatch, this, menu_selector(DeathAnimationPopup::onPickSecondary));
    menu->addChild(p2Btn);

    paimon::ui::layoutButtonRow({m_colorModeBtn, p1Btn, p2Btn}, 74.f, 276.f, 236.f, 8.f, paimon::ui::RowAlign::Left);

    auto addSlider = [&](char const* name, float y, Slider*& sl, CCLabelBMFont*& vl, SEL_MenuHandler cb) {
        auto* lbl = CCLabelBMFont::create(name, "bigFont.fnt");
        lbl->setScale(0.31f); lbl->setAnchorPoint(ccp(0.f, 0.5f)); lbl->setPosition(ccp(26.f, y));
        pane->addChild(lbl);
        sl = Slider::create(this, cb, 0.48f);
        sl->setPosition(ccp(150.f, y));
        pane->addChild(sl);
        vl = CCLabelBMFont::create("", "bigFont.fnt");
        vl->setScale(0.30f); vl->setAnchorPoint(ccp(0.f, 0.5f)); vl->setPosition(ccp(228.f, y));
        pane->addChild(vl);
    };
    addSlider("Scale",     204.f, m_scaleSlider,     m_scaleLbl,     menu_selector(DeathAnimationPopup::onScaleChanged));
    addSlider("Speed",     174.f, m_speedSlider,     m_speedLbl,     menu_selector(DeathAnimationPopup::onSpeedChanged));
    addSlider("Intensity", 144.f, m_intensitySlider, m_intensityLbl, menu_selector(DeathAnimationPopup::onIntensityChanged));
    addSlider("Spin",      114.f, m_spinSlider,      m_spinLbl,      menu_selector(DeathAnimationPopup::onSpinChanged));

    m_keepVanillaToggle = CCMenuItemToggler::createWithStandardSprites(
        this, menu_selector(DeathAnimationPopup::onToggleKeepVanilla), 0.50f);
    m_keepVanillaToggle->setPosition(ccp(36.f, 82.f));
    menu->addChild(m_keepVanillaToggle);
    auto* kvLbl = CCLabelBMFont::create("Keep Vanilla Explosion", "bigFont.fnt");
    kvLbl->setScale(0.30f); kvLbl->setAnchorPoint(ccp(0.f, 0.5f)); kvLbl->setPosition(ccp(52.f, 82.f));
    pane->addChild(kvLbl);

    auto* mSpr = ButtonSprite::create("Pick PNG/GIF", 78, true, "goldFont.fnt", "GJ_button_04.png", 18.f, 0.30f);
    auto* mBtn = CCMenuItemSpriteExtra::create(mSpr, this, menu_selector(DeathAnimationPopup::onPickCustomMedia));
    menu->addChild(mBtn);

    auto* xSpr = ButtonSprite::create("X", 22, true, "bigFont.fnt", "GJ_button_06.png", 18.f, 0.30f);
    auto* xBtn = CCMenuItemSpriteExtra::create(xSpr, this, menu_selector(DeathAnimationPopup::onClearCustomMedia));
    menu->addChild(xBtn);

    paimon::ui::layoutButtonRow({mBtn, xBtn}, 26.f, 152.f, 48.f, 6.f, paimon::ui::RowAlign::Left);

    m_mediaPathLbl = CCLabelBMFont::create("", "chatFont.fnt");
    m_mediaPathLbl->setScale(0.42f); m_mediaPathLbl->setAnchorPoint(ccp(0.f, 0.5f));
    m_mediaPathLbl->setPosition(ccp(xBtn->getPositionX() + xBtn->getScaledContentSize().width * 0.5f + 8.f, 48.f));
    m_mediaPathLbl->setOpacity(180);
    pane->addChild(m_mediaPathLbl);
}

void DeathAnimationPopup::buildParticlesPane(CCNode* pane) {
    if (auto* bg = paimon::ui::makeInset({268.f, 224.f}, 90)) {
        bg->setPosition(ccp(16.f, 30.f));
        pane->addChild(bg, -1);
    }
    auto* menu = CCMenu::create();
    menu->setPosition(ccp(0.f, 0.f));
    pane->addChild(menu, 3);

    auto addRowBtn = [&](char const* name, float y, CCMenuItemSpriteExtra*& btn, SEL_MenuHandler cb) {
        auto* lbl = CCLabelBMFont::create(name, "bigFont.fnt");
        lbl->setScale(0.32f); lbl->setAnchorPoint(ccp(0.f, 0.5f)); lbl->setPosition(ccp(26.f, y));
        pane->addChild(lbl);
        auto* spr = ButtonSprite::create("", 118, true, "bigFont.fnt", "GJ_button_01.png", 19.f, 0.32f);
        btn = CCMenuItemSpriteExtra::create(spr, this, cb);
        btn->setPosition(ccp(204.f, y));
        menu->addChild(btn);
    };
    addRowBtn("Shape",   232.f, m_partShapeBtn, menu_selector(DeathAnimationPopup::onCyclePartShape));
    addRowBtn("Physics", 196.f, m_partPhysBtn,  menu_selector(DeathAnimationPopup::onCyclePartPhysics));

    auto* cLbl = CCLabelBMFont::create("Count", "bigFont.fnt");
    cLbl->setScale(0.32f); cLbl->setAnchorPoint(ccp(0.f, 0.5f)); cLbl->setPosition(ccp(26.f, 158.f));
    pane->addChild(cLbl);
    m_partCountSlider = Slider::create(this, menu_selector(DeathAnimationPopup::onPartCountChanged), 0.48f);
    m_partCountSlider->setPosition(ccp(150.f, 158.f));
    pane->addChild(m_partCountSlider);
    m_partCountLbl = CCLabelBMFont::create("", "bigFont.fnt");
    m_partCountLbl->setScale(0.31f); m_partCountLbl->setAnchorPoint(ccp(0.f, 0.5f)); m_partCountLbl->setPosition(ccp(230.f, 158.f));
    pane->addChild(m_partCountLbl);

    auto addTog = [&](char const* title, float y, CCMenuItemToggler*& out, SEL_MenuHandler cb) {
        out = CCMenuItemToggler::createWithStandardSprites(this, cb, 0.52f);
        out->setPosition(ccp(36.f, y));
        menu->addChild(out);
        auto* lbl = CCLabelBMFont::create(title, "bigFont.fnt");
        lbl->setScale(0.31f); lbl->setAnchorPoint(ccp(0.f, 0.5f)); lbl->setPosition(ccp(54.f, y));
        pane->addChild(lbl);
    };
    addTog("Expanding Shock Ring",  112.f, m_shockRingToggle, menu_selector(DeathAnimationPopup::onToggleShockRing));
    addTog("Player Soul Echo Icon",  76.f, m_ghostIconToggle, menu_selector(DeathAnimationPopup::onToggleGhostIcon));
}

void DeathAnimationPopup::buildCamFxPane(CCNode* pane) {
    if (auto* bg = paimon::ui::makeInset({268.f, 224.f}, 90)) {
        bg->setPosition(ccp(16.f, 30.f));
        pane->addChild(bg, -1);
    }
    auto* menu = CCMenu::create();
    menu->setPosition(ccp(0.f, 0.f));
    pane->addChild(menu, 3);

    auto addSlider = [&](char const* name, float y, Slider*& sl, CCLabelBMFont*& vl, SEL_MenuHandler cb) {
        auto* lbl = CCLabelBMFont::create(name, "bigFont.fnt");
        lbl->setScale(0.30f); lbl->setAnchorPoint(ccp(0.f, 0.5f)); lbl->setPosition(ccp(26.f, y));
        pane->addChild(lbl);
        sl = Slider::create(this, cb, 0.46f);
        sl->setPosition(ccp(152.f, y));
        pane->addChild(sl);
        vl = CCLabelBMFont::create("", "bigFont.fnt");
        vl->setScale(0.30f); vl->setAnchorPoint(ccp(0.f, 0.5f)); vl->setPosition(ccp(230.f, y));
        pane->addChild(vl);
    };
    addSlider("Cam Shake", 236.f, m_camShakeSlider, m_camShakeLbl, menu_selector(DeathAnimationPopup::onCamShakeChanged));

    auto addCycle = [&](char const* name, float y, CCMenuItemSpriteExtra*& btn, SEL_MenuHandler cb) {
        auto* lbl = CCLabelBMFont::create(name, "bigFont.fnt");
        lbl->setScale(0.30f); lbl->setAnchorPoint(ccp(0.f, 0.5f)); lbl->setPosition(ccp(26.f, y));
        pane->addChild(lbl);
        auto* spr = ButtonSprite::create("", 118, true, "bigFont.fnt", "GJ_button_01.png", 18.f, 0.30f);
        btn = CCMenuItemSpriteExtra::create(spr, this, cb);
        btn->setPosition(ccp(204.f, y));
        menu->addChild(btn);
    };
    addCycle("Cam Zoom",     204.f, m_camZoomBtn,  menu_selector(DeathAnimationPopup::onCycleCamZoom));
    addCycle("Cam Tilt",     172.f, m_camTiltBtn,  menu_selector(DeathAnimationPopup::onCycleCamTilt));
    addCycle("GD Shader FX", 136.f, m_screenFxBtn, menu_selector(DeathAnimationPopup::onCycleScreenFx));

    addSlider("FX Power",    98.f, m_fxStrengthSlider, m_fxStrengthLbl, menu_selector(DeathAnimationPopup::onScreenFxStrengthChanged));
    addSlider("FX Time",     64.f, m_fxDurSlider,      m_fxDurLbl,      menu_selector(DeathAnimationPopup::onScreenFxDurationChanged));
}

void DeathAnimationPopup::buildPreviewStage() {
    float const stageX = 296.f;
    float const stageY = 94.f;
    float const stageW = 206.f;
    float const stageH = 186.f;
    m_stageRect = {stageX, stageY, stageW, stageH};

    if (auto* bg = paimon::ui::makeInset({stageW, stageH}, 225)) {
        bg->setPosition(ccp(stageX, stageY));
        m_mainLayer->addChild(bg, 1);
    }

    auto* stencil = CCDrawNode::create();
    CCPoint rectPts[4] = {{0.f, 0.f}, {stageW - 4.f, 0.f}, {stageW - 4.f, stageH - 4.f}, {0.f, stageH - 4.f}};
    stencil->drawPolygon(rectPts, 4, {1, 1, 1, 1}, 0.f, {1, 1, 1, 1});
    m_stageClip = CCClippingNode::create(stencil);
    m_stageClip->setContentSize({stageW - 4.f, stageH - 4.f});
    m_stageClip->setPosition(ccp(stageX + 2.f, stageY + 2.f));
    m_mainLayer->addChild(m_stageClip, 4);

    m_stageCamNode = CCNode::create();
    m_stageCamNode->setPosition(ccp((stageW - 4.f) * 0.5f, (stageH - 4.f) * 0.5f));
    m_stageClip->addChild(m_stageCamNode, 1);

    m_stageNode = CCNode::create();
    m_stageCamNode->addChild(m_stageNode, 2);

    auto* title = CCLabelBMFont::create("INTERACTIVE STAGE", "goldFont.fnt");
    title->setScale(0.34f);
    title->setPosition(ccp(stageX + stageW * 0.5f, stageY + stageH - 12.f));
    m_mainLayer->addChild(title, 6);

    m_previewFxBadge = CCLabelBMFont::create("", "chatFont.fnt");
    m_previewFxBadge->setScale(0.38f);
    m_previewFxBadge->setOpacity(175);
    m_previewFxBadge->setPosition(ccp(stageX + stageW * 0.5f, stageY + 12.f));
    m_mainLayer->addChild(m_previewFxBadge, 6);

    m_previewHint = CCLabelBMFont::create("Uses vanilla GD\nexplosion", "bigFont.fnt");
    m_previewHint->setScale(0.32f);
    m_previewHint->setAlignment(kCCTextAlignmentCenter);
    m_stageNode->addChild(m_previewHint);

    auto* menu = CCMenu::create();
    menu->setPosition(ccp(0.f, 0.f));
    m_mainLayer->addChild(menu, 10);

    auto* repSpr = ButtonSprite::create("Trigger", 52, true, "bigFont.fnt", "GJ_button_02.png", 18.f, 0.34f);
    auto* repBtn = CCMenuItemSpriteExtra::create(repSpr, this, menu_selector(DeathAnimationPopup::onReplayPreview));
    menu->addChild(repBtn);

    auto* rndSpr = ButtonSprite::create("Random", 52, true, "goldFont.fnt", "GJ_button_04.png", 18.f, 0.34f);
    auto* rndBtn = CCMenuItemSpriteExtra::create(rndSpr, this, menu_selector(DeathAnimationPopup::onRandomizeAll));
    menu->addChild(rndBtn);

    paimon::ui::layoutButtonRow({repBtn, rndBtn}, stageX, stageX + 140.f, 70.f, 6.f, paimon::ui::RowAlign::Left);

    auto* loopTog = CCMenuItemToggler::createWithStandardSprites(
        this, menu_selector(DeathAnimationPopup::onToggleAutoLoop), 0.48f);
    loopTog->setPosition(ccp(stageX + 156.f, 70.f));
    loopTog->toggle(m_autoLoop);
    menu->addChild(loopTog);

    auto* loopLbl = CCLabelBMFont::create("Loop", "bigFont.fnt");
    loopLbl->setScale(0.24f);
    loopLbl->setAnchorPoint(ccp(0.f, 0.5f));
    loopLbl->setPosition(ccp(stageX + 168.f, 70.f));
    m_mainLayer->addChild(loopLbl, 6);

    auto* rstSpr = ButtonSprite::create("Reset FX", 62, true, "bigFont.fnt", "GJ_button_06.png", 18.f, 0.32f);
    auto* rstBtn = CCMenuItemSpriteExtra::create(rstSpr, this, menu_selector(DeathAnimationPopup::onResetStyle));
    menu->addChild(rstBtn);

    auto* sfxSpr = ButtonSprite::create("Death SFX ->", 92, true, "goldFont.fnt", "GJ_button_01.png", 18.f, 0.34f);
    auto* sfxBtn = CCMenuItemSpriteExtra::create(sfxSpr, this, menu_selector(DeathAnimationPopup::onOpenDeathSfx));
    menu->addChild(sfxBtn);

    paimon::ui::layoutButtonRow({rstBtn, sfxBtn}, stageX, stageX + stageW, 40.f, 8.f, paimon::ui::RowAlign::Center);
}

bool DeathAnimationPopup::ccTouchBegan(CCTouch* touch, CCEvent* event) {
    if (m_mainLayer && m_stageNode) {
        CCPoint local = m_mainLayer->convertToNodeSpace(touch->getLocation());
        if (m_stageRect.containsPoint(local)) {
            CCPoint center = ccp(m_stageRect.getMidX(), m_stageRect.getMidY());
            triggerPreviewAt(local - center);
            m_loopTimer = 0.f;
            return true;
        }
    }
    return PaimonPopup::ccTouchBegan(touch, event);
}

void DeathAnimationPopup::update(float dt) {
    if (m_stageCamNode && m_stageShakeTime > 0.f) {
        m_stageShakeTime = std::max(0.f, m_stageShakeTime - dt);
        float frac = m_stageShakeTime / 0.32f;
        float ox = std::sin(m_stageShakeTime * 85.f) * m_stageShakeMag * frac;
        float oy = std::cos(m_stageShakeTime * 67.f) * m_stageShakeMag * frac;
        m_stageCamNode->setPosition(ccp((m_stageRect.size.width - 4.f) * 0.5f + ox,
                                        (m_stageRect.size.height - 4.f) * 0.5f + oy));
    } else if (m_stageCamNode) {
        m_stageCamNode->setPosition(ccp((m_stageRect.size.width - 4.f) * 0.5f,
                                        (m_stageRect.size.height - 4.f) * 0.5f));
    }

    if (!m_autoLoop) return;
    m_loopTimer += dt;
    float const period = std::max(0.75f, 1.15f / currentStyleSettings().speed);
    if (m_loopTimer >= period) {
        m_loopTimer = 0.f;
        triggerPreviewAt(ccp(0.f, 0.f));
    }
}

void DeathAnimationPopup::triggerPreviewAt(CCPoint localInStage) {
    clearAnimations(m_stageNode);
    int style = resolveAnimation(vfxConfig().style);
    if (m_previewHint) m_previewHint->setVisible(style < 0);
    if (style < 0) return;

    auto const& s = currentStyleSettings();
    ccColor3B c1{}, c2{};
    resolveEffectColors(s, {85, 225, 255}, {255, 110, 210}, c1, c2);

    if (!spawnAnimationDual(m_stageNode, localInStage, style, {85, 225, 255}, {255, 110, 210}, 0.68f)) {
        if (m_statusLabel) m_statusLabel->setString("Shader unavailable");
    }

    // Preview simulation of Camera Shake, Zoom, Tilt, and Post-FX Overlay
    if (s.cameraShake > 0.02f) {
        m_stageShakeTime = 0.30f;
        m_stageShakeMag = 6.f * s.cameraShake;
    }
    if (m_stageCamNode) {
        m_stageCamNode->stopAllActions();
        float z = 1.f;
        if (s.cameraZoom == DeathCameraZoom::PunchIn) z = 1.14f;
        else if (s.cameraZoom == DeathCameraZoom::RecoilOut) z = 0.86f;
        else if (s.cameraZoom == DeathCameraZoom::DramaticIn) z = 1.24f;
        float tilt = (s.cameraTilt == DeathCameraTilt::HardKick) ? 6.f :
                     (s.cameraTilt == DeathCameraTilt::Subtle ? 2.5f : 0.f);
        m_stageCamNode->setScale(z);
        m_stageCamNode->setRotation(tilt);
        m_stageCamNode->runAction(CCSpawn::create(
            CCEaseOut::create(CCScaleTo::create(0.35f, 1.f), 2.f),
            CCEaseOut::create(CCRotateTo::create(0.35f, 0.f), 2.f),
            nullptr
        ));
    }
    if (m_stageClip && s.screenFx != DeathScreenFx::None) {
        spawnScreenOverlayFx(m_stageClip, m_stageClip->getContentSize(),
            s.screenFx, c1, s.screenFxStrength * 0.7f, std::min(0.45f, s.screenFxDuration));
    }
}

void DeathAnimationPopup::refreshUI() {
    auto const& cfg = vfxConfig();
    auto const& s = currentStyleSettings();

    for (auto* btn : m_catBtns) {
        paimon::ui::setButtonSkin(btn,
            btn->getTag() == m_categoryFilter ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray);
    }
    paimon::ui::matchButtonLabels(m_catBtns);
    for (auto* btn : m_styleButtons) {
        if (!btn) continue;
        paimon::ui::setButtonSkin(btn,
            btn->getTag() == cfg.style ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray);
    }

    auto setText = [](CCMenuItemSpriteExtra* b, char const* t) {
        if (!b) return;
        if (auto* spr = typeinfo_cast<ButtonSprite*>(b->getNormalImage())) spr->setString(t);
    };

    setText(m_colorModeBtn, colorModeLabel(s.colorMode));
    if (m_primarySwatch) m_primarySwatch->setColor(s.customPrimary);
    if (m_secondarySwatch) m_secondarySwatch->setColor(s.customSecondary);

    auto syncSl = [](Slider* sl, CCLabelBMFont* lbl, float val, float lo, float hi, std::string const& txt) {
        if (sl) { sl->setValue(std::clamp((val - lo) / (hi - lo), 0.f, 1.f)); sl->updateBar(); }
        if (lbl) lbl->setString(txt.c_str());
    };
    syncSl(m_scaleSlider,     m_scaleLbl,     s.scale,     0.35f, 2.5f, fmt::format("{:.2f}x", s.scale));
    syncSl(m_speedSlider,     m_speedLbl,     s.speed,     0.40f, 2.2f, fmt::format("{:.2f}x", s.speed));
    syncSl(m_intensitySlider, m_intensityLbl, s.intensity, 0.30f, 1.8f, fmt::format("{:.2f}", s.intensity));
    syncSl(m_spinSlider,      m_spinLbl,      s.spin,     -3.0f,  3.0f, fmt::format("{:.1f}", s.spin));
    if (m_keepVanillaToggle) m_keepVanillaToggle->toggle(s.keepVanillaExplosion);
    if (m_mediaPathLbl) m_mediaPathLbl->setString(shortFileLabel(cfg.customMediaPath).c_str());

    setText(m_partShapeBtn, partShapeLabel(s.particleShape));
    setText(m_partPhysBtn,  partPhysicsLabel(s.particlePhysics));
    syncSl(m_partCountSlider, m_partCountLbl, static_cast<float>(s.particleCount), 0.f, 48.f, fmt::format("{}", s.particleCount));
    if (m_shockRingToggle) m_shockRingToggle->toggle(s.enableShockRing);
    if (m_ghostIconToggle) m_ghostIconToggle->toggle(s.enableGhostIcon);

    syncSl(m_camShakeSlider,   m_camShakeLbl,   s.cameraShake,      0.f, 1.f, fmt::format("{}%", std::lround(s.cameraShake * 100.f)));
    setText(m_camZoomBtn, camZoomLabel(s.cameraZoom));
    setText(m_camTiltBtn, camTiltLabel(s.cameraTilt));
    setText(m_screenFxBtn, screenFxLabel(s.screenFx));
    syncSl(m_fxStrengthSlider, m_fxStrengthLbl, s.screenFxStrength, 0.1f, 1.5f, fmt::format("{:.2f}", s.screenFxStrength));
    syncSl(m_fxDurSlider,      m_fxDurLbl,      s.screenFxDuration, 0.15f, 1.2f, fmt::format("{:.2f}s", s.screenFxDuration));

    if (m_previewFxBadge) {
        m_previewFxBadge->setString(fmt::format("Cam:{}% | {}", std::lround(s.cameraShake * 100.f), screenFxLabel(s.screenFx)).c_str());
    }

    if (m_statusLabel) {
        std::string name;
        if (cfg.style < 0) name = "Original";
        else if (cfg.style == kStyleRandom) name = "Random FX";
        else if (cfg.style == kStyleCustomMedia) name = "Custom Media";
        else name = kAnimationNames[cfg.style];
        m_statusLabel->setString(fmt::format(
            "VFX: {}  |  Particles: {} ({})  |  Screen: {}",
            name, partShapeLabel(s.particleShape), s.particleCount, screenFxLabel(s.screenFx)).c_str());
        m_statusLabel->limitLabelWidth(486.f, 0.28f, 0.16f);
    }
}

void DeathAnimationPopup::onSubTabSwitch(CCObject* sender) {
    auto* btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    m_subTab = btn->getTag();
    for (int i = 0; i < 4; ++i) {
        if (m_subPanes[i]) m_subPanes[i]->setVisible(i == m_subTab);
        paimon::ui::setButtonSkin(m_subTabBtns[i],
            i == m_subTab ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray);
    }
    paimon::ui::matchButtonLabels(
        std::vector<CCMenuItemSpriteExtra*>(m_subTabBtns.begin(), m_subTabBtns.end()));
}

void DeathAnimationPopup::onSelectCategory(CCObject* sender) {
    auto* btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    m_categoryFilter = btn->getTag();
    rebuildStyleGrid();
    refreshUI();
}

void DeathAnimationPopup::onSelectStyle(CCObject* sender) {
    auto* node = typeinfo_cast<CCNode*>(sender);
    if (!node) return;
    vfxConfig().style = node->getTag();
    saveVfxConfig();
    refreshUI();
    m_loopTimer = 0.f;
    triggerPreviewAt(ccp(0.f, 0.f));
}

void DeathAnimationPopup::onReplayPreview(CCObject*) {
    m_loopTimer = 0.f;
    triggerPreviewAt(ccp(0.f, 0.f));
}

void DeathAnimationPopup::onToggleAutoLoop(CCObject* sender) {
    auto* tog = typeinfo_cast<CCMenuItemToggler*>(sender);
    m_autoLoop = !(tog && tog->isToggled());
}

void DeathAnimationPopup::onRandomizeAll(CCObject*) {
    static std::mt19937 rng(std::random_device{}());
    vfxConfig().style = std::uniform_int_distribution<int>(0, kDeathStyleCount - 1)(rng);
    saveVfxConfig();
    refreshUI();
    m_loopTimer = 0.f;
    triggerPreviewAt(ccp(0.f, 0.f));
}

void DeathAnimationPopup::onResetStyle(CCObject*) {
    resetCurrentStyleToDefault();
    refreshUI();
    triggerPreviewAt(ccp(0.f, 0.f));
    PaimonNotify::show("Style reset to default recipe", NotificationIcon::Info);
}

void DeathAnimationPopup::onOpenDeathSfx(CCObject*) {
    this->onClose(nullptr);
    if (auto* sfx = DeathEffectPopup::create()) {
        sfx->show();
    }
}

void DeathAnimationPopup::onCycleColorMode(CCObject*) {
    auto& s = currentStyleSettings();
    s.colorMode = static_cast<DeathColorMode>((static_cast<int>(s.colorMode) + 1) % 5);
    saveVfxConfig();
    refreshUI();
    triggerPreviewAt(ccp(0.f, 0.f));
}

void DeathAnimationPopup::onPickPrimary(CCObject*) {
    WeakRef<DeathAnimationPopup> self = this;
    openColorPicker(currentStyleSettings().customPrimary, [self](ccColor3B col) {
        auto& s = currentStyleSettings();
        s.customPrimary = col;
        s.colorMode = DeathColorMode::Custom;
        saveVfxConfig();
        if (auto p = self.lock()) { p->refreshUI(); p->triggerPreviewAt(ccp(0.f, 0.f)); }
    });
}

void DeathAnimationPopup::onPickSecondary(CCObject*) {
    WeakRef<DeathAnimationPopup> self = this;
    openColorPicker(currentStyleSettings().customSecondary, [self](ccColor3B col) {
        auto& s = currentStyleSettings();
        s.customSecondary = col;
        s.colorMode = DeathColorMode::Custom;
        saveVfxConfig();
        if (auto p = self.lock()) { p->refreshUI(); p->triggerPreviewAt(ccp(0.f, 0.f)); }
    });
}

void DeathAnimationPopup::onScaleChanged(CCObject*) {
    if (!m_scaleSlider || !m_scaleSlider->getThumb()) return;
    currentStyleSettings().scale = 0.35f + m_scaleSlider->getThumb()->getValue() * (2.5f - 0.35f);
    if (m_scaleLbl) m_scaleLbl->setString(fmt::format("{:.2f}x", currentStyleSettings().scale).c_str());
}

void DeathAnimationPopup::onSpeedChanged(CCObject*) {
    if (!m_speedSlider || !m_speedSlider->getThumb()) return;
    currentStyleSettings().speed = 0.40f + m_speedSlider->getThumb()->getValue() * (2.2f - 0.40f);
    if (m_speedLbl) m_speedLbl->setString(fmt::format("{:.2f}x", currentStyleSettings().speed).c_str());
}

void DeathAnimationPopup::onIntensityChanged(CCObject*) {
    if (!m_intensitySlider || !m_intensitySlider->getThumb()) return;
    currentStyleSettings().intensity = 0.30f + m_intensitySlider->getThumb()->getValue() * (1.8f - 0.30f);
    if (m_intensityLbl) m_intensityLbl->setString(fmt::format("{:.2f}", currentStyleSettings().intensity).c_str());
}

void DeathAnimationPopup::onSpinChanged(CCObject*) {
    if (!m_spinSlider || !m_spinSlider->getThumb()) return;
    currentStyleSettings().spin = -3.0f + m_spinSlider->getThumb()->getValue() * 6.0f;
    if (m_spinLbl) m_spinLbl->setString(fmt::format("{:.1f}", currentStyleSettings().spin).c_str());
}

void DeathAnimationPopup::onToggleKeepVanilla(CCObject* sender) {
    auto* tog = typeinfo_cast<CCMenuItemToggler*>(sender);
    currentStyleSettings().keepVanillaExplosion = !(tog && tog->isToggled());
    saveVfxConfig();
}

void DeathAnimationPopup::onPickCustomMedia(CCObject*) {
    WeakRef<DeathAnimationPopup> self = this;
    pt::pickImage([self](geode::Result<std::optional<std::filesystem::path>> res) {
        auto opt = std::move(res).unwrapOr(std::nullopt);
        if (!opt || opt->empty()) return;
        auto imported = paimon::assets::importToBucket(*opt, "death_vfx", paimon::assets::Kind::Image);
        if (!imported.success || imported.path.empty()) {
            PaimonNotify::show("Failed to import PNG/GIF", NotificationIcon::Error);
            return;
        }
        auto& c = vfxConfig();
        c.customMediaPath = paimon::assets::normalizePathString(imported.path);
        c.style = kStyleCustomMedia;
        saveVfxConfig();
        if (auto p = self.lock()) { p->refreshUI(); p->triggerPreviewAt(ccp(0.f, 0.f)); }
        PaimonNotify::show("Custom Death Media equipped!", NotificationIcon::Success);
    });
}

void DeathAnimationPopup::onClearCustomMedia(CCObject*) {
    vfxConfig().customMediaPath.clear();
    if (vfxConfig().style == kStyleCustomMedia) vfxConfig().style = 0;
    saveVfxConfig();
    refreshUI();
    triggerPreviewAt(ccp(0.f, 0.f));
}

void DeathAnimationPopup::onCyclePartShape(CCObject*) {
    auto& s = currentStyleSettings();
    s.particleShape = static_cast<DeathParticleShape>((static_cast<int>(s.particleShape) + 1) % 8);
    saveVfxConfig();
    refreshUI();
    triggerPreviewAt(ccp(0.f, 0.f));
}

void DeathAnimationPopup::onCyclePartPhysics(CCObject*) {
    auto& s = currentStyleSettings();
    s.particlePhysics = static_cast<DeathParticlePhysics>((static_cast<int>(s.particlePhysics) + 1) % 4);
    saveVfxConfig();
    refreshUI();
    triggerPreviewAt(ccp(0.f, 0.f));
}

void DeathAnimationPopup::onPartCountChanged(CCObject*) {
    if (!m_partCountSlider || !m_partCountSlider->getThumb()) return;
    currentStyleSettings().particleCount = static_cast<int>(std::lround(m_partCountSlider->getThumb()->getValue() * 48.f));
    if (m_partCountLbl) m_partCountLbl->setString(fmt::format("{}", currentStyleSettings().particleCount).c_str());
}

void DeathAnimationPopup::onToggleShockRing(CCObject* sender) {
    auto* tog = typeinfo_cast<CCMenuItemToggler*>(sender);
    currentStyleSettings().enableShockRing = !(tog && tog->isToggled());
    saveVfxConfig();
    triggerPreviewAt(ccp(0.f, 0.f));
}

void DeathAnimationPopup::onToggleGhostIcon(CCObject* sender) {
    auto* tog = typeinfo_cast<CCMenuItemToggler*>(sender);
    currentStyleSettings().enableGhostIcon = !(tog && tog->isToggled());
    saveVfxConfig();
    triggerPreviewAt(ccp(0.f, 0.f));
}

void DeathAnimationPopup::onCamShakeChanged(CCObject*) {
    if (!m_camShakeSlider || !m_camShakeSlider->getThumb()) return;
    currentStyleSettings().cameraShake = m_camShakeSlider->getThumb()->getValue();
    if (m_camShakeLbl) m_camShakeLbl->setString(fmt::format("{}%", std::lround(currentStyleSettings().cameraShake * 100.f)).c_str());
}

void DeathAnimationPopup::onCycleCamZoom(CCObject*) {
    auto& s = currentStyleSettings();
    s.cameraZoom = static_cast<DeathCameraZoom>((static_cast<int>(s.cameraZoom) + 1) % 4);
    saveVfxConfig();
    refreshUI();
    triggerPreviewAt(ccp(0.f, 0.f));
}

void DeathAnimationPopup::onCycleCamTilt(CCObject*) {
    auto& s = currentStyleSettings();
    s.cameraTilt = static_cast<DeathCameraTilt>((static_cast<int>(s.cameraTilt) + 1) % 3);
    saveVfxConfig();
    refreshUI();
    triggerPreviewAt(ccp(0.f, 0.f));
}

void DeathAnimationPopup::onCycleScreenFx(CCObject*) {
    auto& s = currentStyleSettings();
    s.screenFx = static_cast<DeathScreenFx>((static_cast<int>(s.screenFx) + 1) % 12);
    saveVfxConfig();
    refreshUI();
    triggerPreviewAt(ccp(0.f, 0.f));
}

void DeathAnimationPopup::onScreenFxStrengthChanged(CCObject*) {
    if (!m_fxStrengthSlider || !m_fxStrengthSlider->getThumb()) return;
    currentStyleSettings().screenFxStrength = 0.1f + m_fxStrengthSlider->getThumb()->getValue() * 1.4f;
    if (m_fxStrengthLbl) m_fxStrengthLbl->setString(fmt::format("{:.2f}", currentStyleSettings().screenFxStrength).c_str());
}

void DeathAnimationPopup::onScreenFxDurationChanged(CCObject*) {
    if (!m_fxDurSlider || !m_fxDurSlider->getThumb()) return;
    currentStyleSettings().screenFxDuration = 0.15f + m_fxDurSlider->getThumb()->getValue() * 1.05f;
    if (m_fxDurLbl) m_fxDurLbl->setString(fmt::format("{:.2f}s", currentStyleSettings().screenFxDuration).c_str());
}

} // namespace paimon::death_effects
