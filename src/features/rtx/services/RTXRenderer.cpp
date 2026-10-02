#include "RTXRenderer.hpp"

#include "RTXManager.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/GLSLLoader.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>
#include <Geode/cocos/shaders/ccGLStateCache.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>

#ifndef GL_RGBA16F
    #define GL_RGBA16F 0x881A
#endif
#ifndef GL_HALF_FLOAT
    #define GL_HALF_FLOAT 0x140B
#endif
#ifndef GL_HALF_FLOAT_OES
    #define GL_HALF_FLOAT_OES 0x8D61
#endif
#ifndef GL_VERTEX_ARRAY_BINDING
    #define GL_VERTEX_ARRAY_BINDING 0x85B5
#endif
#ifndef GL_READ_FRAMEBUFFER
    #define GL_READ_FRAMEBUFFER 0x8CA8
    #define GL_DRAW_FRAMEBUFFER 0x8CA9
    #define GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#endif

using namespace cocos2d;
using namespace geode::prelude;

namespace paimon::rtx {

namespace {

constexpr unsigned kIdleReleaseFrames = 300;

constexpr int kMaxTraceLongEdge = 1280;

constexpr float kMinAdaptiveScale = 0.20f;
constexpr int kMinRaySteps = 4;
constexpr int kMinRayCount = 1;
constexpr int kMinAtrousPasses = 0;
constexpr int kMinBloomLevels = 1;
constexpr int kMaxGovernorSkip = 3;
constexpr int kAdaptPeriodFrames = 30;
constexpr int kUpDwellPeriods = 4;

int contextMajorVersion() {
    auto const* version = reinterpret_cast<char const*>(glGetString(GL_VERSION));
    if (!version) return 0;
    while (*version && (*version < '0' || *version > '9')) ++version;
    return static_cast<int>(std::strtol(version, nullptr, 10));
}

void bindTexture(GLuint unit, GLuint texture) {
    // Texture edits require the active unit even when the binding is unchanged.
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, texture);
}

class FrameState {
    struct Attribute {
        GLint enabled = 0, size = 4, type = GL_FLOAT, normalized = GL_FALSE;
        GLint stride = 0, buffer = 0;
        void* pointer = nullptr;
    };

    static constexpr GLenum kCaps[] = {
        GL_BLEND, GL_SCISSOR_TEST, GL_DEPTH_TEST, GL_STENCIL_TEST, GL_CULL_FACE
    };
    GLint m_program = 0, m_arrayBuffer = 0, m_activeTexture = GL_TEXTURE0;
    GLint m_textures[6] = {};
    GLboolean m_caps[5] = {}, m_colorMask[4] = {};
    Attribute m_attributes[3];
    bool m_separateFramebuffers = false;
    GLint m_readFbo = 0;
#if CC_TEXTURE_ATLAS_USE_VAO
    GLint m_vao = 0;
#endif

public:
    GLint fbo = 0;
    GLint viewport[4] = {};

    FrameState() {
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
        glGetIntegerv(GL_VIEWPORT, viewport);
        m_separateFramebuffers = contextMajorVersion() >= 3;
        if (m_separateFramebuffers) glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &m_readFbo);
        glGetIntegerv(GL_CURRENT_PROGRAM, &m_program);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &m_arrayBuffer);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &m_activeTexture);
        glGetBooleanv(GL_COLOR_WRITEMASK, m_colorMask);
        for (GLuint i = 0; i < 6; ++i) {
            glActiveTexture(GL_TEXTURE0 + i);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &m_textures[i]);
        }
        for (int i = 0; i < 5; ++i) {
            m_caps[i] = glIsEnabled(kCaps[i]);
            glDisable(kCaps[i]);
        }
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
#if CC_TEXTURE_ATLAS_USE_VAO
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &m_vao);
        glBindVertexArray(0);
#endif
        for (GLuint i = 0; i < 3; ++i) {
            auto& a = m_attributes[i];
            glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &a.enabled);
            glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_SIZE, &a.size);
            glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_TYPE, &a.type);
            glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &a.normalized);
            glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &a.stride);
            glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &a.buffer);
            glGetVertexAttribPointerv(i, GL_VERTEX_ATTRIB_ARRAY_POINTER, &a.pointer);
        }
    }

    ~FrameState() {
        // Raw GL calls leave Cocos' cached state unchanged across the pass.
        for (GLuint i = 0; i < 3; ++i) {
            auto const& a = m_attributes[i];
            glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(a.buffer));
            glVertexAttribPointer(i, a.size, static_cast<GLenum>(a.type),
                                  static_cast<GLboolean>(a.normalized), a.stride, a.pointer);
            if (a.enabled) glEnableVertexAttribArray(i);
            else glDisableVertexAttribArray(i);
        }
#if CC_TEXTURE_ATLAS_USE_VAO
        glBindVertexArray(static_cast<GLuint>(m_vao));
#endif
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(m_arrayBuffer));
        glUseProgram(static_cast<GLuint>(m_program));
        for (GLuint i = 0; i < 6; ++i) bindTexture(i, static_cast<GLuint>(m_textures[i]));
        glActiveTexture(static_cast<GLenum>(m_activeTexture));
        if (m_separateFramebuffers) {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(fbo));
            glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(m_readFbo));
        } else {
            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fbo));
        }
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glColorMask(m_colorMask[0], m_colorMask[1], m_colorMask[2], m_colorMask[3]);
        for (int i = 0; i < 5; ++i) {
            if (m_caps[i]) glEnable(kCaps[i]);
            else glDisable(kCaps[i]);
        }
    }
};

constexpr GLfloat kQuad[] = {
    -1.f,  1.f,  0.f, 1.f,
    -1.f, -1.f,  0.f, 0.f,
     1.f, -1.f,  1.f, 0.f,

    -1.f,  1.f,  0.f, 1.f,
     1.f, -1.f,  1.f, 0.f,
     1.f,  1.f,  1.f, 1.f,
};

GLuint compilePart(GLenum type, std::string const& src) {
    GLuint id = glCreateShader(type);
    if (!id) return 0;

    char const* ptr = src.c_str();
    glShaderSource(id, 1, &ptr, nullptr);
    glCompileShader(id);

    GLint ok = 0;
    glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char infoLog[1024] = {};
        glGetShaderInfoLog(id, sizeof(infoLog) - 1, nullptr, infoLog);
        log::warn("[PaimonRTX] fallo al compilar {}: {}",
                  type == GL_VERTEX_SHADER ? "vertex" : "fragment", infoLog);
        glDeleteShader(id);
        return 0;
    }
    return id;
}

