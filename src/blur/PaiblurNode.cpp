#include "PaiblurNode.hpp"
#include "../utils/FramePerf.hpp"

#include <Geode/Geode.hpp>
#include <Geode/cocos/CCDirector.h>
#include <Geode/cocos/shaders/ccGLStateCache.h>
#include <Geode/cocos/actions/CCActionInstant.h>
#include <Geode/cocos/actions/CCActionInterval.h>

#include "../core/RuntimeLifecycle.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_set>
#include <vector>

#ifdef GEODE_IS_ANDROID
#include <EGL/egl.h>
#endif

#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif

// resolve glblitframebuffer at runtime on android gles3 / extension; disable blur if absent.
#ifndef GL_APIENTRY
#define GL_APIENTRY
#endif
namespace {
using PaimonBlitFramebufferFn = void (GL_APIENTRY*)(
    GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);

#if defined(GEODE_IS_ANDROID)
std::atomic<bool> g_blitProbed{false};
PaimonBlitFramebufferFn g_blitFn = nullptr;

bool hasBlitFramebufferSupport() {
    auto const* ver = reinterpret_cast<char const*>(glGetString(GL_VERSION));
    if (ver) {
        // Parse "OpenGL ES X.Y" major version >= 3
        if (auto const* es = std::strstr(ver, "OpenGL ES ")) {
            char const* digits = es + 10;
            if (*digits >= '3' && *digits <= '9') return true;
        }
    }
    auto const* exts = reinterpret_cast<char const*>(glGetString(GL_EXTENSIONS));
    if (exts) {
        if (std::strstr(exts, "GL_NV_framebuffer_blit") || std::strstr(exts, "GL_ANGLE_framebuffer_blit")) {
            return true;
        }
    }
    return false;
}

void resetBlitProbe() {
    g_blitFn = nullptr;
    g_blitProbed.store(false, std::memory_order_release);
}
#endif

inline bool paimonBlitFramebuffer(
    GLint sx0, GLint sy0, GLint sx1, GLint sy1,
    GLint dx0, GLint dy0, GLint dx1, GLint dy1,
    GLbitfield mask, GLenum filter
) {
#if defined(GEODE_IS_ANDROID)
    if (!g_blitProbed.load(std::memory_order_acquire)) {
        PaimonBlitFramebufferFn fn = nullptr;
        if (hasBlitFramebufferSupport()) {
            if (auto p = eglGetProcAddress("glBlitFramebuffer"))
                fn = reinterpret_cast<PaimonBlitFramebufferFn>(p);
            else if (auto p = eglGetProcAddress("glBlitFramebufferNV"))
                fn = reinterpret_cast<PaimonBlitFramebufferFn>(p);
            else if (auto p = eglGetProcAddress("glBlitFramebufferANGLE"))
                fn = reinterpret_cast<PaimonBlitFramebufferFn>(p);
        }
        g_blitFn = fn;
        g_blitProbed.store(true, std::memory_order_release);
    }
    if (!g_blitFn) return false;
    g_blitFn(sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, mask, filter);
    return true;
#elif defined(GEODE_IS_IOS)
// no blit symbol or loader on ios
    (void)sx0; (void)sy0; (void)sx1; (void)sy1;
    (void)dx0; (void)dy0; (void)dx1; (void)dy1; (void)mask; (void)filter;
    return false;
#else
    glBlitFramebuffer(sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, mask, filter);
    return true;
#endif
}

inline void drainGlErrors() {
    for (int i = 0; i < 16 && glGetError() != GL_NO_ERROR; ++i) {}
}
}

using namespace cocos2d;
using namespace geode::prelude;

namespace paimon::paiblur {

namespace {

// half-resolution blur fbo, capped for 4k performance.
constexpr int kMaxBlurLongEdge = 1280;
// refresh a steady backdrop periodically instead of re-blurring every frame.
constexpr int kSteadyRefreshInterval = 2;
float intensityToEclipseRadius(float intensity) {
    float t = std::clamp((intensity - 0.5f) / 9.5f, 0.0f, 1.0f);
    float curved = t * t * (3.0f - 2.0f * t);
    return 0.025f + curved * 0.155f; // 0.025 .. 0.180
}

constexpr char kVertSrc[] = R"(attribute vec2 aPosition;
attribute vec2 aTexCoord;
varying vec2 v_texCoord;
void main() {
    gl_Position = vec4(aPosition, 0.0, 1.0);
    v_texCoord = aTexCoord;
}
)";

// eclipsemenu "fast" blur kernel; darkness baked into u_colormul.
constexpr char kFragSrc[] = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec2 v_texCoord;
uniform sampler2D u_screen;
uniform vec2 u_texSize;
uniform vec2 u_direction;
uniform float u_radius;
uniform vec3 u_colorMul;
uniform float u_alpha;

void main() {
    float r10 = u_radius * 10.0;
    float scaledRadius = (u_radius * u_texSize.y * 0.5) / (r10 + 2.0);
    vec2 texelStep = u_direction / u_texSize;

    vec3 result = texture2D(u_screen, v_texCoord).rgb;
    float weightSum = 1.0;
    float weight = 1.0;

    for (int i = 1; i < 64; i++) {
        if (float(i) >= scaledRadius) break;
        weight -= 1.0 / scaledRadius;
        if (weight <= 0.0) break;
        vec2 offset = texelStep * float(i);
        result += texture2D(u_screen, v_texCoord + offset).rgb * weight;
        result += texture2D(u_screen, v_texCoord - offset).rgb * weight;
        weightSum += 2.0 * weight;
    }

    gl_FragColor = vec4((result / weightSum) * u_colorMul, u_alpha);
}
)";

