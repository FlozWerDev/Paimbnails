#include "GlassCapture.hpp"
#include "../../../utils/GLSLLoader.hpp"

#include <Geode/cocos/shaders/ccGLStateCache.h>
#include <algorithm>
#include <cmath>

#if defined(GEODE_IS_WINDOWS)
#include <Windows.h>
#elif defined(GEODE_IS_ANDROID)
#include <EGL/egl.h>
#endif

#ifndef GL_SAMPLE_BUFFERS
#define GL_SAMPLE_BUFFERS 0x80A8
#endif

using namespace geode::prelude;

namespace paimon::glass {
namespace {

// gl names are only meaningful in the context that created them. a
// changed context (Android resume, window rebuild) means forget, not delete.
void const* currentContextToken() {
#if defined(GEODE_IS_WINDOWS)
    return wglGetCurrentContext();
#elif defined(GEODE_IS_ANDROID)
    return eglGetCurrentContext();
#else
    return nullptr;
#endif
}

constexpr int kSharpStep = 64;
constexpr int kLevelStep = 32;
constexpr unsigned kIdleFrames = 180;
constexpr int kMaxOrdinal = 3;

// pixels the blur chain reaches past a plate; the copy needs that margin.
int blurPadFor(int levels) {
    return std::clamp(4 << levels, 16, 64);
}

// bind on unit 0 for real and keep Cocos' texture cache in agreement, so a
// stale cache entry can never redirect a copy into somebody else's texture.
void bindUnit0(GLuint texture) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    ccGLBindTexture2DN(0, texture);
}

} // namespace

GlassCapture& GlassCapture::get() {
    static GlassCapture instance;
    return instance;
}

