#include "AmbientAnalyzer.hpp"
#include "../../../utils/GLSLLoader.hpp"

#include <Geode/cocos/shaders/ccGLStateCache.h>
#include <algorithm>
#include <chrono>
#include <cstring>

#if defined(GEODE_IS_WINDOWS)
#include <Windows.h>
#elif defined(GEODE_IS_ANDROID)
#include <EGL/egl.h>
#endif

#ifndef GL_SAMPLE_BUFFERS
#define GL_SAMPLE_BUFFERS 0x80A8
#endif
#ifndef GL_PIXEL_PACK_BUFFER
#define GL_PIXEL_PACK_BUFFER 0x88EB
#endif
#ifndef GL_STREAM_READ
#define GL_STREAM_READ 0x88E1
#endif
#ifndef GL_READ_ONLY
#define GL_READ_ONLY 0x88B8
#endif
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif

using namespace geode::prelude;

namespace paimon::ambient {
namespace {

void const* currentContextToken() {
#if defined(GEODE_IS_WINDOWS)
    return wglGetCurrentContext();
#elif defined(GEODE_IS_ANDROID)
    return eglGetCurrentContext();
#else
    return nullptr;
#endif
}

void bindUnit0(GLuint tex) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    ccGLBindTexture2DN(0, tex);
}

void drainGlErrorsBounded() {
    for (int i = 0; i < 16 && glGetError() != GL_NO_ERROR; ++i) {}
}

constexpr double kSampleIntervalSeconds = 0.08;
constexpr std::size_t kMaxCachedTexturePalettes = 64;

CCScene const* findOwningNonTransitionScene(CCNode const* node) {
    for (CCNode* cur = const_cast<CCNode*>(node); cur; cur = cur->getParent()) {
        auto* sc = typeinfo_cast<CCScene const*>(cur);
        if (sc && !typeinfo_cast<CCTransitionScene const*>(sc)) {
            return sc;
        }
    }
    return nullptr;
}

} // namespace

AmbientAnalyzer& AmbientAnalyzer::get() {
    static AmbientAnalyzer instance;
    return instance;
}