struct BlurProgram {
    GLuint prog = 0;
    GLint locTexSize  = -1;
    GLint locDir      = -1;
    GLint locRadius   = -1;
    GLint locColorMul = -1;
    GLint locAlpha    = -1;
};

BlurProgram& sharedProgram() {
    static BlurProgram p;
    return p;
}

GLuint compilePart(GLenum type, char const* src) {
    GLuint id = glCreateShader(type);
    if (!id) return 0;
    glShaderSource(id, 1, &src, nullptr);
    glCompileShader(id);
    GLint ok = 0;
    glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char infoLog[512] = {};
        glGetShaderInfoLog(id, sizeof(infoLog) - 1, nullptr, infoLog);
        log::warn("[Paiblur] shader compile failed ({}): {}",
                  type == GL_VERTEX_SHADER ? "vertex" : "fragment", infoLog);
        glDeleteShader(id);
        return 0;
    }
    return id;
}

bool ensureProgram() {
    auto& p = sharedProgram();
    if (p.prog && glIsProgram(p.prog) == GL_TRUE) return true;
    p = BlurProgram{};

    GLuint vs = compilePart(GL_VERTEX_SHADER, kVertSrc);
    if (!vs) return false;
    GLuint fs = compilePart(GL_FRAGMENT_SHADER, kFragSrc);
    if (!fs) {
        glDeleteShader(vs);
        return false;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
// use cocos attribute slots so its gl state cache stays coherent.
    glBindAttribLocation(prog, kCCVertexAttrib_Position, "aPosition");
    glBindAttribLocation(prog, kCCVertexAttrib_TexCoords, "aTexCoord");
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linked = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) {
        char infoLog[512] = {};
        glGetProgramInfoLog(prog, sizeof(infoLog) - 1, nullptr, infoLog);
        log::warn("[Paiblur] shader link failed: {}", infoLog);
        glDeleteProgram(prog);
        return false;
    }

    p.prog       = prog;
    p.locTexSize  = glGetUniformLocation(prog, "u_texSize");
    p.locDir      = glGetUniformLocation(prog, "u_direction");
    p.locRadius   = glGetUniformLocation(prog, "u_radius");
    p.locColorMul = glGetUniformLocation(prog, "u_colorMul");
    p.locAlpha    = glGetUniformLocation(prog, "u_alpha");

    ccGLUseProgram(prog);
    glUniform1i(glGetUniformLocation(prog, "u_screen"), 0);
    return true;
}

// live instances surviving across a gl context reload (held by blurRegistry).
// guarded by mutex; insert in ctor, erase in dtor (no gl calls there).
std::mutex& liveMutex() {
    static std::mutex m;
    return m;
}

std::unordered_set<PaiblurNode*>& liveSet() {
    static std::unordered_set<PaiblurNode*> s;
    return s;
}

constexpr GLfloat kBlurQuad[] = {
    -1.f,  1.f,  0.f, 1.f,
    -1.f, -1.f,  0.f, 0.f,
     1.f, -1.f,  1.f, 0.f,

    -1.f,  1.f,  0.f, 1.f,
     1.f, -1.f,  1.f, 0.f,
     1.f,  1.f,  1.f, 1.f,
};

bool createQuadVBO(GLuint& vbo) {
    vbo = 0;
    glGenBuffers(1, &vbo);
    if (!vbo) return false;
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kBlurQuad), kBlurQuad, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
}

}

PaiblurNode::PaiblurNode() {
    std::lock_guard<std::mutex> lock(liveMutex());
    liveSet().insert(this);
}

PaiblurNode* PaiblurNode::create(CCSize const& winSize, float intensity, float darkness) {
    auto* node = new PaiblurNode();
    if (node && node->initWithWinSize(winSize, intensity, darkness)) {
        node->autorelease();
        return node;
    }
    CC_SAFE_DELETE(node);
    return nullptr;
}

