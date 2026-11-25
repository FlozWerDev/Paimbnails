#include "PaimonAssistantPopup.hpp"

#include "../../../blur/PopupBlurService.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/GLSLLoader.hpp"
#include "../../../utils/GeodeTextInputSafe.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/cocos/shaders/ccGLStateCache.h>
#include <Geode/ui/OverlayManager.hpp>

#include <algorithm>
#include <cmath>
#include <string>

#ifndef kQuadSize
#define kQuadSize sizeof(cocos2d::ccV3F_C4B_T2F)
#endif

using namespace geode::prelude;

namespace paimon::assistant {

namespace {

constexpr char const* kEffortKey = "paimon-assistant-effort";
constexpr int kOverlayZ = 999400;
// la capa traga toques de fondo; el menu va antes para que los botones/input vivan.
constexpr int kTouchPriority = -200;
constexpr int kMenuPriority = -300;

constexpr float kEffortBtnScale = 0.52f;
constexpr float kEffortBtnW = 78.f;
constexpr float kEffortGap = 8.f;
constexpr float kEffortLabelGap = 10.f;

constexpr char const* kVertexShaderFallback = R"(
attribute vec4 a_position;
attribute vec4 a_color;
attribute vec2 a_texCoord;

#ifdef GL_ES
varying lowp vec4 v_fragmentColor;
varying mediump vec2 v_texCoord;
#else
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
#endif

void main() {
    gl_Position = CC_MVPMatrix * a_position;
    v_fragmentColor = a_color;
    v_texCoord = a_texCoord;
}
)";

constexpr char const* kBackdropFragFallback = R"(
#ifdef GL_ES
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
varying lowp vec4 v_fragmentColor;
varying mediump vec2 v_texCoord;
#else
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
#endif

uniform vec2 u_resolution;
uniform float u_time;
uniform vec3 u_effortWeights;
uniform float u_working;
uniform float u_fade;

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

vec4 animNormal(vec2 uv, vec2 p, float aspect, float t, float work) {
    float speed = t * mix(0.28, 0.60, work);
    vec3 col = vec3(0.03, 0.06, 0.11);

    float acc = 0.0;
    vec3 orbTint = vec3(0.0);
    for (int i = 0; i < 5; ++i) {
        float fi = float(i);
        vec2 center = vec2(
            sin(speed * (0.42 + fi * 0.11) + fi * 1.7) * (aspect * 0.36),
            cos(speed * (0.37 + fi * 0.09) + fi * 2.3) * 0.34
        );
        float radius = 0.22 + 0.05 * sin(speed * 0.6 + fi * 1.3);
        float d = length(p - center);
        float orb = exp(-(d * d) / (radius * radius));
        vec3 c = mix(vec3(0.32, 0.74, 0.96), vec3(0.65, 0.88, 0.98), 0.5 + 0.5 * sin(fi * 1.9 + speed * 0.4));
        orbTint += c * orb;
        acc += orb;
    }

    float breath = 0.90 + 0.10 * sin(speed * 0.9);
    col += orbTint * (0.10 + 0.06 * work) * breath;
    float alpha = clamp(0.08 + acc * 0.055 + work * 0.04, 0.0, 0.24);
    return vec4(col, alpha);
}

vec4 animHigh(vec2 uv, vec2 p, float aspect, float t, float work) {
    float speed = t * mix(0.55, 1.05, work);
    vec3 base = mix(vec3(0.04, 0.04, 0.12), vec3(0.03, 0.08, 0.15), uv.y);

    vec3 aurora = vec3(0.0);
    float totalBand = 0.0;
    for (int i = 0; i < 3; ++i) {
        float fi = float(i);
        float freq = 2.2 + fi * 0.9;
        float wave = sin(p.x * freq + speed * (0.75 + fi * 0.18) + fi * 2.1) * 0.13
                   + cos(p.x * (freq * 0.55) - speed * 0.55 + fi) * 0.07;
        float yTarget = (fi - 1.0) * 0.19 + wave;
        float dist = abs(p.y - yTarget);
        float ribbon = exp(-dist * (7.2 - fi * 1.0));
        float shimmer = 0.80 + 0.20 * sin(p.x * 4.2 - speed * 1.2 + fi * 1.4);

        vec3 rCol = (i == 0) ? vec3(0.28, 0.78, 0.96)
                  : (i == 1) ? vec3(0.56, 0.42, 0.96)
                             : vec3(0.84, 0.44, 0.82);
        aurora += rCol * ribbon * shimmer;
        totalBand += ribbon;
    }

    vec3 col = base + aurora * (0.16 + 0.09 * work);
    float alpha = clamp(0.10 + totalBand * 0.075 + work * 0.05, 0.0, 0.28);
    return vec4(col, alpha);
}

vec4 animUltra(vec2 uv, vec2 p, float aspect, float t, float work) {
    float speed = t * mix(0.75, 1.30, work);
    float r = length(p);
    float ang = atan(p.y, p.x);

    float swirl = sin(ang * 3.0 - r * 6.5 + speed * 1.1) * 0.5 + 0.5;
    float rays  = pow(0.5 + 0.5 * cos(ang * 6.0 + speed * 0.7 + sin(r * 4.5 - speed)), 2.4);
    float ring  = exp(-abs(r - (0.37 + 0.03 * sin(speed * 1.1))) * 7.0);
    float outerEnergy = smoothstep(0.10, 0.58, r);

    vec3 coreCol  = vec3(0.98, 0.76, 0.30);
    vec3 flareCol = vec3(0.95, 0.42, 0.34);
    vec3 prismCol = vec3(0.86, 0.48, 0.84);

    vec3 plasma = mix(flareCol, coreCol, swirl) * (0.14 * rays + 0.14 * ring) * (0.45 + 0.55 * outerEnergy);
    plasma += prismCol * swirl * exp(-r * 2.2) * 0.10;

    vec2 grid = uv * vec2(aspect * 16.0, 16.0);
    vec2 cell = floor(grid);
    vec2 f = fract(grid) - 0.5;
    float rnd = hash21(cell);
    float starMask = step(0.87, rnd);
    float twinkle = 0.50 + 0.50 * sin(speed * (1.6 + rnd * 2.2) + rnd * 25.0);
    float star = starMask * exp(-dot(f, f) * 40.0) * twinkle;

    vec3 base = vec3(0.06, 0.03, 0.05);
    vec3 col = base + plasma * (0.85 + 0.35 * work) + vec3(1.0, 0.92, 0.74) * star * 0.38;
    float alpha = clamp(0.11 + (rays * 0.07 + ring * 0.08 + star * 0.14) + work * 0.05, 0.0, 0.32);
    return vec4(col, alpha);
}

void main() {
    float aspect = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 p = (v_texCoord - 0.5) * vec2(aspect, 1.0);

    vec4 accum = vec4(0.0);
    if (u_effortWeights.x > 0.001) {
        accum += animNormal(v_texCoord, p, aspect, u_time, u_working) * u_effortWeights.x;
    }
    if (u_effortWeights.y > 0.001) {
        accum += animHigh(v_texCoord, p, aspect, u_time, u_working) * u_effortWeights.y;
    }
    if (u_effortWeights.z > 0.001) {
        accum += animUltra(v_texCoord, p, aspect, u_time, u_working) * u_effortWeights.z;
    }

    float centerSoft = smoothstep(0.06, 0.54, length(p * vec2(0.75, 1.05)));
    float readBoost = mix(0.65, 1.0, centerSoft);

    gl_FragColor = vec4(accum.rgb * readBoost, accum.a * u_fade * v_fragmentColor.a);
}
)";