double AmbientAnalyzer::nowSeconds() const {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

AmbientAnalyzer::SceneAmbientEntry& AmbientAnalyzer::ensureSceneEntry(CCScene const* scene) {
    auto it = m_scenes.find(scene);
    if (it != m_scenes.end()) return it->second;
    SceneAmbientEntry entry;
    entry.generation = ++m_nextGeneration;
    return m_scenes.emplace(scene, std::move(entry)).first->second;
}

void AmbientAnalyzer::pruneStaleScenes() {
    auto* director = CCDirector::get();
    if (!director || m_scenes.empty()) return;

    auto isLiveScene = [&](CCScene const* target) -> bool {
        if (!target) return false;
        if (target == m_activeScene) return true;
        auto* running = director->getRunningScene();
        if (target == running) return true;
        if (auto* trans = typeinfo_cast<CCTransitionScene*>(running)) {
            if (target == trans->m_pInScene || target == trans->m_pOutScene) return true;
        }
        auto* next = director->getNextScene();
        if (target == next) return true;
        if (auto* trans = typeinfo_cast<CCTransitionScene*>(next)) {
            if (target == trans->m_pInScene || target == trans->m_pOutScene) return true;
        }
        if (auto* stack = director->m_pobScenesStack) {
            for (auto* item : CCArrayExt<CCNode*>(stack)) {
                if (item == target) return true;
            }
        }
        return false;
    };

    std::erase_if(m_scenes, [&](auto const& pair) {
        return !isLiveScene(pair.first);
    });
    if (m_pboPending && !isLiveScene(m_pboScene)) {
        m_pboPending = false;
        m_pboScene = nullptr;
    }
    if (m_pendingLayerSeeds.size() > 32) {
        m_pendingLayerSeeds.clear();
    }
}

void AmbientAnalyzer::onSceneChanged(CCScene* scene) {
    if (!scene || typeinfo_cast<CCTransitionScene*>(scene) != nullptr) return;
    if (scene != m_activeScene) {
        m_activeScene = scene;
        m_validationFailures = 0;
        m_broken = false;
        pruneStaleScenes();
        ensureSceneEntry(scene);
        bindSceneSeeds(scene);
    }
}

void AmbientAnalyzer::requestImmediateSample(CCScene const* scene) {
    CCScene const* target = scene ? scene : m_activeScene;
    if (!target) return;
    auto& entry = ensureSceneEntry(target);
    entry.lastSampleTime = -1.0;
    entry.lastSampledFrame = 0;
    entry.consecutiveDarkSamples = 0;
    entry.target.fromCapture = false;
    entry.smoothed.fromCapture = false;
}

void AmbientAnalyzer::advanceFrame(float dt) {
    float const safeDt = std::clamp(dt, 0.f, 0.05f);
    m_time = std::fmod(m_time + safeDt, 628.3185f);

    auto* director = CCDirector::get();
    auto* running = director ? director->getRunningScene() : nullptr;
    auto* trans = running ? typeinfo_cast<CCTransitionScene*>(running) : nullptr;
    CCScene const* frozenOut = trans ? trans->m_pOutScene : nullptr;

    for (auto& [scenePtr, entry] : m_scenes) {
        if (scenePtr == frozenOut) continue;
        entry.smoothed = smoothAmbientState(entry.smoothed, entry.target, dt);
    }
}

bool AmbientAnalyzer::hasSceneCapture(CCScene const* scene) const {
    CCScene const* target = scene ? scene : m_activeScene;
    if (!target) return false;
    auto it = m_scenes.find(target);
    return it != m_scenes.end() && it->second.target.fromCapture;
}

AmbientLightState const& AmbientAnalyzer::stateForScene(CCScene const* scene) const {
    if (scene) {
        auto it = m_scenes.find(scene);
        if (it != m_scenes.end() && it->second.smoothed.fromCapture) {
            return it->second.smoothed;
        }
    }
    return currentState();
}

AmbientLightState const& AmbientAnalyzer::currentState() const {
    if (m_activeScene) {
        auto it = m_scenes.find(m_activeScene);
        if (it != m_scenes.end() && it->second.smoothed.fromCapture) {
            return it->second.smoothed;
        }
    }
    return m_defaultState;
}

AmbientLightState const& AmbientAnalyzer::targetState() const {
    if (m_activeScene) {
        auto it = m_scenes.find(m_activeScene);
        if (it != m_scenes.end()) {
            return it->second.target;
        }
    }
    return m_defaultState;
}

bool AmbientAnalyzer::ensurePrograms() {
    if (m_downsampleProgram) return true;
    auto* prog = paimon::shaders::loadShader(
        "paimbnails/ambient-downsample-v1",
        "cell_vertex.glsl",
        "ambient_downsample.glsl",
        nullptr,
        nullptr
    );
    if (!prog) return false;
    prog->use();
    glUniform1i(glGetUniformLocation(prog->getProgram(), "u_texture"), 0);
    m_downsampleProgram = prog;
    return true;
}

bool AmbientAnalyzer::ensureGridTarget() {
    if (m_gridTex && m_gridFbo) return true;
    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

    if (m_gridTex) glDeleteTextures(1, &m_gridTex);
    if (m_gridFbo) glDeleteFramebuffers(1, &m_gridFbo);
    m_gridTex = 0;
    m_gridFbo = 0;

    glGenTextures(1, &m_gridTex);
    if (!m_gridTex) return false;
    bindUnit0(m_gridTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kGridW, kGridH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenFramebuffers(1, &m_gridFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_gridFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_gridTex, 0);
    bool const complete = (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    return complete;
}

bool AmbientAnalyzer::ensureCopyTarget(int w, int h) {
    if (w <= 0 || h <= 0) return false;
    if (m_copyTex && m_copyW == w && m_copyH == h) return true;
    if (m_copyFbo) {
        glDeleteFramebuffers(1, &m_copyFbo);
        m_copyFbo = 0;
    }
    if (m_copyTex) glDeleteTextures(1, &m_copyTex);
    glGenTextures(1, &m_copyTex);
    if (!m_copyTex) return false;
    bindUnit0(m_copyTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    m_copyW = w;
    m_copyH = h;
    return true;
}

#if defined(GEODE_IS_WINDOWS)
bool AmbientAnalyzer::ensureCopyFbo(int w, int h) {
    if (!ensureCopyTarget(w, h)) return false;
    if (m_copyFbo) return true;
    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGenFramebuffers(1, &m_copyFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_copyFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_copyTex, 0);
    bool const complete = (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    if (!complete) {
        glDeleteFramebuffers(1, &m_copyFbo);
        m_copyFbo = 0;
    }
    return complete;
}
#endif

bool AmbientAnalyzer::ensureBlitTarget() {
    if (m_blitTex && m_blitFbo) return true;
    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

    if (m_blitTex) glDeleteTextures(1, &m_blitTex);
    if (m_blitFbo) glDeleteFramebuffers(1, &m_blitFbo);
    m_blitTex = 0;
    m_blitFbo = 0;

    glGenTextures(1, &m_blitTex);
    if (!m_blitTex) return false;
    bindUnit0(m_blitTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kBlitW, kBlitH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenFramebuffers(1, &m_blitFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_blitFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_blitTex, 0);
    bool const complete = (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    return complete;
}

void AmbientAnalyzer::collectPendingPbo(unsigned frame) {
#if defined(GEODE_IS_WINDOWS)
    if (!m_pboPending || !m_pbo || frame < m_pboFrame + 2) return;
    auto it = m_scenes.find(m_pboScene);
    if (it == m_scenes.end() || it->second.generation != m_pboSceneGen) {
        m_pboPending = false;
        m_pboScene = nullptr;
        return;
    }
    auto& entry = it->second;
    glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbo);
    if (void const* mapped = glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY)) {
        std::memcpy(m_readback.data(), mapped, m_readback.size());
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        auto candidate = analyzeAmbientGrid(m_readback.data(), kGridW, kGridH);
        if (candidate.averageLuma < 0.012f && entry.smoothed.fromCapture && ++entry.consecutiveDarkSamples < 2) {
            // Skip single-frame black flashes during async backdrop swaps/resets.
        } else {
            entry.consecutiveDarkSamples = 0;
            entry.target = std::move(candidate);
            if (!entry.smoothed.fromCapture) {
                entry.smoothed = entry.target;
            }
        }
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    m_pboPending = false;
    m_pboScene = nullptr;
#else
    (void)frame;
#endif
}

void AmbientAnalyzer::enqueuePboOrReadImmediate(unsigned frame, CCScene const* scene, SceneAmbientEntry& entry) {
    glBindFramebuffer(GL_FRAMEBUFFER, m_gridFbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

#if defined(GEODE_IS_WINDOWS)
    // On desktop Windows OpenGL, use non-blocking PBO DMA once the scene has an
    // initial capture, so scene switches react immediately while steady updates never stall.
    if (!m_pbo) {
        glGenBuffers(1, &m_pbo);
        if (m_pbo) {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbo);
            glBufferData(GL_PIXEL_PACK_BUFFER, static_cast<GLsizeiptr>(m_readback.size()), nullptr, GL_STREAM_READ);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        }
    }
    if (m_pbo && entry.smoothed.fromCapture && entry.target.fromCapture) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbo);
        glReadPixels(0, 0, kGridW, kGridH, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        m_pboPending = true;
        m_pboFrame = frame;
        m_pboScene = scene;
        m_pboSceneGen = entry.generation;
        return;
    }
#else
    (void)frame;
    (void)scene;
#endif

    glReadPixels(0, 0, kGridW, kGridH, GL_RGBA, GL_UNSIGNED_BYTE, m_readback.data());
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    auto candidate = analyzeAmbientGrid(m_readback.data(), kGridW, kGridH);
    if (candidate.averageLuma >= 0.012f || !entry.smoothed.fromCapture || ++entry.consecutiveDarkSamples >= 2) {
        entry.consecutiveDarkSamples = 0;
        entry.target = std::move(candidate);
        if (!entry.smoothed.fromCapture) {
            entry.smoothed = entry.target;
        }
    }
}

void AmbientAnalyzer::maybeSampleFramebuffer(
    CCScene const* ownerScene,
    GLint framebuffer,
    int vpX, int vpY, int vpW, int vpH
) {
    if (m_broken || vpW <= 0 || vpH <= 0) return;

    void const* token = currentContextToken();
    if (token != m_contextToken) {
        if (m_contextToken) releaseGL(false);
        m_contextToken = token;
    }

    auto* director = CCDirector::get();
    auto* running = director ? director->getRunningScene() : nullptr;
    auto* trans = running ? typeinfo_cast<CCTransitionScene*>(running) : nullptr;
    CCScene const* targetScene = ownerScene ? ownerScene : m_activeScene;
    if (!targetScene) return;

    auto& entry = ensureSceneEntry(targetScene);
    if (trans) {
        if (targetScene == trans->m_pOutScene) {
            // Freeze outgoing scene colors during transition so fade-outs never alter them.
            return;
        }
        if (targetScene == trans->m_pInScene) {
            // Allow sampling the incoming scene once on its initial draw/capture
            // (e.g. offscreen CCRenderTexture capture) so Frame 0 has exact colors.
            if (entry.smoothed.fromCapture) return;
        }
    }

    unsigned const frame = director ? director->getTotalFrames() : 0;

    // Collect any async PBO result queued 2+ frames ago without blocking.
    collectPendingPbo(frame);
    if (m_pboPending) return;

    if (entry.lastSampleTime >= 0.0 && frame == entry.lastSampledFrame) return;
    double const t = nowSeconds();
    if (entry.smoothed.fromCapture && entry.lastSampleTime >= 0.0 && (t - entry.lastSampleTime) < kSampleIntervalSeconds) {
        return;
    }

    GLint sampleBuffers = 0;
    glGetIntegerv(GL_SAMPLE_BUFFERS, &sampleBuffers);
#if defined(GEODE_IS_WINDOWS)
    if (sampleBuffers > 0 && glBlitFramebuffer == nullptr) return;
#else
    if (sampleBuffers > 0) return;
#endif

    if (!ensurePrograms()) {
        m_broken = true;
        return;
    }

    bool const validate = !m_validated;
    if (validate) {
        drainGlErrorsBounded();
    }

    if (!ensureGridTarget()) {
        m_broken = true;
        return;
    }

    GLuint sourceTex = 0;
    GLboolean const scissor = glIsEnabled(GL_SCISSOR_TEST);
    if (scissor) glDisable(GL_SCISSOR_TEST);
    GLboolean const stencil = glIsEnabled(GL_STENCIL_TEST);
    if (stencil) glDisable(GL_STENCIL_TEST);

#if defined(GEODE_IS_WINDOWS)
    if (sampleBuffers > 0 && glBlitFramebuffer != nullptr && ensureCopyFbo(vpW, vpH)) {
        // Resolve MSAA framebuffer at exact 1:1 dimensions before GPU shader downsampling.
        drainGlErrorsBounded();
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_copyFbo);
        glBlitFramebuffer(
            vpX, vpY, vpX + vpW, vpY + vpH,
            0, 0, vpW, vpH,
            GL_COLOR_BUFFER_BIT, GL_NEAREST
        );
        if (glGetError() == GL_NO_ERROR) {
            sourceTex = m_copyTex;
        } else {
            drainGlErrorsBounded();
            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
        }
    } else if (sampleBuffers == 0 && glBlitFramebuffer != nullptr && ensureBlitTarget()) {
        // Prefer a tiny 96x56 glBlitFramebuffer downsample over copying the full 1080p/4K buffer.
        drainGlErrorsBounded();
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_blitFbo);
        glBlitFramebuffer(
            vpX, vpY, vpX + vpW, vpY + vpH,
            0, 0, kBlitW, kBlitH,
            GL_COLOR_BUFFER_BIT, GL_LINEAR
        );
        if (glGetError() == GL_NO_ERROR) {
            sourceTex = m_blitTex;
        } else {
            drainGlErrorsBounded();
            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
        }
    }
#endif

    if (!sourceTex) {
        if (sampleBuffers > 0 || !ensureCopyTarget(vpW, vpH)) {
            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
            glViewport(vpX, vpY, vpW, vpH);
            if (scissor) glEnable(GL_SCISSOR_TEST);
            if (stencil) glEnable(GL_STENCIL_TEST);
            if (sampleBuffers == 0 && ++m_validationFailures >= 8) m_broken = true;
            return;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
        bindUnit0(m_copyTex);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vpX, vpY, vpW, vpH);
        sourceTex = m_copyTex;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, m_gridFbo);
    glViewport(0, 0, kGridW, kGridH);
    m_downsampleProgram->use();
    kmMat4 ident;
    kmMat4Identity(&ident);
    GLint const mvpLoc = glGetUniformLocation(m_downsampleProgram->getProgram(), "CC_MVPMatrix");
    if (mvpLoc >= 0) {
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, ident.mat);
    }
    bindUnit0(sourceTex);
    ccGLBlendFunc(GL_ONE, GL_ZERO);
#if CC_TEXTURE_ATLAS_USE_VAO
    ccGLBindVAO(0);
#endif
    ccGLEnableVertexAttribs(kCCVertexAttribFlag_Position | kCCVertexAttribFlag_TexCoords);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    static constexpr GLfloat kPos[] = {-1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, 1.f};
    static constexpr GLfloat kUv[]  = { 0.f,  0.f, 1.f,  0.f,  0.f, 1.f, 1.f, 1.f};
    glVertexAttribPointer(kCCVertexAttrib_Position, 2, GL_FLOAT, GL_FALSE, 0, kPos);
    glVertexAttribPointer(kCCVertexAttrib_TexCoords, 2, GL_FLOAT, GL_FALSE, 0, kUv);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    enqueuePboOrReadImmediate(frame, targetScene, entry);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
    glViewport(vpX, vpY, vpW, vpH);
    if (scissor) glEnable(GL_SCISSOR_TEST);
    if (stencil) glEnable(GL_STENCIL_TEST);

    if (validate) {
        if (glGetError() != GL_NO_ERROR) {
            drainGlErrorsBounded();
            if (++m_validationFailures >= 8) {
                m_broken = true;
            }
            return;
        }
        m_validationFailures = 0;
        m_validated = true;
    }

    entry.lastSampledFrame = frame;
    entry.lastSampleTime = t;
}

bool AmbientAnalyzer::analyzeTextureToState(CCTexture2D* texture, AmbientLightState& outState) {
    if (m_broken || !texture || !texture->getName()) return false;
    if (auto it = m_texturePaletteCache.find(texture); it != m_texturePaletteCache.end()) {
        outState = it->second;
        return true;
    }

    void const* token = currentContextToken();
    if (token != m_contextToken) {
        if (m_contextToken) releaseGL(false);
        m_contextToken = token;
    }
    if (!ensurePrograms() || !ensureGridTarget()) return false;

    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    GLint vp[4] = {};
    glGetIntegerv(GL_VIEWPORT, vp);
    GLboolean const scissor = glIsEnabled(GL_SCISSOR_TEST);
    if (scissor) glDisable(GL_SCISSOR_TEST);
    GLboolean const stencil = glIsEnabled(GL_STENCIL_TEST);
    if (stencil) glDisable(GL_STENCIL_TEST);

    glBindFramebuffer(GL_FRAMEBUFFER, m_gridFbo);
    glViewport(0, 0, kGridW, kGridH);
    m_downsampleProgram->use();
    kmMat4 ident;
    kmMat4Identity(&ident);
    GLint const mvpLoc = glGetUniformLocation(m_downsampleProgram->getProgram(), "CC_MVPMatrix");
    if (mvpLoc >= 0) glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, ident.mat);
    bindUnit0(texture->getName());
    ccGLBlendFunc(GL_ONE, GL_ZERO);
#if CC_TEXTURE_ATLAS_USE_VAO
    ccGLBindVAO(0);
#endif
    ccGLEnableVertexAttribs(kCCVertexAttribFlag_Position | kCCVertexAttribFlag_TexCoords);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    static constexpr GLfloat kPos[] = {-1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, 1.f};
    static constexpr GLfloat kUv[]  = { 0.f,  0.f, 1.f,  0.f,  0.f, 1.f, 1.f, 1.f};
    glVertexAttribPointer(kCCVertexAttrib_Position, 2, GL_FLOAT, GL_FALSE, 0, kPos);
    glVertexAttribPointer(kCCVertexAttrib_TexCoords, 2, GL_FLOAT, GL_FALSE, 0, kUv);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, kGridW, kGridH, GL_RGBA, GL_UNSIGNED_BYTE, m_readback.data());
    glPixelStorei(GL_PACK_ALIGNMENT, 4);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    if (scissor) glEnable(GL_SCISSOR_TEST);
    if (stencil) glEnable(GL_STENCIL_TEST);

    outState = analyzeAmbientGrid(m_readback.data(), kGridW, kGridH);
    if (m_texturePaletteCache.size() >= kMaxCachedTexturePalettes) {
        m_texturePaletteCache.clear();
    }
    m_texturePaletteCache[texture] = outState;
    return true;
}