double GlassCapture::now() const {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

void GlassCapture::setProfile(GlassProfile const& profile, float blurSpread) {
    bool const chainChanged = profile.live != m_profile.live ||
        profile.blurLevels != m_profile.blurLevels || std::abs(blurSpread - m_spread) > 1e-4f;
    m_profile = profile;
    m_spread = blurSpread;
    if (chainChanged) {
        for (auto& slot : m_slots) slot.valid = false;
    }
    // a settings change retries a driver path that failed before.
    m_broken = false;
}

void GlassCapture::beginFrame() {
    unsigned const frame = CCDirector::get()->getTotalFrames();
    if (m_frameStarted && frame == m_frame) return;
    m_frameStarted = true;
    m_frame = frame;
    m_capturesThisFrame = 0;
    m_starvedLastFrame = m_starvedThisFrame;
    m_starvedThisFrame = false;
    m_roots.clear();
    void const* token = currentContextToken();
    if (token != m_contextToken) {
        if (m_contextToken) releaseGL(false);
        m_contextToken = token;
    }
}

int GlassCapture::ordinalFor(void const* root, GlassRect const& plate) {
    beginFrame();
    if (!m_profile.layered) return 0;
    for (auto const& entry : m_roots) {
        if (entry.root != root) continue;
        int overlaps = 0;
        for (auto const& prev : entry.rects) {
            if (prev.intersects(plate)) {
                ++overlaps;
                if (overlaps >= kMaxOrdinal) return kMaxOrdinal;
            }
        }
        return overlaps;
    }
    return 0;
}

void GlassCapture::notePanelDrawn(void const* root, GlassRect const& plate) {
    beginFrame();
    // If an upper-layer slot for this root already captured this frame in a
    // region this panel just drew into, invalidate its fresh-frame flag so
    // subsequent overlapping plates recapture with this panel included.
    for (auto& slot : m_slots) {
        if (slot.root == root && slot.ordinal > 0 && slot.capturedFrame == m_frame &&
            slot.region.intersects(plate)) {
            slot.capturedFrame = 0;
            slot.valid = false;
        }
    }
    for (auto& entry : m_roots) {
        if (entry.root == root) {
            entry.rects.push_back(plate);
            return;
        }
    }
    RootPanels entry;
    entry.root = root;
    entry.rects.push_back(plate);
    m_roots.push_back(std::move(entry));
}

GlassCapture::Slot* GlassCapture::findSlot(void const* root, int ordinal) {
    for (auto& slot : m_slots) {
        if (slot.root == root && slot.ordinal == ordinal) return &slot;
    }
    return nullptr;
}

GlassCapture::Slot* GlassCapture::allocateSlot(void const* root, int ordinal) {
    // fixed capacity: slot pointers stay valid while a frame draws.
    if (m_slots.capacity() < kMaxSlots) m_slots.reserve(kMaxSlots);
    if (m_slots.size() < kMaxSlots) {
        auto& slot = m_slots.emplace_back();
        slot.root = root;
        slot.ordinal = ordinal;
        return &slot;
    }
    // reuse the least recently used slot, never one already drawn this frame.
    Slot* oldest = nullptr;
    for (auto& slot : m_slots) {
        if (slot.lastUsedFrame == m_frame) continue;
        if (!oldest || m_frame - slot.lastUsedFrame > m_frame - oldest->lastUsedFrame) oldest = &slot;
    }
    if (!oldest) return nullptr;
    auto levels = oldest->levels;
    *oldest = Slot{};
    oldest->levels = levels; // keep the allocations, drop the contents
    oldest->root = root;
    oldest->ordinal = ordinal;
    return oldest;
}

std::optional<CaptureView> GlassCapture::acquire(void const* root, int ordinal, GlassRect const& plate,
                                                 GLint framebuffer, GlassRect const& viewport) {
    beginFrame();
    if (!m_profile.live || m_broken || plate.empty() || viewport.empty()) return std::nullopt;
    GlassRect const visible = padAndClamp(plate, 0, viewport);
    if (visible.empty()) return std::nullopt;
    m_lastActiveFrame = m_frame;

    Slot* slot = findSlot(root, ordinal);
    if (!slot) slot = allocateSlot(root, ordinal);
    if (!slot) return std::nullopt;
    slot->lastUsedFrame = m_frame;
    if (slot->unionFrame != m_frame) {
        slot->unionPrev = slot->unionThis;
        slot->unionThis = {};
        slot->unionFrame = m_frame;
    }
    slot->unionThis = unite(slot->unionThis, visible);

    bool const sameTarget = slot->valid && slot->framebuffer == framebuffer && slot->viewport == viewport;
    bool const capturedNow = sameTarget && slot->capturedFrame == m_frame;
    bool const covers = sameTarget && slot->region.contains(visible);
    bool due = !capturedNow &&
        (m_profile.refreshSeconds <= 0.f || now() - slot->capturedAt >= m_profile.refreshSeconds);
    // the budget turned plates away last frame: copies refreshed then wait
    // one frame so the refused ones go first instead of starving.
    if (due && covers && m_starvedLastFrame && slot->capturedFrame + 1 == m_frame) due = false;
    bool const wanted = !covers || due;

    if (wanted && m_capturesThisFrame < m_profile.maxCapturesPerFrame) {
        // last frame's plates predict this frame's: one copy serves them all.
        // A brand-new slot starts with an empty unionPrev, so capture the full
        // viewport once so Frame 0 never starves later plates or stretches edges.
        GlassRect want = slot->unionPrev.empty()
            ? viewport
            : unite(visible, padAndClamp(slot->unionPrev, 0, viewport));
        if (capturedNow) want = unite(want, slot->region);
        int const levels = std::clamp(m_profile.blurLevels + (viewport.h > 1500 ? 1 : 0), 1, kMaxLevels);
        GlassRect const region = padAndClamp(want, blurPadFor(levels), viewport);
        if (region.empty()) return std::nullopt;
        ++m_capturesThisFrame;
        if (!capture(*slot, region, framebuffer, viewport)) {
            slot->valid = false;
            return std::nullopt;
        }
    } else {
        if (wanted) m_starvedThisFrame = true;
        if (!sameTarget) return std::nullopt;
    }
    return viewFor(*slot);
}

bool GlassCapture::capture(Slot& slot, GlassRect const& region, GLint framebuffer, GlassRect const& viewport) {
    if (!ensurePrograms()) {
        log::warn("[Glass] blur shader unavailable; using the simulated material");
        m_broken = true;
        return false;
    }
    // multisampled targets cannot be copied with glCopyTexSubImage2D.
    GLint sampleBuffers = 0;
    glGetIntegerv(GL_SAMPLE_BUFFERS, &sampleBuffers);
    if (sampleBuffers > 0) return false;

    bool const validate = !m_validated;
    if (validate) {
        while (glGetError() != GL_NO_ERROR) {}
    }

    auto const restoreTarget = [&] {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
        glViewport(viewport.x, viewport.y, viewport.w, viewport.h);
    };

    // compare sizes, not names: drivers hand a deleted name straight back.
    int const previousSharpW = m_sharp.width;
    int const previousSharpH = m_sharp.height;
    int const levels = std::clamp(m_profile.blurLevels + (viewport.h > 1500 ? 1 : 0), 1, kMaxLevels);
    bool ok = ensureTarget(m_sharp, viewport.w, viewport.h, false, kSharpStep);
    int w = region.w;
    int h = region.h;
    for (int i = 0; ok && i < levels; ++i) {
        w = halfExtent(w);
        h = halfExtent(h);
        slot.contentW[i] = w;
        slot.contentH[i] = h;
        ok = ensureTarget(slot.levels[i], w, h, true, kLevelStep);
    }
    if (!ok) {
        restoreTarget();
        log::warn("[Glass] could not allocate the glass targets; using the simulated material");
        m_broken = true;
        return false;
    }
    if (m_sharp.width != previousSharpW || m_sharp.height != previousSharpH) {
        // a regrown copy texture lost what every other slot copied into it.
        for (auto& other : m_slots) other.sharpValid = false;
    }
    slot.levelCount = levels;

    // 1. Copy what is drawn so far behind the plates (GPU-side, no readback).
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
    bindUnit0(m_sharp.tex);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, region.x - viewport.x, region.y - viewport.y,
                        region.x, region.y, region.w, region.h);

    // 2. Dual-kawase chain: halve `levels` times, then climb back to 1/2.
    GLboolean const scissor = glIsEnabled(GL_SCISSOR_TEST);
    if (scissor) glDisable(GL_SCISSOR_TEST);
    m_blurProgram->use();
    ccGLBlendFunc(GL_ONE, GL_ZERO);
#if CC_TEXTURE_ATLAS_USE_VAO
    ccGLBindVAO(0);
#endif
    ccGLEnableVertexAttribs(kCCVertexAttribFlag_Position | kCCVertexAttribFlag_TexCoords);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    float const sharpW = static_cast<float>(m_sharp.width);
    float const sharpH = static_cast<float>(m_sharp.height);
    float const sourceUv[4] = {
        static_cast<float>(region.x - viewport.x) / sharpW,
        static_cast<float>(region.y - viewport.y) / sharpH,
        static_cast<float>(region.right() - viewport.x) / sharpW,
        static_cast<float>(region.top() - viewport.y) / sharpH,
    };
    blurPass(m_sharp.tex, sourceUv, 1.f / sharpW, 1.f / sharpH,
             slot.levels[0], slot.contentW[0], slot.contentH[0], false);
    for (int i = 1; i < levels; ++i) {
        auto const& source = slot.levels[i - 1];
        float const uv[4] = {0.f, 0.f,
            static_cast<float>(slot.contentW[i - 1]) / static_cast<float>(source.width),
            static_cast<float>(slot.contentH[i - 1]) / static_cast<float>(source.height)};
        blurPass(source.tex, uv, 1.f / static_cast<float>(source.width), 1.f / static_cast<float>(source.height),
                 slot.levels[i], slot.contentW[i], slot.contentH[i], false);
    }
    for (int i = levels - 1; i > 0; --i) {
        auto const& source = slot.levels[i];
        float const uv[4] = {0.f, 0.f,
            static_cast<float>(slot.contentW[i]) / static_cast<float>(source.width),
            static_cast<float>(slot.contentH[i]) / static_cast<float>(source.height)};
        blurPass(source.tex, uv, 1.f / static_cast<float>(source.width), 1.f / static_cast<float>(source.height),
                 slot.levels[i - 1], slot.contentW[i - 1], slot.contentH[i - 1], true);
    }

    restoreTarget();
    if (scissor) glEnable(GL_SCISSOR_TEST);

    if (validate) {
        GLenum const error = glGetError();
        if (error != GL_NO_ERROR) {
            log::warn("[Glass] framebuffer copy failed (GL 0x{:X}); using the simulated material",
                      static_cast<unsigned>(error));
            m_broken = true;
            return false;
        }
        m_validated = true;
    }

    GlassRect const texRect{region.x - viewport.x, region.y - viewport.y, region.w, region.h};
    for (auto& other : m_slots) {
        if (&other == &slot || !other.sharpValid) continue;
        GlassRect const otherTex{other.region.x - other.viewport.x, other.region.y - other.viewport.y,
                                 other.region.w, other.region.h};
        if (otherTex.intersects(texRect)) other.sharpValid = false;
    }
    slot.valid = true;
    slot.sharpValid = true;
    slot.framebuffer = framebuffer;
    slot.viewport = viewport;
    slot.region = region;
    slot.capturedFrame = m_frame;
    slot.capturedAt = now();
    return true;
}