GLuint linkProgram(char const* tag, std::string const& vert, std::string const& frag) {
    GLuint vs = compilePart(GL_VERTEX_SHADER, vert);
    if (!vs) return 0;
    GLuint fs = compilePart(GL_FRAGMENT_SHADER, frag);
    if (!fs) {
        glDeleteShader(vs);
        return 0;
    }

    GLuint prog = glCreateProgram();
    if (!prog) {
        glDeleteShader(vs);
        glDeleteShader(fs);
        return 0;
    }
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, kCCVertexAttrib_Position, "aPosition");
    glBindAttribLocation(prog, kCCVertexAttrib_TexCoords, "aTexCoord");
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linked = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) {
        char infoLog[1024] = {};
        glGetProgramInfoLog(prog, sizeof(infoLog) - 1, nullptr, infoLog);
        log::warn("[PaimonRTX] fallo al enlazar {}: {}", tag, infoLog);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

void bindSampler(GLuint prog, char const* name, int unit) {
    GLint loc = glGetUniformLocation(prog, name);
    if (loc != -1) glUniform1i(loc, unit);
}

bool probeHdrTargets(GLint& format, GLenum& type) {
    format = GL_RGBA16F;
    type = GL_HALF_FLOAT;
#if defined(GEODE_IS_MOBILE)
    if (contextMajorVersion() < 3) {
        auto const* raw = reinterpret_cast<char const*>(glGetString(GL_EXTENSIONS));
        std::string_view const extensions = raw ? raw : "";
        if (extensions.find("GL_OES_texture_half_float") == std::string_view::npos
            || extensions.find("GL_OES_texture_half_float_linear") == std::string_view::npos) {
            return false;
        }
        format = GL_RGBA;
        type = GL_HALF_FLOAT_OES;
    }
#endif
    GLuint tex = 0;
    GLuint fbo = 0;
    glGenTextures(1, &tex);
    bindTexture(0, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, format, 4, 4, 0, GL_RGBA, type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    bool const ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE
                 && glGetError() == GL_NO_ERROR;

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &tex);
    bindTexture(0, 0);
    while (glGetError() != GL_NO_ERROR) {}
    return ok;
}

} // namespace

RTXRenderer& RTXRenderer::get() {
    static RTXRenderer instance;
    return instance;
}