constexpr char const* kCornerGlowFragFallback = R"(
#ifdef GL_ES
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
varying lowp vec4 v_fragmentColor;
varying mediump vec2 v_texCoord;
#else
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
#endif

uniform vec2 u_resolution;
uniform float u_time;
uniform vec3 u_effortWeights;
uniform float u_working;
uniform float u_fade;

void main() {
    float aspect = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 halfExt = vec2(aspect * 0.5, 0.5);
    vec2 p = (v_texCoord - 0.5) * vec2(aspect, 1.0);

    float cornerR = 0.095;
    vec2 q = abs(p) - (halfExt - vec2(cornerR));
    float sd = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - cornerR;

    float dIn  = max(-sd, 0.0);
    float dOut = max(sd, 0.0);

    vec2 cornerDelta = halfExt - abs(p);
    float distCorner = length(cornerDelta);

    float pulseSpeed = mix(1.0, 2.4, u_working);
    float breath = 0.90 + 0.10 * sin(u_time * pulseSpeed);

    float rim      = exp(-dIn * mix(135.0, 105.0, u_working)) * exp(-dOut * 26.0);
    float edgeHalo = exp(-dIn * mix(28.0, 21.0, u_working)) * exp(-dOut * 8.0);
    float wideEdge = exp(-dIn * mix(11.0, 8.5, u_working));

    float cornerCore  = exp(-distCorner * mix(15.0, 11.5, u_working));
    float cornerBloom = exp(-distCorner * mix(6.2, 4.8, u_working));

    float ang = atan(p.y, p.x);
    float sweepSpeed = mix(0.45, 1.35, u_working);
    float waveA = 0.5 + 0.5 * sin(ang * 2.0 - u_time * sweepSpeed);
    float waveB = 0.5 + 0.5 * cos(ang * 3.0 + u_time * (sweepSpeed * 0.8));

    vec3 cNormA = vec3(0.62, 0.88, 1.00);
    vec3 cNormB = vec3(0.92, 0.96, 1.00);

    vec3 cHighA = vec3(0.45, 0.84, 0.98);
    vec3 cHighB = vec3(0.72, 0.54, 0.98);

    vec3 cUltraA = vec3(1.00, 0.80, 0.38);
    vec3 cUltraB = vec3(0.98, 0.52, 0.58);

    vec3 colA = cNormA * u_effortWeights.x + cHighA * u_effortWeights.y + cUltraA * u_effortWeights.z;
    vec3 colB = cNormB * u_effortWeights.x + cHighB * u_effortWeights.y + cUltraB * u_effortWeights.z;

    vec3 glowCol = mix(colA, colB, waveA * 0.65 + waveB * 0.35);
    vec3 rimCol = mix(glowCol, vec3(1.0), 0.58);

    float intensity =
        rim * (0.42 + 0.20 * u_working)
        + edgeHalo * (0.16 + 0.10 * u_working)
        + wideEdge * (0.04 + 0.04 * u_working)
        + cornerCore * (0.32 + 0.16 * u_working)
        + cornerBloom * (0.18 + 0.10 * u_working);

    intensity *= breath * (0.88 + 0.14 * waveA);

    vec3 finalCol = mix(glowCol, rimCol, clamp(rim + cornerCore * 0.30, 0.0, 1.0));
    float alpha = clamp(intensity, 0.0, 0.85) * u_fade * v_fragmentColor.a;

    gl_FragColor = vec4(finalCol, alpha);
}
)";