void GlassCapture::blurPass(GLuint source, float const uv[4], float texelW, float texelH,
                            GlassTarget const& dest, int destW, int destH, bool up) {
    glBindFramebuffer(GL_FRAMEBUFFER, dest.fbo);
    glViewport(0, 0, destW, destH);
    bindUnit0(source);
    glUniform4f(m_locStep, texelW * 0.5f * m_spread, texelH * 0.5f * m_spread, 0.f, 0.f);
    // taps stay inside this capture: the shared copy holds other regions too.
    glUniform4f(m_locClamp, uv[0] + texelW * 0.5f, uv[1] + texelH * 0.5f,
                std::max(uv[0] + texelW * 0.5f, uv[2] - texelW * 0.5f),
                std::max(uv[1] + texelH * 0.5f, uv[3] - texelH * 0.5f));
    glUniform1f(m_locUp, up ? 1.f : 0.f);

    GLfloat const positions[] = {-1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, 1.f};
    GLfloat const coords[] = {uv[0], uv[1], uv[2], uv[1], uv[0], uv[3], uv[2], uv[3]};
    glVertexAttribPointer(kCCVertexAttrib_Position, 2, GL_FLOAT, GL_FALSE, 0, positions);
    glVertexAttribPointer(kCCVertexAttrib_TexCoords, 2, GL_FLOAT, GL_FALSE, 0, coords);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_ANDROID)
    CC_INCREMENT_GL_DRAWS(1);