bool RTXRenderer::ensurePrograms() {
    if (m_trace.id && glIsProgram(m_trace.id) == GL_TRUE) return true;

    auto vert = paimon::shaders::readShaderFile("rtx_fullscreen.vert");
    // glsl has no includes: preamble prepends every fragment.
    auto common = paimon::shaders::readShaderFile("rtx_common.glsl");
    auto traceSrc = paimon::shaders::readShaderFile("rtx_trace.glsl");
    auto temporalSrc = paimon::shaders::readShaderFile("rtx_temporal.glsl");
    auto atrousSrc = paimon::shaders::readShaderFile("rtx_atrous.glsl");
    auto bloomSrc = paimon::shaders::readShaderFile("rtx_bloom.glsl");
    auto compositeSrc = paimon::shaders::readShaderFile("rtx_composite.glsl");

    if (vert.empty() || common.empty() || traceSrc.empty() || temporalSrc.empty()
        || atrousSrc.empty() || bloomSrc.empty() || compositeSrc.empty()) {
        log::warn("[PaimonRTX] faltan shaders en resources/shaders - RTX desactivado");
        return false;
    }

    GLuint trace = linkProgram("trace", vert, common + traceSrc);
    GLuint temporal = linkProgram("temporal", vert, common + temporalSrc);
    GLuint atrous = linkProgram("atrous", vert, common + atrousSrc);
    GLuint bloom = linkProgram("bloom", vert, common + bloomSrc);
    GLuint composite = linkProgram("composite", vert, common + compositeSrc);

    if (!trace || !temporal || !atrous || !bloom || !composite) {
        if (trace) glDeleteProgram(trace);
        if (temporal) glDeleteProgram(temporal);
        if (atrous) glDeleteProgram(atrous);
        if (bloom) glDeleteProgram(bloom);
        if (composite) glDeleteProgram(composite);
        return false;
    }

    m_trace = TraceProgram{};
    m_trace.id               = trace;
    m_trace.texel            = glGetUniformLocation(trace, "u_texel");
    m_trace.frame            = glGetUniformLocation(trace, "u_frame");
    m_trace.rayCount         = glGetUniformLocation(trace, "u_rayCount");
    m_trace.raySteps         = glGetUniformLocation(trace, "u_raySteps");
    m_trace.rayDistance      = glGetUniformLocation(trace, "u_rayDistance");
    m_trace.stepGrowth       = glGetUniformLocation(trace, "u_stepGrowth");
    m_trace.lightThreshold   = glGetUniformLocation(trace, "u_lightThreshold");
    m_trace.lightRange       = glGetUniformLocation(trace, "u_lightRange");
    m_trace.bounceFalloff    = glGetUniformLocation(trace, "u_bounceFalloff");
    m_trace.giSaturation     = glGetUniformLocation(trace, "u_giSaturation");
    m_trace.giStrength       = glGetUniformLocation(trace, "u_giStrength");
    m_trace.normalStrength   = glGetUniformLocation(trace, "u_normalStrength");
    m_trace.thickness        = glGetUniformLocation(trace, "u_thickness");
    m_trace.aoRadius         = glGetUniformLocation(trace, "u_aoRadius");
    m_trace.aoPower          = glGetUniformLocation(trace, "u_aoPower");
    m_trace.reflectStrength  = glGetUniformLocation(trace, "u_reflectStrength");
    m_trace.reflectRoughness = glGetUniformLocation(trace, "u_reflectRoughness");
    m_trace.reflectFresnel   = glGetUniformLocation(trace, "u_reflectFresnel");
    m_trace.reflectFade      = glGetUniformLocation(trace, "u_reflectFade");
    glUseProgram(trace);
    bindSampler(trace, "u_scene", 0);

    m_temporalProg = TemporalProgram{};
    m_temporalProg.id          = temporal;
    m_temporalProg.texel       = glGetUniformLocation(temporal, "u_texel");
    m_temporalProg.temporal    = glGetUniformLocation(temporal, "u_temporal");
    m_temporalProg.clampSigma  = glGetUniformLocation(temporal, "u_clampSigma");
    m_temporalProg.reprojRow0  = glGetUniformLocation(temporal, "u_reprojRow0");
    m_temporalProg.reprojRow1  = glGetUniformLocation(temporal, "u_reprojRow1");
    m_temporalProg.historyValid = glGetUniformLocation(temporal, "u_historyValid");
    m_temporalProg.outVariance  = glGetUniformLocation(temporal, "u_outVariance");
    glUseProgram(temporal);
    bindSampler(temporal, "u_current", 0);
    bindSampler(temporal, "u_history", 1);
    bindSampler(temporal, "u_histVar", 2);
    bindSampler(temporal, "u_guideNow", 3);
    bindSampler(temporal, "u_guidePrev", 4);

    m_atrousProg = AtrousProgram{};
    m_atrousProg.id     = atrous;
    m_atrousProg.texel  = glGetUniformLocation(atrous, "u_texel");
    m_atrousProg.stride = glGetUniformLocation(atrous, "u_stride");
    m_atrousProg.phi    = glGetUniformLocation(atrous, "u_phi");
    m_atrousProg.wide   = glGetUniformLocation(atrous, "u_wide");
    glUseProgram(atrous);
    bindSampler(atrous, "u_src", 0);
    bindSampler(atrous, "u_guide", 1);
    bindSampler(atrous, "u_var", 2);

    m_bloom = BloomProgram{};
    m_bloom.id         = bloom;
    m_bloom.texel      = glGetUniformLocation(bloom, "u_texel");
    m_bloom.mode       = glGetUniformLocation(bloom, "u_mode");
    m_bloom.threshold  = glGetUniformLocation(bloom, "u_threshold");
    m_bloom.softKnee   = glGetUniformLocation(bloom, "u_softKnee");
    m_bloom.radius     = glGetUniformLocation(bloom, "u_radius");
    m_bloom.blend      = glGetUniformLocation(bloom, "u_blend");
    m_bloom.anamorphic = glGetUniformLocation(bloom, "u_anamorphic");
    m_bloom.lightPos   = glGetUniformLocation(bloom, "u_lightPos");
    m_bloom.decay      = glGetUniformLocation(bloom, "u_decay");
    m_bloom.density    = glGetUniformLocation(bloom, "u_density");
    m_bloom.tonemap    = glGetUniformLocation(bloom, "u_tonemap");
    m_bloom.hdrRange   = glGetUniformLocation(bloom, "u_hdrRange");
    m_bloom.giMix      = glGetUniformLocation(bloom, "u_giMix");
    m_bloom.adaptRate  = glGetUniformLocation(bloom, "u_adaptRate");
    m_bloom.reprojRow0 = glGetUniformLocation(bloom, "u_reprojRow0");
    m_bloom.reprojRow1 = glGetUniformLocation(bloom, "u_reprojRow1");
    glUseProgram(bloom);
    bindSampler(bloom, "u_src", 0);
    bindSampler(bloom, "u_add", 1);
    bindSampler(bloom, "u_guide", 2);

    m_composite = CompositeProgram{};
    m_composite.id            = composite;
    m_composite.texel         = glGetUniformLocation(composite, "u_texel");
    m_composite.giTexel       = glGetUniformLocation(composite, "u_giTexel");
    m_composite.reprojRow0    = glGetUniformLocation(composite, "u_reprojRow0");
    m_composite.reprojRow1    = glGetUniformLocation(composite, "u_reprojRow1");
    m_composite.historyValid  = glGetUniformLocation(composite, "u_historyValid");
    m_composite.time          = glGetUniformLocation(composite, "u_time");
    m_composite.mixAmount     = glGetUniformLocation(composite, "u_mix");
    m_composite.aoStrength    = glGetUniformLocation(composite, "u_aoStrength");
    m_composite.bloomStrength = glGetUniformLocation(composite, "u_bloomStrength");
    m_composite.rayStrength   = glGetUniformLocation(composite, "u_rayStrength");
    m_composite.tonemap       = glGetUniformLocation(composite, "u_tonemap");
    m_composite.exposure      = glGetUniformLocation(composite, "u_exposure");
    m_composite.adaptKey      = glGetUniformLocation(composite, "u_adaptKey");
    m_composite.contrast      = glGetUniformLocation(composite, "u_contrast");
    m_composite.saturation    = glGetUniformLocation(composite, "u_saturation");
    m_composite.temperature   = glGetUniformLocation(composite, "u_temperature");
    m_composite.tint          = glGetUniformLocation(composite, "u_tint");
    m_composite.gammaV        = glGetUniformLocation(composite, "u_gammaV");
    m_composite.ca            = glGetUniformLocation(composite, "u_ca");
    m_composite.vignette      = glGetUniformLocation(composite, "u_vignette");
    m_composite.grain         = glGetUniformLocation(composite, "u_grain");
    m_composite.sharpen       = glGetUniformLocation(composite, "u_sharpen");
    glUseProgram(composite);
    bindSampler(composite, "u_scene", 0);
    bindSampler(composite, "u_gi", 1);
    bindSampler(composite, "u_bloom", 2);
    bindSampler(composite, "u_rays", 3);
    bindSampler(composite, "u_adapt", 4);
    bindSampler(composite, "u_guide", 5);

    if (!m_vbo) {
        glGenBuffers(1, &m_vbo);
        if (!m_vbo) return false;
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(kQuad), kQuad, GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    if (!m_blackTex) {
        unsigned char const black[4] = {0, 0, 0, 0};
        glGenTextures(1, &m_blackTex);
        bindTexture(0, m_blackTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    m_hdr = probeHdrTargets(m_hdrFormat, m_hdrType);
#if defined(GEODE_IS_MOBILE)
    auto const* rawExtensions = reinterpret_cast<char const*>(glGetString(GL_EXTENSIONS));
    std::string_view const extensions = rawExtensions ? rawExtensions : "";
    m_canGenerateMips = contextMajorVersion() >= 3
                    || extensions.find("GL_OES_texture_npot") != std::string_view::npos;
#endif
    log::info("[PaimonRTX] programas compilados (rango alto {})",
              m_hdr ? "disponible" : "no soportado");
    return true;
}

bool RTXRenderer::makeTarget(Target& t, int w, int h, bool hdr) {
    w = std::max(1, w);
    h = std::max(1, h);
    hdr = hdr && m_hdr;
    if (t.fbo && t.w == w && t.h == h) return true;

    dropTarget(t);

    glGenTextures(1, &t.tex);
    if (!t.tex) return false;
    bindTexture(0, t.tex);
    glTexImage2D(GL_TEXTURE_2D, 0, hdr ? m_hdrFormat : GL_RGBA, w, h, 0, GL_RGBA,
                 hdr ? m_hdrType : GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &t.fbo);
    if (!t.fbo) {
        dropTarget(t);
        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE
        || glGetError() != GL_NO_ERROR) {
        log::warn("[PaimonRTX] FBO incompleto ({}x{})", w, h);
        dropTarget(t);
        return false;
    }

    GLfloat prevClear[4] = {0.f, 0.f, 0.f, 1.f};
    glGetFloatv(GL_COLOR_CLEAR_VALUE, prevClear);
    glViewport(0, 0, w, h);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    glClearColor(prevClear[0], prevClear[1], prevClear[2], prevClear[3]);

    t.w = w;
    t.h = h;
    return true;
}

void RTXRenderer::dropTarget(Target& t) {
    if (t.fbo) glDeleteFramebuffers(1, &t.fbo);
    if (t.tex) glDeleteTextures(1, &t.tex);
    t = Target{};
}

bool RTXRenderer::ensureFullTargets(int srcW, int srcH) {
    if (m_sceneTex && srcW == m_sceneW && srcH == m_sceneH) return true;

    if (m_sceneTex) {
        glDeleteTextures(1, &m_sceneTex);
        m_sceneTex = 0;
    }

    glGenTextures(1, &m_sceneTex);
    if (!m_sceneTex) return false;
    bindTexture(0, m_sceneTex);
    // GLES2 can copy opaque framebuffers into RGB textures without an alpha channel.
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, srcW, srcH, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // float bloom: 8-bit expansion clips at 1.0.
    for (int i = 0; i < kBloomLevels; ++i) {
        int const w = std::max(1, srcW >> (i + 1));
        int const h = std::max(1, srcH >> (i + 1));
        if (!makeTarget(m_bloomDown[i], w, h, true)) return false;
        if (!makeTarget(m_bloomUp[i], w, h, true)) return false;
    }

    int const rayLevel = std::min(kBloomLevels - 1, 2);
    if (!makeTarget(m_rays, m_bloomDown[rayLevel].w, m_bloomDown[rayLevel].h, true)) return false;

    if (m_hdr) {
        if (!makeTarget(m_exposure[0], 1, 1, true)) return false;
        if (!makeTarget(m_exposure[1], 1, 1, true)) return false;
    }
    m_hasExposure = false;

    m_sceneW = srcW;
    m_sceneH = srcH;
    log::debug("[PaimonRTX] objetivos de pantalla completa {}x{}", srcW, srcH);
    return true;
}

bool RTXRenderer::ensureTraceTargets(int srcW, int srcH, float scale) {
    float const cappedScale = std::min(scale, static_cast<float>(kMaxTraceLongEdge)
                                                   / static_cast<float>(std::max(srcW, srcH)));
    int const w = std::max(1, static_cast<int>(std::lround(srcW * cappedScale)));
    int const h = std::max(1, static_cast<int>(std::lround(srcH * cappedScale)));

    if (m_traceRT.fbo && w == m_traceW && h == m_traceH) return true;

    if (!makeTarget(m_traceSrc, w, h)) return false;
    if (!makeTarget(m_guideHistory, w, h)) return false;
    if (!makeTarget(m_traceRT, w, h, true)) return false;
    if (!makeTarget(m_history[0], w, h, true)) return false;
    if (!makeTarget(m_history[1], w, h, true)) return false;
    if (!makeTarget(m_variance[0], w, h, true)) return false;
    if (!makeTarget(m_variance[1], w, h, true)) return false;
    if (!makeTarget(m_atrous[0], w, h, true)) return false;
    if (!makeTarget(m_atrous[1], w, h, true)) return false;

    m_traceW = w;
    m_traceH = h;
    invalidateHistory();
    log::debug("[PaimonRTX] objetivos de trazado {}x{} (escala {:.2f})", w, h, scale);
    return true;
}

void RTXRenderer::releaseAll() {
    dropTarget(m_traceSrc);
    dropTarget(m_guideHistory);
    dropTarget(m_traceRT);
    dropTarget(m_history[0]);
    dropTarget(m_history[1]);
    dropTarget(m_variance[0]);
    dropTarget(m_variance[1]);
    dropTarget(m_atrous[0]);
    dropTarget(m_atrous[1]);
    for (int i = 0; i < kBloomLevels; ++i) {
        dropTarget(m_bloomDown[i]);
        dropTarget(m_bloomUp[i]);
    }
    dropTarget(m_rays);
    dropTarget(m_exposure[0]);
    dropTarget(m_exposure[1]);

    if (m_sceneTex) {
        glDeleteTextures(1, &m_sceneTex);
        m_sceneTex = 0;
    }
    m_sceneW = 0;
    m_sceneH = 0;
    m_traceW = 0;
    m_traceH = 0;
    m_bloomResultTex = 0;
    m_giResultTex = 0;
    invalidateHistory();
    m_hasExposure = false;
}

void RTXRenderer::onGLContextReload() {
    // old context still alive: everything rebuilds lazily.
    releaseAll();

    if (m_vbo) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_blackTex) {
        glDeleteTextures(1, &m_blackTex);
        m_blackTex = 0;
    }
    if (m_trace.id) glDeleteProgram(m_trace.id);
    if (m_temporalProg.id) glDeleteProgram(m_temporalProg.id);
    if (m_atrousProg.id) glDeleteProgram(m_atrousProg.id);
    if (m_bloom.id) glDeleteProgram(m_bloom.id);
    if (m_composite.id) glDeleteProgram(m_composite.id);

    m_trace = TraceProgram{};
    m_temporalProg = TemporalProgram{};
    m_atrousProg = AtrousProgram{};
    m_bloom = BloomProgram{};
    m_composite = CompositeProgram{};

    m_wasActive = false;
    m_broken = false;
    m_hdr = true;
}

void RTXRenderer::drawInto(Target const& t) {
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glViewport(0, 0, t.w, t.h);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void RTXRenderer::invalidateHistory() {
    m_hasHistory = false;
    m_giResultTex = 0;
    m_reprojRow0 = {1.f, 0.f, 0.f};
    m_reprojRow1 = {0.f, 1.f, 0.f};
}

void RTXRenderer::updateReprojection() {
    auto* director = CCDirector::get();
    auto* scene = director->getRunningScene();
    auto* game = GJBaseGameLayer::get();
    auto* layer = game ? game->m_objectLayer : nullptr;
    if (scene != m_scene || layer != m_cameraLayer) {
        invalidateHistory();
        m_hasExposure = false;
        m_scene = scene;
        m_cameraLayer = layer;
    }

    m_camera = CameraTransform{};
    if (layer) {
        auto const win = director->getWinSize();
        if (win.width <= 0.f || win.height <= 0.f) {
            invalidateHistory();
            return;
        }
        auto const xf = layer->nodeToWorldTransform();
        m_camera = {xf.a / win.width, xf.b / win.height,
                    xf.c / win.width, xf.d / win.height,
                    xf.tx / win.width, xf.ty / win.height};
    }
    if (!m_hasHistory) return;

    auto const& n = m_camera;
    auto const& p = m_historyCamera;
    float const det = n.a * n.d - n.b * n.c;
    if (!std::isfinite(det) || std::abs(det) < 1e-12f) {
        invalidateHistory();
        return;
    }
    float const a = (p.a * n.d - p.c * n.b) / det;
    float const c = (p.c * n.a - p.a * n.c) / det;
    float const b = (p.b * n.d - p.d * n.b) / det;
    float const d = (p.d * n.a - p.b * n.c) / det;
    m_reprojRow0 = {a, c, p.tx - a * n.tx - c * n.ty};
    m_reprojRow1 = {b, d, p.ty - b * n.tx - d * n.ty};
    for (auto const& row : {m_reprojRow0, m_reprojRow1}) {
        for (float v : row) {
            if (!std::isfinite(v)) {
                invalidateHistory();
                return;
            }
        }
    }
    if (std::abs(m_reprojRow0[2]) > 0.25f || std::abs(m_reprojRow1[2]) > 0.25f
        || std::abs(a * d - b * c) < 0.25f || std::abs(a * d - b * c) > 4.f) {
        invalidateHistory();
    }
}

void RTXRenderer::syncGovernorEffectives(RTXConfig const& cfg) {
    m_activeScale = std::clamp(cfg.renderScale, kMinAdaptiveScale, 1.f);
    m_effRayCount = std::clamp(cfg.rayCount, kMinRayCount, 16);
    m_effRaySteps = std::clamp(cfg.raySteps, kMinRaySteps, 32);
    m_effAtrous = std::clamp(cfg.atrousPasses, kMinAtrousPasses, 5);
    m_effBloom = std::clamp(cfg.bloomPasses, kMinBloomLevels, kBloomLevels);
    m_effSkip = std::clamp(cfg.frameSkip, 0, kMaxGovernorSkip);
    m_adaptTicks = 0;
    m_upTicks = 0;
}

void RTXRenderer::clampGovernorToConfig(RTXConfig const& cfg) {
    // only degrades below config: instant ceiling, dwell on the way up.
    float const scaleCeil = std::clamp(cfg.renderScale, kMinAdaptiveScale, 1.f);
    if (m_activeScale > scaleCeil) m_activeScale = scaleCeil;
    m_effRayCount = std::min(m_effRayCount, std::clamp(cfg.rayCount, kMinRayCount, 16));
    m_effRaySteps = std::min(m_effRaySteps, std::clamp(cfg.raySteps, kMinRaySteps, 32));
    m_effAtrous = std::min(m_effAtrous, std::clamp(cfg.atrousPasses, kMinAtrousPasses, 5));
    m_effBloom = std::min(m_effBloom, std::clamp(cfg.bloomPasses, kMinBloomLevels, kBloomLevels));
    m_effSkip = std::max(m_effSkip, std::clamp(cfg.frameSkip, 0, kMaxGovernorSkip));
}

bool RTXRenderer::governorStepDown(float budget) {
    if (m_activeScale > kMinAdaptiveScale + 1e-6f) {
        float const over = budget > 0.f ? m_frameMs / budget : 2.f;
        float const step = over > 1.5f ? 0.10f : 0.05f;
        m_activeScale = std::max(kMinAdaptiveScale, m_activeScale - step);
        return true;
    }
    if (m_effRaySteps > kMinRaySteps) {
        m_effRaySteps = std::max(kMinRaySteps, m_effRaySteps - 2);
        return true;
    }
    if (m_effRayCount > kMinRayCount) {
        m_effRayCount = std::max(kMinRayCount, m_effRayCount - 1);
        return true;
    }
    if (m_effAtrous > kMinAtrousPasses) {
        --m_effAtrous;
        return true;
    }
    if (m_effBloom > kMinBloomLevels) {
        --m_effBloom;
        return true;
    }
    if (m_effSkip < kMaxGovernorSkip) {
        ++m_effSkip;
        return true;
    }
    return false;
}

bool RTXRenderer::governorStepUp(RTXConfig const& cfg) {
    // reverse of stepping down: cadence first, scale last.
    int const wantSkip = std::clamp(cfg.frameSkip, 0, kMaxGovernorSkip);
    if (m_effSkip > wantSkip) {
        --m_effSkip;
        return true;
    }
    int const wantBloom = std::clamp(cfg.bloomPasses, kMinBloomLevels, kBloomLevels);
    if (m_effBloom < wantBloom) {
        ++m_effBloom;
        return true;
    }
    int const wantAtrous = std::clamp(cfg.atrousPasses, kMinAtrousPasses, 5);
    if (m_effAtrous < wantAtrous) {
        ++m_effAtrous;
        return true;
    }
    int const wantCount = std::clamp(cfg.rayCount, kMinRayCount, 16);
    if (m_effRayCount < wantCount) {
        ++m_effRayCount;
        return true;
    }
    int const wantSteps = std::clamp(cfg.raySteps, kMinRaySteps, 32);
    if (m_effRaySteps < wantSteps) {
        m_effRaySteps = std::min(wantSteps, m_effRaySteps + 2);
        return true;
    }
    float const wantScale = std::clamp(cfg.renderScale, kMinAdaptiveScale, 1.f);
    if (m_activeScale < wantScale - 1e-6f) {
        m_activeScale = std::min(wantScale, m_activeScale + 0.05f);
        return true;
    }
    return false;
}

void RTXRenderer::updateAdaptiveScale(RTXConfig const& cfg) {
    if (!cfg.adaptive) {
        syncGovernorEffectives(cfg);
        return;
    }
    clampGovernorToConfig(cfg);

    int const fps = cfg.targetFps > 0 ? cfg.targetFps : 60;
    float const budget = 1000.f / static_cast<float>(fps);
    if (budget <= 0.f || m_frameMs <= 0.f) return;

    if (++m_adaptTicks < kAdaptPeriodFrames) return;
    m_adaptTicks = 0;

    if (m_frameMs > budget * 1.15f) {
        governorStepDown(budget);
        m_upTicks = 0;
        return;
    }

    // threshold just over budget for vsync; stepping up is 4x slower.
    if (m_frameMs < budget * 1.02f) {
        if (++m_upTicks >= kUpDwellPeriods) {
            m_upTicks = 0;
            governorStepUp(cfg);
        }
        return;
    }
    // dead band: hold and demand sustained headroom before stepping up.
    m_upTicks = 0;
}

void RTXRenderer::runTrace(RTXConfig const& cfg) {
    float const texelX = 1.f / static_cast<float>(m_traceW);
    float const texelY = 1.f / static_cast<float>(m_traceH);

    glUseProgram(m_bloom.id);
    glUniform1f(m_bloom.mode, 1.f);
    glUniform2f(m_bloom.texel, 1.f / static_cast<float>(m_sceneW),
                               1.f / static_cast<float>(m_sceneH));
    bindTexture(0, m_sceneTex);
    bindTexture(1, m_blackTex);
    drawInto(m_traceSrc);

    glUseProgram(m_trace.id);
    glUniform2f(m_trace.texel, texelX, texelY);
    glUniform1f(m_trace.frame, static_cast<float>(m_frameCounter % 4096u));
    glUniform1f(m_trace.rayCount, static_cast<float>(m_effRayCount));
    glUniform1f(m_trace.raySteps, static_cast<float>(m_effRaySteps));
    glUniform1f(m_trace.rayDistance, cfg.rayDistance);
    glUniform1f(m_trace.stepGrowth, cfg.stepGrowth);
    glUniform1f(m_trace.lightThreshold, cfg.lightThreshold);
    glUniform1f(m_trace.lightRange, cfg.lightRange);
    glUniform1f(m_trace.bounceFalloff, cfg.bounceFalloff);
    glUniform1f(m_trace.giSaturation, cfg.giSaturation);
    glUniform1f(m_trace.giStrength, cfg.giStrength);
    glUniform1f(m_trace.normalStrength, cfg.normalStrength);
    glUniform1f(m_trace.thickness, cfg.thickness);
    glUniform1f(m_trace.aoRadius, cfg.aoRadius);
    glUniform1f(m_trace.aoPower, cfg.aoPower);
    glUniform1f(m_trace.reflectStrength, cfg.reflectStrength);
    glUniform1f(m_trace.reflectRoughness, cfg.reflectRoughness);
    glUniform1f(m_trace.reflectFresnel, cfg.reflectFresnel);
    glUniform1f(m_trace.reflectFade, cfg.reflectFade);
    bindTexture(0, m_traceSrc.tex);
    drawInto(m_traceRT);

    runFilter(cfg);
}

void RTXRenderer::runFilter(RTXConfig const& cfg) {
    float const texelX = 1.f / static_cast<float>(m_traceW);
    float const texelY = 1.f / static_cast<float>(m_traceH);

    int const dst = 1 - m_historyIndex;
    int const histSrc = m_historyIndex;
    glUseProgram(m_temporalProg.id);
    glUniform2f(m_temporalProg.texel, texelX, texelY);
    glUniform1f(m_temporalProg.temporal, cfg.temporal);
    glUniform1f(m_temporalProg.clampSigma, cfg.ghostClamp ? cfg.clampSigma : -1.f);
    glUniform3f(m_temporalProg.reprojRow0, m_reprojRow0[0], m_reprojRow0[1], m_reprojRow0[2]);
    glUniform3f(m_temporalProg.reprojRow1, m_reprojRow1[0], m_reprojRow1[1], m_reprojRow1[2]);
    glUniform1f(m_temporalProg.historyValid, m_hasHistory ? 1.f : 0.f);
    glUniform1f(m_temporalProg.outVariance, 0.f);
    bindTexture(0, m_traceRT.tex);
    bindTexture(1, m_history[histSrc].tex);
    bindTexture(2, m_variance[histSrc].tex);
    bindTexture(3, m_traceSrc.tex);
    bindTexture(4, m_guideHistory.tex);
    drawInto(m_history[dst]);

    glUniform1f(m_temporalProg.outVariance, 1.f);
    bindTexture(0, m_traceRT.tex);
    bindTexture(1, m_history[histSrc].tex);
    bindTexture(2, m_variance[histSrc].tex);
    drawInto(m_variance[dst]);
    m_historyIndex = dst;

    glBindFramebuffer(GL_FRAMEBUFFER, m_traceSrc.fbo);
    bindTexture(0, m_guideHistory.tex);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, m_traceW, m_traceH);
    m_historyCamera = m_camera;
    m_hasHistory = true;
    m_reprojRow0 = {1.f, 0.f, 0.f};
    m_reprojRow1 = {0.f, 1.f, 0.f};

    int const passes = std::clamp(m_effAtrous, 0, 5);
    if (passes == 0) {
        m_giResultTex = m_history[m_historyIndex].tex;
        return;
    }

    // high phi keeps edges, low phi cleans at the cost of flattening.
    float const phi = 48.f - std::clamp(cfg.denoise, 0.f, 4.f) * 11.f;

    glUseProgram(m_atrousProg.id);
    glUniform2f(m_atrousProg.texel, texelX, texelY);
    glUniform1f(m_atrousProg.phi, phi);
    bindTexture(1, m_traceSrc.tex);
    bindTexture(2, m_variance[m_historyIndex].tex);

    GLuint src = m_history[m_historyIndex].tex;
    int out = 0;
    for (int i = 0; i < passes; ++i) {
        glUniform1f(m_atrousProg.stride, static_cast<float>(1 << i));
        glUniform1f(m_atrousProg.wide, (passes >= 4 && i == passes - 1) ? 1.f : 0.f);
        bindTexture(0, src);
        drawInto(m_atrous[out]);
        src = m_atrous[out].tex;
        out = 1 - out;
    }
    m_giResultTex = src;
}

void RTXRenderer::runBloom(RTXConfig const& cfg) {
    int const levels = std::clamp(m_effBloom, 1, kBloomLevels);

    glUseProgram(m_bloom.id);
    glUniform1f(m_bloom.tonemap, static_cast<float>(cfg.tonemap));
    glUniform1f(m_bloom.hdrRange, m_hdr ? cfg.hdrRange : 1.f);
    glUniform1f(m_bloom.anamorphic, cfg.bloomAnamorphic);
    glUniform1f(m_bloom.radius, cfg.bloomRadius);

    glUniform1f(m_bloom.mode, 0.f);
    glUniform1f(m_bloom.threshold, cfg.bloomThreshold);
    glUniform1f(m_bloom.softKnee, cfg.bloomSoftKnee);
    glUniform1f(m_bloom.giMix, m_hasHistory ? 1.f : 0.f);
    glUniform3f(m_bloom.reprojRow0, m_reprojRow0[0], m_reprojRow0[1], m_reprojRow0[2]);
    glUniform3f(m_bloom.reprojRow1, m_reprojRow1[0], m_reprojRow1[1], m_reprojRow1[2]);
    glUniform2f(m_bloom.texel, 1.f / static_cast<float>(m_sceneW),
                               1.f / static_cast<float>(m_sceneH));
    bindTexture(0, m_sceneTex);
    bindTexture(1, m_giResultTex ? m_giResultTex : m_blackTex);
    bindTexture(2, m_hasHistory ? m_guideHistory.tex : m_blackTex);
    drawInto(m_bloomDown[0]);

    glUniform1f(m_bloom.mode, 1.f);
    for (int i = 1; i < levels; ++i) {
        glUniform2f(m_bloom.texel, 1.f / static_cast<float>(m_bloomDown[i - 1].w),
                                   1.f / static_cast<float>(m_bloomDown[i - 1].h));
        bindTexture(0, m_bloomDown[i - 1].tex);
        drawInto(m_bloomDown[i]);
    }

    glUniform1f(m_bloom.mode, 2.f);
    if (levels == 1) {
        glUniform1f(m_bloom.blend, 1.f);
        glUniform2f(m_bloom.texel, 1.f / static_cast<float>(m_bloomDown[0].w),
                                   1.f / static_cast<float>(m_bloomDown[0].h));
        bindTexture(0, m_bloomDown[0].tex);
        bindTexture(1, m_blackTex);
        drawInto(m_bloomUp[0]);
    } else {
        glUniform1f(m_bloom.blend, cfg.bloomBlend);
        for (int i = levels - 2; i >= 0; --i) {
            Target const& src = (i == levels - 2) ? m_bloomDown[levels - 1] : m_bloomUp[i + 1];
            glUniform2f(m_bloom.texel, 1.f / static_cast<float>(src.w),
                                       1.f / static_cast<float>(src.h));
            bindTexture(0, src.tex);
            bindTexture(1, m_bloomDown[i].tex);
            drawInto(m_bloomUp[i]);
        }
    }

    m_bloomResultTex = m_bloomUp[0].tex;

    if (cfg.godRayStrength > 0.001f) {
        Target const& src = m_bloomDown[std::min(levels - 1, 2)];
        glUniform1f(m_bloom.mode, 3.f);
        glUniform2f(m_bloom.lightPos, cfg.godRayX, cfg.godRayY);
        glUniform1f(m_bloom.decay, cfg.godRayDecay);
        glUniform1f(m_bloom.density, cfg.godRayDensity);
        glUniform2f(m_bloom.texel, 1.f / static_cast<float>(src.w),
                                   1.f / static_cast<float>(src.h));
        bindTexture(0, src.tex);
        bindTexture(1, m_blackTex);
        drawInto(m_rays);
    }
}

void RTXRenderer::runAutoExposure(RTXConfig const& cfg) {
    // high mip is already mean brightness; ping-pong adds inertia.
    bindTexture(0, 0);
    bindTexture(0, m_sceneTex);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

    float const dt = std::clamp(m_deltaSeconds, 0.f, 0.1f);
    float const rate = m_hasExposure ? 1.f - std::exp(-cfg.adaptSpeed * dt) : 1.f;

    int const dst = 1 - m_exposureIndex;
    glUseProgram(m_bloom.id);
    glUniform1f(m_bloom.mode, 4.f);
    glUniform1f(m_bloom.adaptRate, rate);
    bindTexture(1, m_exposure[m_exposureIndex].tex);
    drawInto(m_exposure[dst]);

    // back to flat filter or bloom would read mip 1 and come out soft.
    bindTexture(0, 0);
    bindTexture(0, m_sceneTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    m_exposureIndex = dst;
    m_hasExposure = true;
}

void RTXRenderer::runComposite(RTXConfig const& cfg, GLint const* viewport, GLuint prevFbo) {
    glUseProgram(m_composite.id);
    glUniform2f(m_composite.texel, 1.f / static_cast<float>(m_sceneW),
                                   1.f / static_cast<float>(m_sceneH));
    int const giW = m_giResultTex ? std::max(1, m_traceW) : std::max(1, m_sceneW);
    int const giH = m_giResultTex ? std::max(1, m_traceH) : std::max(1, m_sceneH);
    glUniform2f(m_composite.giTexel, 1.f / static_cast<float>(giW),
                                     1.f / static_cast<float>(giH));
    glUniform3f(m_composite.reprojRow0, m_reprojRow0[0], m_reprojRow0[1], m_reprojRow0[2]);
    glUniform3f(m_composite.reprojRow1, m_reprojRow1[0], m_reprojRow1[1], m_reprojRow1[2]);
    glUniform1f(m_composite.historyValid, m_hasHistory ? 1.f : 0.f);
    glUniform1f(m_composite.time, m_shaderTime);
    glUniform1f(m_composite.mixAmount, cfg.intensity);
    glUniform1f(m_composite.aoStrength, cfg.aoStrength);
    glUniform1f(m_composite.bloomStrength, cfg.bloomStrength);
    glUniform1f(m_composite.rayStrength, cfg.godRayStrength);
    glUniform1f(m_composite.tonemap, static_cast<float>(cfg.tonemap));
    glUniform1f(m_composite.exposure, cfg.exposure);
    bool const hasAuto = cfg.adaptEnabled && m_hdr && m_canGenerateMips && m_hasExposure;
    glUniform1f(m_composite.adaptKey, hasAuto ? cfg.adaptKey : 0.f);
    glUniform1f(m_composite.contrast, cfg.contrast);
    glUniform1f(m_composite.saturation, cfg.saturation);
    glUniform1f(m_composite.temperature, cfg.temperature);
    glUniform1f(m_composite.tint, cfg.tint);
    glUniform1f(m_composite.gammaV, cfg.gamma);
    glUniform1f(m_composite.ca, cfg.chromatic);
    glUniform1f(m_composite.vignette, cfg.vignette);
    glUniform1f(m_composite.grain, cfg.grain);
    glUniform1f(m_composite.sharpen, cfg.sharpen);

    bindTexture(0, m_sceneTex);
    bindTexture(1, m_giResultTex ? m_giResultTex : m_blackTex);
    bindTexture(2, m_bloomResultTex ? m_bloomResultTex : m_blackTex);
    bindTexture(3, (cfg.godRayStrength > 0.001f && m_rays.tex) ? m_rays.tex : m_blackTex);
    bindTexture(4, hasAuto ? m_exposure[m_exposureIndex].tex : m_blackTex);
    bindTexture(5, m_hasHistory ? m_guideHistory.tex : m_blackTex);

    glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void RTXRenderer::renderFrame() {
    if (paimon::isRuntimeShuttingDown() || m_broken) return;

    auto const& cfg = RTXManager::get().config();
    if (!RTXManager::get().shouldRender()) {
        m_wasActive = false;
        invalidateHistory();
        m_hasExposure = false;
        if (m_sceneTex && ++m_idleFrames > kIdleReleaseFrames) {
            releaseAll();
            m_idleFrames = 0;
        }
        return;
    }
    m_idleFrames = 0;

    FrameState const state;
    auto const* viewport = state.viewport;
    if (viewport[2] <= 0 || viewport[3] <= 0) return;

    std::array<GLint, 4> const sourceViewport{viewport[0], viewport[1], viewport[2], viewport[3]};
    if (sourceViewport != m_sourceViewport) {
        invalidateHistory();
        m_hasExposure = false;
        m_sourceViewport = sourceViewport;
    }

    if (!ensurePrograms()) {
        m_broken = true;
        log::warn("[PaimonRTX] no se pudieron preparar los shaders - RTX apagado esta sesion");
        return;
    }

    auto const now = std::chrono::steady_clock::now();
    if (m_wasActive) {
        float const ms = std::chrono::duration<float, std::milli>(now - m_lastFrame).count();
        m_deltaSeconds = ms * 0.001f;
        if (ms > 250.f) {
            invalidateHistory();
            m_hasExposure = false;
        }
        m_frameMs = m_frameMs > 0.f ? m_frameMs * 0.9f + ms * 0.1f : ms;
        m_shaderTime = std::fmod(m_shaderTime + std::min(m_deltaSeconds, 0.1f), 1024.f);
        updateAdaptiveScale(cfg);
    } else {
        syncGovernorEffectives(cfg);
        m_frameMs = 0.f;
        m_deltaSeconds = 0.f;
    }
    m_lastFrame = now;
    m_wasActive = true;

    while (glGetError() != GL_NO_ERROR) {}

    if (ensureFullTargets(viewport[2], viewport[3])
        && ensureTraceTargets(viewport[2], viewport[3], m_activeScale)) {

        // Target allocation binds offscreen FBOs; capture the presented frame explicitly.
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(state.fbo));
        bindTexture(0, m_sceneTex);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, viewport[0], viewport[1], m_sceneW, m_sceneH);
        if (glGetError() != GL_NO_ERROR) {
            m_broken = true;
            log::warn("[PaimonRTX] no se pudo capturar el framebuffer - RTX apagado esta sesion");
            releaseAll();
            return;
        }

        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glEnableVertexAttribArray(kCCVertexAttrib_Position);
        glDisableVertexAttribArray(kCCVertexAttrib_Color);
        glEnableVertexAttribArray(kCCVertexAttrib_TexCoords);
        glVertexAttribPointer(kCCVertexAttrib_Position, 2, GL_FLOAT, GL_FALSE,
                              4 * sizeof(GLfloat), reinterpret_cast<void*>(0));
        glVertexAttribPointer(kCCVertexAttrib_TexCoords, 2, GL_FLOAT, GL_FALSE,
                              4 * sizeof(GLfloat), reinterpret_cast<void*>(2 * sizeof(GLfloat)));

        bool const wantsTrace = cfg.giStrength > 0.001f || cfg.aoStrength > 0.001f
                             || cfg.reflectStrength > 0.001f;
        bool const wantsBloom = cfg.bloomStrength > 0.001f || cfg.godRayStrength > 0.001f;

        std::array<float, 16> const traceSettings{
            cfg.rayDistance, cfg.stepGrowth, cfg.lightThreshold, cfg.lightRange,
            cfg.bounceFalloff, cfg.giSaturation, cfg.normalStrength, cfg.thickness,
            cfg.aoRadius, cfg.aoPower, cfg.reflectStrength, cfg.reflectRoughness,
            cfg.reflectFresnel, cfg.reflectFade, static_cast<float>(cfg.tonemap), cfg.giStrength
        };
        if (traceSettings != m_traceSettings) {
            invalidateHistory();
            m_traceSettings = traceSettings;
        }
        updateReprojection();

        if (cfg.adaptEnabled && m_hdr && m_canGenerateMips) runAutoExposure(cfg);
        else m_hasExposure = false;

        unsigned const cadence = static_cast<unsigned>(std::max(1, m_effSkip + 1));
        if (wantsTrace) {
            if (!m_hasHistory || m_frameCounter % cadence == 0) runTrace(cfg);
        } else {
            invalidateHistory();
        }

        if (wantsBloom) {
            runBloom(cfg);
        } else {
            m_bloomResultTex = 0;
        }

        GLenum const passError = glGetError();
        if (passError != GL_NO_ERROR) {
            m_broken = true;
            log::warn("[PaimonRTX] error GL 0x{:X} en los pases de luz - RTX apagado esta sesion",
                      static_cast<unsigned>(passError));
            releaseAll();
            return;
        }
        runComposite(cfg, viewport, static_cast<GLuint>(state.fbo));

        GLenum const err = glGetError();
        if (err != GL_NO_ERROR) {
            m_broken = true;
            log::warn("[PaimonRTX] error GL 0x{:X} en el postproceso - RTX apagado esta sesion",
                      static_cast<unsigned>(err));
            releaseAll();
        }
    } else {
        m_broken = true;
        log::warn("[PaimonRTX] no se pudieron crear los render targets - RTX apagado esta sesion");
        releaseAll();
    }

    m_frameCounter++;
}

} // namespace paimon::rtx