bool PaiblurNode::initWithWinSize(CCSize const& winSize, float intensity, float darkness) {
    if (winSize.width <= 0.f || winSize.height <= 0.f) return false;
    if (!CCNodeRGBA::init()) return false;

    m_intensity = std::clamp(intensity, 0.1f, 10.0f);
    m_darkness = std::clamp(darkness, 0.0f, 1.0f);

// failure falls back to static blur.
    if (!ensureProgram()) {
        log::warn("[Paiblur] blur shader unavailable - Paiblur disabled");
        return false;
    }

    if (!createQuadVBO(m_vbo)) return false;

    // allocate on the first visible visit using the actual pixel viewport.
    // frame size can differ under dpi scaling/letterboxing and used to allocate twice.

    setID("paimon-paiblur-node"_spr);
    setContentSize(winSize);
    setOpacity(0);
    return true;
}

PaiblurNode::~PaiblurNode() {
    {
        std::lock_guard<std::mutex> lock(liveMutex());
        liveSet().erase(this);
    }
// during shutdown the gl context may already be gone; avoid deleting gl objects.
    if (paimon::isRuntimeShuttingDown()) return;
    releaseRenderTargets();
    if (m_vbo) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
}

void PaiblurNode::onGLContextReload(bool deleteHandles) {
    releaseRenderTargets(deleteHandles);
    if (m_vbo) {
        if (deleteHandles) glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    sharedProgram() = BlurProgram{};
#if defined(GEODE_IS_ANDROID)
    resetBlitProbe();
#endif
    m_broken = false;
}

void PaiblurNode::onGLContextReloadAll(bool deleteHandles) {
    std::vector<PaiblurNode*> live;
    {
        std::lock_guard<std::mutex> lock(liveMutex());
        live.reserve(liveSet().size());
        for (auto* node : liveSet()) {
            if (node) live.push_back(node);
        }
    }
    for (auto* node : live) node->onGLContextReload(deleteHandles);
}

bool PaiblurNode::ensureRenderTargets(int srcW, int srcH) {
    if (srcW <= 0 || srcH <= 0) return false;
    if (m_fboA && srcW == m_lastSrcW && srcH == m_lastSrcH) {
        if (glIsFramebuffer(m_fboA) == GL_TRUE && glIsFramebuffer(m_fboB) == GL_TRUE) return true;
        // handles died with a previous gl context; fall through and rebuild.
    }

    int bw = std::max(2, srcW / 2);
    int bh = std::max(2, srcH / 2);
    int longEdge = std::max(bw, bh);
    if (longEdge > kMaxBlurLongEdge) {
        float s = static_cast<float>(kMaxBlurLongEdge) / static_cast<float>(longEdge);
        bw = std::max(2, static_cast<int>(std::lround(bw * s)));
        bh = std::max(2, static_cast<int>(std::lround(bh * s)));
    }

    releaseRenderTargets();

    GLint prevFbo = 0;
    GLint prevTex = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);

    auto makeRT = [&](GLuint& fbo, GLuint& tex) -> bool {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, bw, bh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    };

    bool ok = makeRT(m_fboA, m_texA) && makeRT(m_fboB, m_texB);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex));

    if (!ok) {
        log::warn("[Paiblur] blur FBO incomplete ({}x{})", bw, bh);
        releaseRenderTargets(true);
        return false;
    }

    m_blurW = bw;
    m_blurH = bh;
    m_lastSrcW = srcW;
    m_lastSrcH = srcH;
    log::debug("[Paiblur] render targets {}x{} (source {}x{})", bw, bh, srcW, srcH);
    return true;
}

void PaiblurNode::releaseRenderTargets(bool deleteHandles) {
    if (deleteHandles) {
        if (m_fboA) glDeleteFramebuffers(1, &m_fboA);
        if (m_fboB) glDeleteFramebuffers(1, &m_fboB);
        if (m_texA) glDeleteTextures(1, &m_texA);
        if (m_texB) glDeleteTextures(1, &m_texB);
    }
    m_fboA = m_fboB = m_texA = m_texB = 0;
    m_blurW = m_blurH = 0;
    m_lastSrcW = m_lastSrcH = 0;
    m_hasCachedBlur = false;
    m_lastRadius = -1.f;
    m_steadyFrames = 0;
}

void PaiblurNode::fadeIn(float duration) {
    stopAllActions();
    if (duration <= 0.01f) {
        setOpacity(255);
        return;
    }
    runAction(CCFadeTo::create(duration, 255));
}