#endif
}

CaptureView GlassCapture::viewFor(Slot const& slot) const {
    CaptureView view;
    auto const& level = slot.levels[0];
    float const sharpW = static_cast<float>(std::max(m_sharp.width, 1));
    float const sharpH = static_cast<float>(std::max(m_sharp.height, 1));
    float const levelW = static_cast<float>(std::max(level.width, 1));
    float const levelH = static_cast<float>(std::max(level.height, 1));
    float const contentW = static_cast<float>(slot.contentW[0]);
    float const contentH = static_cast<float>(slot.contentH[0]);
    auto const& region = slot.region;
    auto const& viewport = slot.viewport;

    view.sharpTex = m_sharp.tex;
    view.blurTex = level.tex;
    view.sharpValid = slot.sharpValid && m_sharp.tex != 0;
    view.sharpMap[0] = static_cast<float>(viewport.x);
    view.sharpMap[1] = static_cast<float>(viewport.y);
    view.sharpMap[2] = 1.f / sharpW;
    view.sharpMap[3] = 1.f / sharpH;
    view.sharpClamp[0] = (static_cast<float>(region.x - viewport.x) + 0.5f) / sharpW;
    view.sharpClamp[1] = (static_cast<float>(region.y - viewport.y) + 0.5f) / sharpH;
    view.sharpClamp[2] = (static_cast<float>(region.right() - viewport.x) - 0.5f) / sharpW;
    view.sharpClamp[3] = (static_cast<float>(region.top() - viewport.y) - 0.5f) / sharpH;
    view.blurMap[0] = static_cast<float>(region.x);
    view.blurMap[1] = static_cast<float>(region.y);
    view.blurMap[2] = contentW / static_cast<float>(std::max(region.w, 1)) / levelW;
    view.blurMap[3] = contentH / static_cast<float>(std::max(region.h, 1)) / levelH;
    view.blurClamp[0] = 0.5f / levelW;
    view.blurClamp[1] = 0.5f / levelH;
    view.blurClamp[2] = std::max(0.5f, contentW - 0.5f) / levelW;
    view.blurClamp[3] = std::max(0.5f, contentH - 0.5f) / levelH;
    return view;
}