cocos2d::ccColor3B borderTintForWeights(std::array<float, 3> const& w) {
    // Blend Normal (cyan), High (violet), Ultra (warm gold).
    float const r = 110.f * w[0] + 190.f * w[1] + 255.f * w[2];
    float const g = 215.f * w[0] + 135.f * w[1] + 198.f * w[2];
    float const b = 255.f * w[0] + 255.f * w[1] +  85.f * w[2];
    return {
        static_cast<GLubyte>(std::clamp(r, 0.f, 255.f)),
        static_cast<GLubyte>(std::clamp(g, 0.f, 255.f)),
        static_cast<GLubyte>(std::clamp(b, 0.f, 255.f)),
    };
}

paimon::ui::Btn skinForEffort(int index) {
    switch (index) {
        case 0: return paimon::ui::Btn::Cyan;
        case 1: return paimon::ui::Btn::Pink;
        case 2: return paimon::ui::Btn::Red;
        default: return paimon::ui::Btn::Gray;
    }
}

void fadeNodeTree(cocos2d::CCNode* node, float duration) {
    if (!node) return;
    if (dynamic_cast<cocos2d::CCRGBAProtocol*>(node)) {
        node->runAction(cocos2d::CCFadeTo::create(duration, 0));
    }
    if (auto* children = node->getChildren()) {
        for (auto* child : CCArrayExt<cocos2d::CCNode*>(children)) {
            fadeNodeTree(child, duration);
        }
    }
}

} // namespace

// Fullscreen textured quad driven by a custom CCGLProgram with cached uniforms.
class AssistantShaderQuad : public cocos2d::CCSprite {
public:
    float m_time = 0.f;
    float m_working = 0.f;
    float m_fade = 0.f;
    std::array<float, 3> m_effortWeights{1.f, 0.f, 0.f};

