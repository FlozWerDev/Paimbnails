#include "PaiblurNode.hpp"

#include <Geode/Geode.hpp>
#include <Geode/cocos/CCDirector.h>
#include <Geode/cocos/shaders/ccGLStateCache.h>
#include <Geode/cocos/actions/CCActionInstant.h>
#include <Geode/cocos/actions/CCActionInterval.h>

#include "../core/RuntimeLifecycle.hpp"

#include <algorithm>
#include <cmath>

#ifdef GEODE_IS_ANDROID
#include <EGL/egl.h>
#endif

#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif

// glBlitFramebuffer is core on desktop GL and GLES3, but on Android's GLES2 it
// only exists as the GL_NV_framebuffer_blit extension (glBlitFramebufferNV).
// Resolve it at runtime there; if unavailable the caller disables the blur.
#ifndef GL_APIENTRY
#define GL_APIENTRY
#endif
namespace {
using PaimonBlitFramebufferFn = void (GL_APIENTRY*)(
    GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);

inline bool paimonBlitFramebuffer(
    GLint sx0, GLint sy0, GLint sx1, GLint sy1,
    GLint dx0, GLint dy0, GLint dx1, GLint dy1,
    GLbitfield mask, GLenum filter
) {
#if defined(GEODE_IS_ANDROID)
    static PaimonBlitFramebufferFn fn = []() -> PaimonBlitFramebufferFn {
        if (auto p = eglGetProcAddress("glBlitFramebuffer"))
            return reinterpret_cast<PaimonBlitFramebufferFn>(p);
        if (auto p = eglGetProcAddress("glBlitFramebufferNV"))
            return reinterpret_cast<PaimonBlitFramebufferFn>(p);
        return nullptr;
    }();
    if (!fn) return false;
    fn(sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, mask, filter);
    return true;
#elif defined(GEODE_IS_IOS)
    // iOS ships only the GLES2 headers (no glBlitFramebuffer, no runtime proc
    // loader like eglGetProcAddress), so the blit-based blur is unavailable.
    (void)sx0; (void)sy0; (void)sx1; (void)sy1;
    (void)dx0; (void)dy0; (void)dx1; (void)dy1; (void)mask; (void)filter;
    return false;
#else
    glBlitFramebuffer(sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, mask, filter);
    return true;
#endif
}
}

using namespace cocos2d;
using namespace geode::prelude;

namespace paimon::paiblur {

namespace {

// Half-res blur FBO, capped to keep 4K performant.
constexpr int kMaxBlurLongEdge = 1280;
// Once steady at full opacity, refresh the frozen backdrop every Nth frame
// instead of re-blurring every frame (saves the blit + horizontal pass).
constexpr int kSteadyRefreshInterval = 2;
// Smoothstep intensity (0.1..10) to normalized blur radius.
float intensityToEclipseRadius(float intensity) {
    float t = std::clamp((intensity - 0.5f) / 9.5f, 0.0f, 1.0f);
    float curved = t * t * (3.0f - 2.0f * t);
    return 0.025f + curved * 0.155f; // 0.025 .. 0.180
}

// Embedded shaders (no file I/O).
constexpr char kVertSrc[] = R"(attribute vec2 aPosition;
attribute vec2 aTexCoord;
varying vec2 v_texCoord;
void main() {
    gl_Position = vec4(aPosition, 0.0, 1.0);
    v_texCoord = aTexCoord;
}
)";

// EclipseMenu "fast" blur kernel; darkness baked into u_colorMul.
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

// Shared blur program; uniform locations cached at link time.
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
    // Use cocos's standard attribute slots so ccGLEnableVertexAttribs' state cache stays coherent.
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

} // namespace

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

    // Failure here means falling back to static blur.
    if (!ensureProgram()) {
        log::warn("[Paiblur] blur shader unavailable — Paiblur disabled");
        return false;
    }

    // Fullscreen NDC quad.
    static constexpr GLfloat kQuad[] = {
        -1.f,  1.f,  0.f, 1.f,
        -1.f, -1.f,  0.f, 0.f,
         1.f, -1.f,  1.f, 0.f,

        -1.f,  1.f,  0.f, 1.f,
         1.f, -1.f,  1.f, 0.f,
         1.f,  1.f,  1.f, 1.f,
    };
    glGenBuffers(1, &m_vbo);
    if (!m_vbo) return false;
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuad), kQuad, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // Eagerly create blur FBOs; defer via viewport if GLView reports nothing.
    auto* director = CCDirector::get();
    auto* glView = director ? director->getOpenGLView() : nullptr;
    CCSize frame = glView ? glView->getFrameSize() : CCSizeZero;
    int fw = static_cast<int>(frame.width);
    int fh = static_cast<int>(frame.height);
    if (fw > 0 && fh > 0 && !ensureRenderTargets(fw, fh)) {
        log::warn("[Paiblur] blur FBO creation failed — Paiblur disabled");
        return false;
    }

    setID("paimon-paiblur-node"_spr);
    setContentSize(winSize);
    setOpacity(0);
    return true;
}

PaiblurNode::~PaiblurNode() {
    // During shutdown the GL context may already be gone; deleting GL objects
    // then is UB on some drivers.
    if (paimon::isRuntimeShuttingDown()) return;
    releaseRenderTargets();
    if (m_vbo) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
}