void AmbientAnalyzer::cacheTexturePalette(CCTexture2D* texture) {
    AmbientLightState state;
    (void)analyzeTextureToState(texture, state);
}

bool AmbientAnalyzer::seedSceneFromTexture(CCScene const* scene, CCTexture2D* texture, bool snapIfLive) {
    if (!scene || !texture) return false;
    AmbientLightState candidate;
    if (!analyzeTextureToState(texture, candidate)) return false;

    auto& entry = ensureSceneEntry(scene);
    if (candidate.averageLuma >= 0.012f || !entry.smoothed.fromCapture || ++entry.consecutiveDarkSamples >= 2) {
        entry.consecutiveDarkSamples = 0;
        entry.target = candidate;
        if (snapIfLive || !entry.smoothed.fromCapture) {
            entry.smoothed = entry.target;
        }
    }
    return true;
}

bool AmbientAnalyzer::seedLayerFromTexture(CCLayer* layer, CCTexture2D* texture, bool snapIfLive) {
    if (!layer || !texture) return false;
    AmbientLightState candidate;
    if (!analyzeTextureToState(texture, candidate)) return false;

    if (CCScene const* owner = findOwningNonTransitionScene(layer)) {
        auto& entry = ensureSceneEntry(owner);
        if (candidate.averageLuma >= 0.012f || !entry.smoothed.fromCapture || ++entry.consecutiveDarkSamples >= 2) {
            entry.consecutiveDarkSamples = 0;
            entry.target = candidate;
            if (snapIfLive || !entry.smoothed.fromCapture) {
                entry.smoothed = entry.target;
            }
        }
        m_pendingLayerSeeds.erase(layer);
        return true;
    }

    // Layer is still inside init() before attachment to its destination scene:
    // queue a pending seed so the currently running scene's colors stay untouched.
    m_pendingLayerSeeds[layer] = std::move(candidate);
    return true;
}