bool GlassCapture::ensureTarget(GlassTarget& target, int width, int height, bool renderable, int step) {
    if (target.tex && target.width >= width && target.height >= height) return true;
    // grow only: shrinking would reallocate on every popup animation frame.
    int const allocW = bucketUp(std::max(width, target.width), step);
    int const allocH = bucketUp(std::max(height, target.height), step);
    releaseTarget(target, true);

    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (!texture) return false;
    bindUnit0(texture);
    // the copy texture has no alpha: RGB is copyable from every
    // framebuffer format, including RGB565 surfaces on old phones.
    GLenum const format = renderable ? GL_RGBA : GL_RGB;
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(format), allocW, allocH, 0, format,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    target.tex = texture;
    target.width = allocW;
    target.height = allocH;

    if (renderable) {
        GLuint fbo = 0;
        glGenFramebuffers(1, &fbo);
        if (!fbo) {
            releaseTarget(target, true);
            return false;
        }
        target.fbo = fbo;
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            releaseTarget(target, true);
            return false;
        }
    }
    return true;
}

bool GlassCapture::ensurePrograms() {
    if (m_blurProgram) return true;
    auto* program = paimon::shaders::loadShader(
        "paimbnails/glass-blur-v3", "glass_pass_vertex.glsl", "glass_blur.glsl", nullptr, nullptr);
    if (!program) return false;
    GLuint const id = program->getProgram();
    m_locStep = glGetUniformLocation(id, "u_step");
    m_locClamp = glGetUniformLocation(id, "u_clamp");
    m_locUp = glGetUniformLocation(id, "u_up");
    if (m_locStep < 0 || m_locClamp < 0 || m_locUp < 0) return false;
    program->use();
    glUniform1i(glGetUniformLocation(id, "u_texture"), 0);
    m_blurProgram = program;
    return true;
}

void GlassCapture::releaseTarget(GlassTarget& target, bool deleteHandles) {
    if (deleteHandles) {
        if (target.fbo) glDeleteFramebuffers(1, &target.fbo);
        if (target.tex) ccGLDeleteTexture(target.tex);
    }
    target = GlassTarget{};
}

void GlassCapture::releaseGL(bool deleteHandles) {
    releaseTarget(m_sharp, deleteHandles);
    for (auto& slot : m_slots) {
        for (auto& level : slot.levels) releaseTarget(level, deleteHandles);
    }
    m_slots.clear();
    m_blurProgram = nullptr;
    m_locStep = m_locClamp = m_locUp = -1;
    m_contextToken = nullptr;
    m_validated = false;
    m_broken = false;
}

void GlassCapture::trimIdle() {
    if (m_contextToken && currentContextToken() != m_contextToken) return; // beginFrame forgets them
    unsigned const frame = CCDirector::get()->getTotalFrames();
    std::erase_if(m_slots, [&](Slot& slot) {
        if (frame - slot.lastUsedFrame < kIdleFrames) return false;
        for (auto& level : slot.levels) releaseTarget(level, true);
        return true;
    });
    if (m_slots.empty() && frame - m_lastActiveFrame >= kIdleFrames) releaseTarget(m_sharp, true);
}

} // namespace paimon::glass