    static AssistantShaderQuad* create(
        cocos2d::CCSize const& size,
        cocos2d::CCGLProgram* program,
        bool additive
    ) {
        auto* quad = new AssistantShaderQuad();
        static constexpr unsigned char kWhite[16] = {
            255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255
        };
        auto* tex = new cocos2d::CCTexture2D();
        if (tex && tex->initWithData(
                kWhite, cocos2d::kCCTexture2DPixelFormat_RGBA8888, 2, 2, {2.f, 2.f})) {
            cocos2d::ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
            tex->setTexParameters(&params);
            if (quad->initWithTexture(tex)) {
                tex->release();
                quad->autorelease();
                quad->setTextureRect({0.f, 0.f, 2.f, 2.f});
                quad->setAnchorPoint({0.f, 0.f});
                quad->setPosition({0.f, 0.f});
                quad->setContentSize(size);
                quad->setBlendFunc(additive
                    ? cocos2d::ccBlendFunc{GL_SRC_ALPHA, GL_ONE}
                    : cocos2d::ccBlendFunc{GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
                if (program) quad->setShaderProgram(program);
                return quad;
            }
        }
        CC_SAFE_DELETE(tex);
        CC_SAFE_DELETE(quad);
        return nullptr;
    }

    void setContentSize(cocos2d::CCSize const& size) override {
        CCSprite::setContentSize(size);
        m_sQuad.bl.vertices = {0.f, 0.f, 0.f};
        m_sQuad.br.vertices = {size.width, 0.f, 0.f};
        m_sQuad.tl.vertices = {0.f, size.height, 0.f};
        m_sQuad.tr.vertices = {size.width, size.height, 0.f};

        m_sQuad.bl.texCoords = {0.f, 0.f};
        m_sQuad.br.texCoords = {1.f, 0.f};
        m_sQuad.tl.texCoords = {0.f, 1.f};
        m_sQuad.tr.texCoords = {1.f, 1.f};

        cocos2d::ccColor4B const white{255, 255, 255, getOpacity()};
        m_sQuad.bl.colors = white;
        m_sQuad.br.colors = white;
        m_sQuad.tl.colors = white;
        m_sQuad.tr.colors = white;
    }

    void setOpacity(GLubyte opacity) override {
        CCSprite::setOpacity(opacity);
        cocos2d::ccColor4B const c{255, 255, 255, opacity};
        m_sQuad.bl.colors = c;
        m_sQuad.br.colors = c;
        m_sQuad.tl.colors = c;
        m_sQuad.tr.colors = c;
    }

    void draw() override {
        auto* prog = getShaderProgram();
        if (!prog || m_fade <= 0.001f) return;

        CC_NODE_DRAW_SETUP();

        if (prog != m_cachedProgram) {
            m_cachedProgram = prog;
            m_locResolution = prog->getUniformLocationForName("u_resolution");
            m_locTime = prog->getUniformLocationForName("u_time");
            m_locWeights = prog->getUniformLocationForName("u_effortWeights");
            m_locWorking = prog->getUniformLocationForName("u_working");
            m_locFade = prog->getUniformLocationForName("u_fade");
        }

        auto const sz = getContentSize();
        if (m_locResolution != -1) {
            prog->setUniformLocationWith2f(m_locResolution, std::max(sz.width, 1.f), std::max(sz.height, 1.f));
        }
        if (m_locTime != -1) {
            prog->setUniformLocationWith1f(m_locTime, m_time);
        }
        if (m_locWeights != -1) {
            prog->setUniformLocationWith3f(
                m_locWeights, m_effortWeights[0], m_effortWeights[1], m_effortWeights[2]);
        }
        if (m_locWorking != -1) {
            prog->setUniformLocationWith1f(m_locWorking, m_working);
        }
        if (m_locFade != -1) {
            prog->setUniformLocationWith1f(m_locFade, m_fade);
        }

        cocos2d::ccGLBlendFunc(m_sBlendFunc.src, m_sBlendFunc.dst);
        cocos2d::ccGLBindTexture2D(getTexture() ? getTexture()->getName() : 0);

#if CC_TEXTURE_ATLAS_USE_VAO
        cocos2d::ccGLBindVAO(0);
#endif
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        cocos2d::ccGLEnableVertexAttribs(cocos2d::kCCVertexAttribFlag_PosColorTex);

        uintptr_t const offset = reinterpret_cast<uintptr_t>(&m_sQuad);
        glVertexAttribPointer(
            cocos2d::kCCVertexAttrib_Position, 3, GL_FLOAT, GL_FALSE, kQuadSize,
            reinterpret_cast<void*>(offset + offsetof(cocos2d::ccV3F_C4B_T2F, vertices)));
        glVertexAttribPointer(
            cocos2d::kCCVertexAttrib_TexCoords, 2, GL_FLOAT, GL_FALSE, kQuadSize,
            reinterpret_cast<void*>(offset + offsetof(cocos2d::ccV3F_C4B_T2F, texCoords)));
        glVertexAttribPointer(
            cocos2d::kCCVertexAttrib_Color, 4, GL_UNSIGNED_BYTE, GL_TRUE, kQuadSize,
            reinterpret_cast<void*>(offset + offsetof(cocos2d::ccV3F_C4B_T2F, colors)));

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        CHECK_GL_ERROR_DEBUG();
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_ANDROID)
        CC_INCREMENT_GL_DRAWS(1);
#endif
    }

private:
    cocos2d::CCGLProgram* m_cachedProgram = nullptr;
    GLint m_locResolution = -1;
    GLint m_locTime = -1;
    GLint m_locWeights = -1;
    GLint m_locWorking = -1;
    GLint m_locFade = -1;
};

PaimonAssistantPopup* PaimonAssistantPopup::s_instance = nullptr;

void PaimonAssistantPopup::toggle() {
    if (s_instance) {
        s_instance->close();
        return;
    }
    if (auto* ov = PaimonAssistantPopup::create()) {
        ov->show();
    }
}

PaimonAssistantPopup* PaimonAssistantPopup::create() {
    auto* ret = new PaimonAssistantPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

void PaimonAssistantPopup::show() {
    this->setID("paimon-assistant-overlay"_spr);
    if (auto* host = geode::OverlayManager::get()) {
        host->addChild(this, kOverlayZ);
    } else if (auto* scene = CCDirector::get()->getRunningScene()) {
        scene->addChild(this, 99999);
    }
    runShowAnimation();
}

void PaimonAssistantPopup::close() {
    doClose();
}

PaimonAssistantPopup::Effort PaimonAssistantPopup::loadEffort() {
    auto const saved = Mod::get()->getSavedValue<std::string>(kEffortKey, "normal");
    if (saved == "high") return Effort::High;
    if (saved == "ultra") return Effort::Ultra;
    return Effort::Normal;
}

void PaimonAssistantPopup::saveEffort(Effort effort) {
    char const* name = "normal";
    if (effort == Effort::High) name = "high";
    if (effort == Effort::Ultra) name = "ultra";
    Mod::get()->setSavedValue<std::string>(kEffortKey, name);
}

char const* PaimonAssistantPopup::effortName() const {
    switch (m_effort) {
        case Effort::High: return "High";
        case Effort::Ultra: return "Ultra";
        case Effort::Normal:
        default: return "Normal";
    }
}

bool PaimonAssistantPopup::init() {
    if (!CCLayer::init()) return false;
    s_instance = this;
    m_effort = loadEffort();
    m_effortWeights = {
        m_effort == Effort::Normal ? 1.f : 0.f,
        m_effort == Effort::High ? 1.f : 0.f,
        m_effort == Effort::Ultra ? 1.f : 0.f,
    };

    this->setTouchEnabled(true);
    this->setKeypadEnabled(true);

    buildDim();
    buildShaderLayers();
    buildContent();
    syncShaderUniforms();
    this->scheduleUpdate();
    return true;
}

void PaimonAssistantPopup::onEnter() {
    CCLayer::onEnter();
    // show() corre dentro del evento de teclado; diferir el cambio de
    // prioridad hasta que el dispatcher confirme el nuevo handler.
    if (m_priorityScheduled) return;
    m_priorityScheduled = true;
    geode::WeakRef<PaimonAssistantPopup> weak = this;
    Loader::get()->queueInMainThread([weak]() {
        auto self = weak.lock();
        if (!self || self->m_closing) return;
        if (self->m_menu) self->m_menu->setHandlerPriority(kMenuPriority);
    });
}

void PaimonAssistantPopup::onExit() {
    this->unscheduleUpdate();
    this->stopAllActions();
    if (m_input) paimon::ui::detachGeodeTextInput(m_input);
    paimon::popupblur::cleanup(this);
    if (s_instance == this) s_instance = nullptr;
    CCLayer::onExit();
}

void PaimonAssistantPopup::update(float dt) {
    float const clampedDt = std::clamp(dt, 0.f, 0.05f);
    m_shaderTime += clampedDt;

    float const targetFade = m_closing ? 0.f : 1.f;
    float const fadeRate = 1.f - std::exp(-clampedDt * (m_closing ? 14.f : 9.f));
    m_fade += (targetFade - m_fade) * fadeRate;

    float const targetWork = m_working ? 1.f : 0.f;
    float const workRate = 1.f - std::exp(-clampedDt * 5.5f);
    m_workingMix += (targetWork - m_workingMix) * workRate;

    // Crossfade smooth entre las 3 animaciones de fondo independientes.
    std::array<float, 3> const targetW{
        m_effort == Effort::Normal ? 1.f : 0.f,
        m_effort == Effort::High ? 1.f : 0.f,
        m_effort == Effort::Ultra ? 1.f : 0.f,
    };
    float const effortRate = 1.f - std::exp(-clampedDt * 6.5f);
    float sum = 0.f;
    for (size_t i = 0; i < 3; ++i) {
        m_effortWeights[i] += (targetW[i] - m_effortWeights[i]) * effortRate;
        sum += m_effortWeights[i];
    }
    if (sum > 0.0001f) {
        for (float& w : m_effortWeights) w /= sum;
    }

    syncShaderUniforms();
}

void PaimonAssistantPopup::syncShaderUniforms() {
    auto apply = [&](AssistantShaderQuad* quad) {
        if (!quad) return;
        quad->m_time = m_shaderTime;
        quad->m_working = m_workingMix;
        quad->m_fade = m_fade;
        quad->m_effortWeights = m_effortWeights;
    };
    apply(m_backdropShader);
    apply(m_cornerGlowShader);

    if (m_inputBorder) {
        m_inputBorder->setColor(borderTintForWeights(m_effortWeights));
        float const pulse = m_working ? (160.f + 75.f * std::sin(m_shaderTime * 5.f)) : 120.f;
        m_inputBorder->setOpacity(static_cast<GLubyte>(std::clamp(pulse * m_fade, 0.f, 255.f)));
    }
}

void PaimonAssistantPopup::registerWithTouchDispatcher() {
    CCDirector::get()->getTouchDispatcher()->addTargetedDelegate(this, kTouchPriority, true);
}

bool PaimonAssistantPopup::ccTouchBegan(CCTouch*, CCEvent*) {
    // modal: el fondo traga el toque (el menu tiene prioridad mayor).
    return true;
}

void PaimonAssistantPopup::ccTouchMoved(CCTouch*, CCEvent*) {}
void PaimonAssistantPopup::ccTouchEnded(CCTouch*, CCEvent*) {}
void PaimonAssistantPopup::ccTouchCancelled(CCTouch*, CCEvent*) {}

void PaimonAssistantPopup::keyBackClicked() {
    this->close();
}

void PaimonAssistantPopup::keyDown(cocos2d::enumKeyCodes key, double timestamp) {
    if (key == cocos2d::enumKeyCodes::KEY_Enter
        || key == cocos2d::enumKeyCodes::KEY_NumEnter) {
        runComputerUse();
        return;
    }
    if (key == cocos2d::enumKeyCodes::KEY_Escape) {
        this->close();
        return;
    }
    CCLayer::keyDown(key, timestamp);
}

void PaimonAssistantPopup::buildDim() {
    auto const win = CCDirector::get()->getWinSize();
    m_dim = CCLayerColor::create(ccc4(6, 8, 16, 0), win.width, win.height);
    m_dim->setPosition({0.f, 0.f});
    m_dim->setID("paimon-assistant-dim"_spr);
    this->addChild(m_dim, 0);
}

void PaimonAssistantPopup::buildShaderLayers() {
    auto const win = CCDirector::get()->getWinSize();

    auto* backdropProg = paimon::shaders::loadShader(
        "paimon-assistant-backdrop-v2",
        "cell_vertex.glsl",
        "assistant_backdrop.glsl",
        kVertexShaderFallback,
        kBackdropFragFallback
    );
    if ((m_backdropShader = AssistantShaderQuad::create(win, backdropProg, false))) {
        m_backdropShader->setID("paimon-assistant-backdrop-anim"_spr);
        this->addChild(m_backdropShader, 5);
    }

    auto* cornerGlowProg = paimon::shaders::loadShader(
        "paimon-assistant-corner-glow-v2",
        "cell_vertex.glsl",
        "assistant_corner_glow.glsl",
        kVertexShaderFallback,
        kCornerGlowFragFallback
    );
    if ((m_cornerGlowShader = AssistantShaderQuad::create(win, cornerGlowProg, true))) {
        m_cornerGlowShader->setID("paimon-assistant-corner-glow"_spr);
        // Encima del contenido/fondo con blending aditivo: las esquinas y bordes
        // irradian glow suave hacia adentro sin tapar el centro.
        this->addChild(m_cornerGlowShader, 70);
    }
}

void PaimonAssistantPopup::buildContent() {
    auto const win = CCDirector::get()->getWinSize();
    float const cx = win.width * 0.5f;
    float const cy = win.height * 0.5f;

    m_content = CCNode::create();
    m_content->setPosition({0.f, 0.f});
    m_content->setContentSize(win);
    m_content->setID("paimon-assistant-content"_spr);
    this->addChild(m_content, 50);

    // Titulo dorado con sombra sutil para legibilidad.
    auto* shadow = CCLabelBMFont::create("Preguntale a Paimon", "goldFont.fnt");
    shadow->setScale(0.72f);
    shadow->setColor({0, 0, 0});
    shadow->setOpacity(165);
    shadow->setPosition({cx + 1.5f, cy + 76.5f});
    m_content->addChild(shadow, 49);

    auto* goldLabel = CCLabelBMFont::create("Preguntale a Paimon", "goldFont.fnt");
    goldLabel->setScale(0.72f);
    goldLabel->setColor(paimon::ui::palette::gold);
    goldLabel->setPosition({cx, cy + 78.f});
    goldLabel->setID("paimon-assistant-gold"_spr);
    m_content->addChild(goldLabel, 50);

    auto* subtitle = CCLabelBMFont::create("Computer Use  -  Ctrl+J para ocultar", "chatFont.fnt");
    subtitle->setScale(0.36f);
    subtitle->setColor(paimon::ui::palette::muted);
    subtitle->setOpacity(210);
    subtitle->setPosition({cx, cy + 54.f});
    subtitle->setID("paimon-assistant-subtitle"_spr);
    m_content->addChild(subtitle, 50);

    // Barra de comando unica: inset oscuro limpio + borde tintado al effort activo.
    float const panelW = std::min(430.f, win.width - 110.f);
    constexpr float panelH = 40.f;
    float const inputY = cy + 16.f;

    if (auto* panel = paimon::ui::makeInset({panelW, panelH}, 200)) {
        panel->setPosition({cx - panelW * 0.5f, inputY - panelH * 0.5f});
        panel->setID("paimon-assistant-input-bg"_spr);
        m_content->addChild(panel, 50);
    }

    if (auto* border = geode::NineSlice::create("GJ_square07.png")) {
        border->setScaleMultiplier(0.5f);
        border->setContentSize({panelW, panelH});
        border->setAnchorPoint({0.5f, 0.5f});
        border->setPosition({cx, inputY});
        border->setOpacity(0);
        border->setID("paimon-assistant-input-border"_spr);
        m_content->addChild(border, 51);
        m_inputBorder = border;
    }

    float const inputW = panelW - 24.f;
    m_input = geode::TextInput::create(inputW, "Escribele a Paimon...", "chatFont.fnt");
    if (auto* innerBg = m_input->getBGSprite()) {
        innerBg->setVisible(false);
    }
    m_input->setCommonFilter(geode::CommonFilter::TextEmojis);
    m_input->setMaxCharCount(200);
    m_input->setPosition({cx, inputY});
    m_input->setID("paimon-assistant-input"_spr);
    geode::WeakRef<PaimonAssistantPopup> weakSelf = this;
    m_input->setEnterCallback([weakSelf](std::string const&) {
        if (auto self = weakSelf.lock()) {
            self->runComputerUse();
        }
    });
    m_content->addChild(m_input, 52);

    // Selector de Effort alineado sin solapamientos.
    float const rowY = cy - 30.f;
    auto* effortLabel = paimon::ui::makeLabel("Effort:", 72.f, 0.38f, paimon::ui::palette::muted);
    effortLabel->setAnchorPoint({0.f, 0.5f});
    effortLabel->setID("paimon-assistant-effort-label"_spr);

    m_menu = CCMenu::create();
    m_menu->setPosition({0.f, 0.f});
    m_menu->setContentSize(win);
    m_menu->setID("paimon-assistant-menu"_spr);
    m_content->addChild(m_menu, 53);

    char const* names[] = {"Normal", "High", "Ultra"};
    float const labelW = effortLabel->getScaledContentSize().width;
    float const btnSlotW = kEffortBtnW * 1.06f;
    float const rowW = labelW + kEffortLabelGap + btnSlotW * 3.f + kEffortGap * 2.f;
    float const x0 = cx - rowW * 0.5f;

    effortLabel->setPosition({x0, rowY});
    m_content->addChild(effortLabel, 53);

    float const btnStartX = x0 + labelW + kEffortLabelGap + btnSlotW * 0.5f;
    for (int i = 0; i < 3; ++i) {
        auto effort = static_cast<Effort>(i);
        auto* btn = paimon::ui::makeButton(
            names[i],
            [this, effort]() { this->setEffort(effort); },
            paimon::ui::Btn::Gray, kEffortBtnW, kEffortBtnScale
        );
        btn->setPosition({btnStartX + i * (btnSlotW + kEffortGap), rowY});
        btn->setID(fmt::format("paimon-assistant-effort-{}", i));
        m_menu->addChild(btn);
        m_effortBtns.push_back(btn);
    }
    refreshEffortButtons();

    m_statusLabel = CCLabelBMFont::create(
        fmt::format("Effort: {}  -  Enter para preguntar", effortName()).c_str(),
        "chatFont.fnt"
    );
    m_statusLabel->limitLabelWidth(panelW, 0.36f, 0.22f);
    m_statusLabel->setColor(paimon::ui::palette::muted);
    m_statusLabel->setPosition({cx, cy - 64.f});
    m_statusLabel->setID("paimon-assistant-status"_spr);
    m_content->addChild(m_statusLabel, 53);

    m_askBtn = paimon::ui::makeButton(
        "Preguntar",
        [this]() { this->onAsk(nullptr); },
        paimon::ui::Btn::Green, 144.f, 0.62f
    );
    m_askBtn->setPosition({cx, cy - 102.f});
    m_askBtn->setID("paimon-assistant-ask"_spr);
    m_menu->addChild(m_askBtn);
}

void PaimonAssistantPopup::runShowAnimation() {
    // blur fullscreen con fade + dim sutil; el contenido entra escalonado.
    paimon::popupblur::Config cfg;
    cfg.enabled = true;
    cfg.intensity = 6.5f;
    cfg.darkness = 0.22f;
    paimon::popupblur::captureAndApplyWithConfig(this, cfg);

    if (m_dim) {
        m_dim->stopAllActions();
        m_dim->setOpacity(0);
        m_dim->runAction(CCFadeTo::create(0.25f, 55));
    }
    if (m_content) {
        int i = 0;
        for (auto* child : CCArrayExt<CCNode*>(m_content->getChildren())) {
            paimon::ui::animateIn(child, 0.018f * static_cast<float>(i));
            ++i;
        }
    }

    Loader::get()->queueInMainThread([self = WeakRef<PaimonAssistantPopup>(this)]() {
        if (auto ov = self.lock()) {
            if (ov->m_closing) return;
            if (ov->m_input) ov->m_input->focus();
        }
    });
}

void PaimonAssistantPopup::doClose() {
    if (m_closing) return;
    m_closing = true;

    // el blur de pantalla completa se desvanece solo.
    paimon::popupblur::cleanupWithFade(this, 0.18f);

    if (m_dim) {
        m_dim->stopAllActions();
        m_dim->runAction(CCFadeTo::create(0.18f, 0));
    }
    if (m_content) {
        fadeNodeTree(m_content, 0.16f);
    }

    this->runAction(CCSequence::create(
        CCDelayTime::create(0.19f),
        CCCallFunc::create(this, callfunc_selector(CCNode::removeFromParent)),
        nullptr
    ));
}

void PaimonAssistantPopup::refreshEffortButtons() {
    for (size_t i = 0; i < m_effortBtns.size(); ++i) {
        auto* btn = m_effortBtns[i];
        if (!btn) continue;
        bool const selected = static_cast<int>(i) == static_cast<int>(m_effort);
        paimon::ui::setButtonSkin(
            btn,
            selected ? skinForEffort(static_cast<int>(i)) : paimon::ui::Btn::Gray
        );
        float const scale = selected ? (kEffortBtnScale * 1.06f) : (kEffortBtnScale * 0.96f);
        btn->setScale(scale);
        btn->m_baseScale = scale;
    }
}

void PaimonAssistantPopup::setEffort(Effort effort) {
    if (m_effort == effort) return;
    m_effort = effort;
    saveEffort(effort);
    refreshEffortButtons();
    if (!m_working && m_statusLabel) {
        m_statusLabel->setString(
            fmt::format("Effort: {}  -  Enter para preguntar", effortName()).c_str());
        m_statusLabel->limitLabelWidth(430.f, 0.36f, 0.22f);
    }
}

void PaimonAssistantPopup::onAsk(CCObject*) {
    runComputerUse();
}

void PaimonAssistantPopup::runComputerUse() {
    if (m_working || m_closing) return;
    std::string const prompt = m_input ? std::string(m_input->getString()) : std::string{};
    if (prompt.empty()) {
        PaimonNotify::show("Escribele algo a Paimon primero", NotificationIcon::Warning);
        if (m_input) m_input->focus();
        return;
    }

    m_working = true;
    if (m_askBtn) m_askBtn->setEnabled(false);

    // efecto computer-use: esquinas glow y fondo aceleran + estado pulsante.
    m_statusLabel->setString(
        fmt::format("Paimon mirando tu pantalla... ({})", effortName()).c_str());
    m_statusLabel->limitLabelWidth(430.f, 0.36f, 0.22f);
    m_statusLabel->stopAllActions();
    m_statusLabel->setOpacity(255);
    m_statusLabel->runAction(CCRepeatForever::create(
        CCSequence::create(
            CCFadeTo::create(0.45f, 120),
            CCFadeTo::create(0.45f, 255),
            nullptr
        )
    ));

    float delay = 1.1f;
    if (m_effort == Effort::High) delay = 1.7f;
    if (m_effort == Effort::Ultra) delay = 2.4f;

    auto* seq = CCSequence::create(
        CCDelayTime::create(delay),
        CCCallFuncN::create(this, callfuncN_selector(PaimonAssistantPopup::finishComputerUse)),
        nullptr
    );
    seq->setTag(9917);
    this->runAction(seq);
}

void PaimonAssistantPopup::finishComputerUse(CCNode*) {
    m_working = false;
    if (m_statusLabel) {
        m_statusLabel->stopAllActions();
        m_statusLabel->setOpacity(255);
    }
    std::string const prompt = m_input ? std::string(m_input->getString()) : std::string{};
    if (m_statusLabel) {
        std::string reply = fmt::format("Paimon ({}) entendio: \"{}\"", effortName(), prompt);
        m_statusLabel->setString(reply.c_str());
        m_statusLabel->limitLabelWidth(480.f, 0.36f, 0.22f);
        m_statusLabel->setOpacity(0);
        m_statusLabel->runAction(CCFadeIn::create(0.35f));
    }
    if (m_askBtn) m_askBtn->setEnabled(true);
    PaimonNotify::show(
        fmt::format("Paimon ({}) listo", effortName()).c_str(),
        NotificationIcon::Success
    );
}

} // namespace paimon::assistant