void PaiblurNode::visit() {
    if (m_broken || !isVisible()) return;
#if defined(GEODE_IS_ANDROID)
    if (void const* curCtx = eglGetCurrentContext()) {
        if (m_contextToken && m_contextToken != curCtx) {
            onGLContextReload(false);
        }
        m_contextToken = curCtx;
    }
#endif
    paimon::frameperf::Scope perf(paimon::frameperf::Stage::PopupBlur);

    GLubyte op = getDisplayedOpacity();
    if (op == 0) return;

    float progress = static_cast<float>(op) / 255.f;
    float eased = 0.5f * (1.f - std::cos(3.14159265f * progress));
    if (eased < 0.01f) return;

// viewport is the letterboxed window in real pixels.
    GLint vp[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, vp);
    int srcW = vp[2];
    int srcH = vp[3];
    if (srcW <= 0 || srcH <= 0) return;

    if (!m_vbo || glIsBuffer(m_vbo) != GL_TRUE) {
        if (!createQuadVBO(m_vbo)) {
            m_broken = true;
            return;
        }
    }

    if (!ensureRenderTargets(srcW, srcH)) {
        m_broken = true;
        return;
    }

// the program handle dies with the gl context; rebuild it lazily if needed.
    if (!ensureProgram()) return;
    auto& prg = sharedProgram();
    if (!prg.prog) return;

    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

    float radius = intensityToEclipseRadius(m_intensity) * eased;
    bool steady = (op >= 254);

// cache both blur axes. Only the inexpensive composite runs at full resolution.
    bool needFull = !m_hasCachedBlur || !steady || std::abs(radius - m_lastRadius) > 0.001f;
    if (!needFull) {
        if (++m_steadyFrames >= kSteadyRefreshInterval) {
            m_steadyFrames = 0;
            needFull = true;
        }
    } else {
        m_steadyFrames = 0;
    }

    drainGlErrors();

    if (needFull) {
#if defined(GEODE_IS_ANDROID)
        if (!hasBlitFramebufferSupport()) {
            m_broken = true;
            log::warn("[Paiblur] glBlitFramebuffer unsupported on this GLES context - Paiblur disabled");
            return;
        }
#endif
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_fboA);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
        if (!paimonBlitFramebuffer(vp[0], vp[1], vp[0] + srcW, vp[1] + srcH,
                                   0, 0, m_blurW, m_blurH,
                                   GL_COLOR_BUFFER_BIT, GL_LINEAR)) {
            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
            m_broken = true;
            log::warn("[Paiblur] glBlitFramebuffer unavailable - Paiblur disabled for this popup");
            return;
        }
        GLenum blitErr = glGetError();
        if (blitErr != GL_NO_ERROR) {
            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
            m_broken = true;
            log::warn("[Paiblur] glBlitFramebuffer failed (err=0x{:X}) - Paiblur disabled for this popup",
                      static_cast<unsigned>(blitErr));
            return;
        }
    }

    ccGLUseProgram(prg.prog);
#if CC_TEXTURE_ATLAS_USE_VAO
    ccGLBindVAO(0);
#endif
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    ccGLEnableVertexAttribs(kCCVertexAttribFlag_Position | kCCVertexAttribFlag_TexCoords);
    glVertexAttribPointer(kCCVertexAttrib_Position, 2, GL_FLOAT, GL_FALSE,
                          4 * sizeof(GLfloat), reinterpret_cast<void*>(0));
    glVertexAttribPointer(kCCVertexAttrib_TexCoords, 2, GL_FLOAT, GL_FALSE,
                          4 * sizeof(GLfloat), reinterpret_cast<void*>(2 * sizeof(GLfloat)));

    glUniform2f(prg.locTexSize, static_cast<float>(m_blurW), static_cast<float>(m_blurH));
    glUniform1f(prg.locRadius, radius);

    if (needFull) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_fboB);
        glViewport(0, 0, m_blurW, m_blurH);
        glUniform2f(prg.locDir, 1.f, 0.f);
        glUniform3f(prg.locColorMul, 1.f, 1.f, 1.f);
        glUniform1f(prg.locAlpha, 1.f);
        ccGLBindTexture2DN(0, m_texA);
        ccGLBlendFunc(GL_ONE, GL_ZERO);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        glBindFramebuffer(GL_FRAMEBUFFER, m_fboA);
        glUniform2f(prg.locDir, 0.f, 1.f);
        ccGLBindTexture2DN(0, m_texB);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        m_hasCachedBlur = true;
        m_lastRadius = radius;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    float dark = 1.f - m_darkness * eased;
    // radius zero exits the kernel before any extra samples. keep darkness and
    // opacity here so the fade stays smooth even when the cached blur is reused.
    glUniform1f(prg.locRadius, 0.f);
    glUniform2f(prg.locDir, 0.f, 0.f);
    glUniform3f(prg.locColorMul, dark, dark, dark);
    if (op >= 254) {
        glUniform1f(prg.locAlpha, 1.f);
        ccGLBlendFunc(GL_ONE, GL_ZERO);
    } else {
        glUniform1f(prg.locAlpha, eased);
        ccGLBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    ccGLBindTexture2DN(0, m_texA);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

}