void AmbientAnalyzer::bindSceneSeeds(CCScene* scene) {
    if (!scene || typeinfo_cast<CCTransitionScene*>(scene) != nullptr) return;
    if (m_pendingLayerSeeds.empty()) return;
    auto* children = scene->getChildren();
    if (!children) return;
    for (auto* child : CCArrayExt<CCNode*>(children)) {
        auto* layer = typeinfo_cast<CCLayer*>(child);
        if (!layer) continue;
        auto it = m_pendingLayerSeeds.find(layer);
        if (it == m_pendingLayerSeeds.end()) continue;
        auto& entry = ensureSceneEntry(scene);
        entry.target = it->second;
        entry.smoothed = it->second;
        entry.consecutiveDarkSamples = 0;
        m_pendingLayerSeeds.erase(it);
    }
}

bool AmbientAnalyzer::sampleTexture(CCTexture2D* texture) {
    if (!texture) return false;
    if (m_activeScene) {
        return seedSceneFromTexture(m_activeScene, texture, false);
    }
    AmbientLightState state;
    return analyzeTextureToState(texture, state);
}

void AmbientAnalyzer::releaseGL(bool deleteHandles) {
    if (deleteHandles) {
        if (m_copyTex) glDeleteTextures(1, &m_copyTex);
        if (m_copyFbo) glDeleteFramebuffers(1, &m_copyFbo);
        if (m_blitTex) glDeleteTextures(1, &m_blitTex);
        if (m_blitFbo) glDeleteFramebuffers(1, &m_blitFbo);
        if (m_gridTex) glDeleteTextures(1, &m_gridTex);
        if (m_gridFbo) glDeleteFramebuffers(1, &m_gridFbo);
#if defined(GEODE_IS_WINDOWS)
        if (m_pbo) glDeleteBuffers(1, &m_pbo);
#endif
    }
    m_copyTex = 0;
    m_copyFbo = 0;
    m_copyW = 0;
    m_copyH = 0;
    m_blitTex = 0;
    m_blitFbo = 0;
    m_gridTex = 0;
    m_gridFbo = 0;
    m_pbo = 0;
    m_pboPending = false;
    m_pboFrame = 0;
    m_pboScene = nullptr;
    m_pboSceneGen = 0;
    m_downsampleProgram = nullptr;
    m_texturePaletteCache.clear();
    m_validationFailures = 0;
    m_validated = false;
    m_broken = false;
}

} // namespace paimon::ambient