bool PaiblurNode::ensureRenderTargets(int srcW, int srcH) {
    if (srcW <= 0 || srcH <= 0) return false;
    if (m_fboA && srcW == m_lastSrcW && srcH == m_lastSrcH) return true;

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
        releaseRenderTargets();
        return false;
    }

    m_blurW = bw;
    m_blurH = bh;
    m_lastSrcW = srcW;
    m_lastSrcH = srcH;
    log::debug("[Paiblur] render targets {}x{} (source {}x{})", bw, bh, srcW, srcH);
    return true;
}

void PaiblurNode::releaseRenderTargets() {
    if (m_fboA) glDeleteFramebuffers(1, &m_fboA);
    if (m_fboB) glDeleteFramebuffers(1, &m_fboB);
    if (m_texA) glDeleteTextures(1, &m_texA);
    if (m_texB) glDeleteTextures(1, &m_texB);
    m_fboA = m_fboB = m_texA = m_texB = 0;
    m_blurW = m_blurH = 0;
    m_lastSrcW = m_lastSrcH = 0;
    m_hasCachedBlur = false;
    m_lastRadius = -1.f;
    m_steadyFrames = 0;
}

void PaiblurNode::setBlurIntensity(float intensity) {
    m_intensity = std::clamp(intensity, 0.1f, 10.0f);
}

void PaiblurNode::setDarkness(float darkness) {
    m_darkness = std::clamp(darkness, 0.0f, 1.0f);
}

void PaiblurNode::fadeIn(float duration) {
    stopAllActions();
    if (duration <= 0.01f) {
        setOpacity(255);
        return;
    }
    runAction(CCFadeTo::create(duration, 255));
}

void PaiblurNode::fadeOutAndRemove(float duration) {
    stopAllActions();
    if (duration <= 0.01f) {
        if (getParent()) removeFromParent();
        return;
    }
    runAction(CCSequence::create(
        CCFadeTo::create(duration, 0),
        CCCallFunc::create(this, callfunc_selector(CCNode::removeFromParent)),
        nullptr
    ));
}

void PaiblurNode::visit() {
    if (m_broken || !isVisible()) return;

    GLubyte op = getDisplayedOpacity();
    if (op == 0) return;

    // Cosine-eased blur radius ramp from opacity.
    float progress = static_cast<float>(op) / 255.f;
    float eased = 0.5f * (1.f - std::cos(3.14159265f * progress));
    if (eased < 0.01f) return;

    // Viewport is the letterboxed window in real pixels.
    GLint vp[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, vp);
    int srcW = vp[2];
    int srcH = vp[3];
    if (srcW <= 0 || srcH <= 0) return;

    if (!ensureRenderTargets(srcW, srcH)) {
        m_broken = true;
        return;
    }

    auto& prg = sharedProgram();
    if (!prg.prog) return;

    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

    float radius = intensityToEclipseRadius(m_intensity) * eased;
    bool steady = (op >= 254);

    // Re-run the capture + horizontal pass only when the result would actually
    // change (fading, radius moved, no cache yet) or on the periodic backdrop
    // refresh. Otherwise reuse the cached m_texB and just re-composite it.
    bool needFull = !m_hasCachedBlur || !steady || std::abs(radius - m_lastRadius) > 0.001f;
    if (!needFull) {
        if (++m_steadyFrames >= kSteadyRefreshInterval) {
            m_steadyFrames = 0;
            needFull = true;
        }
    } else {
        m_steadyFrames = 0;
    }

    // Clear stale GL errors before blit.
    while (glGetError() != GL_NO_ERROR) {}

    // 1) Capture scene into rtA (downsampled via GL_LINEAR).
    if (needFull) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_fboA);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
        if (!paimonBlitFramebuffer(vp[0], vp[1], vp[0] + srcW, vp[1] + srcH,
                                   0, 0, m_blurW, m_blurH,
                                   GL_COLOR_BUFFER_BIT, GL_LINEAR)) {
            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
            m_broken = true;
            log::warn("[Paiblur] glBlitFramebuffer unavailable — Paiblur disabled for this popup");
            return;
        }
        GLenum blitErr = glGetError();
        if (blitErr != GL_NO_ERROR) {
            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
            m_broken = true;
            log::warn("[Paiblur] glBlitFramebuffer failed (err=0x{:X}) — Paiblur disabled for this popup",
                      static_cast<unsigned>(blitErr));
            return;
        }
    }

    // Shared draw state for both passes.
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

    // 2) Horizontal blur: rtA → rtB.
    if (needFull) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_fboB);
        glViewport(0, 0, m_blurW, m_blurH);
        glUniform2f(prg.locDir, 1.f, 0.f);
        glUniform3f(prg.locColorMul, 1.f, 1.f, 1.f);
        glUniform1f(prg.locAlpha, 1.f);
        ccGLBindTexture2DN(0, m_texA);
        ccGLBlendFunc(GL_ONE, GL_ZERO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        m_hasCachedBlur = true;
        m_lastRadius = radius;
    }

    // 3) Vertical blur + darkness: rtB → framebuffer.
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    float dark = 1.f - m_darkness * eased;
    glUniform2f(prg.locDir, 0.f, 1.f);
    glUniform3f(prg.locColorMul, dark, dark, dark);
    if (op >= 254) {
        glUniform1f(prg.locAlpha, 1.f);
        ccGLBlendFunc(GL_ONE, GL_ZERO);
    } else {
        glUniform1f(prg.locAlpha, eased);
        ccGLBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    ccGLBindTexture2DN(0, m_texB);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

} // namespace paimon::paiblur
