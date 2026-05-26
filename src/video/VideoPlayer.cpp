#include "VideoDecoder.hpp"
#include "VideoPlayer.hpp"
#include "AudioExtractor.hpp"
#include "../utils/GLSLLoader.hpp"
#include <Geode/loader/Log.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/cocos/misc_nodes/CCRenderTexture.h>
#include "../features/dynamic-songs/services/DynamicSongManager.hpp"
#include "../utils/MainThreadDelay.hpp"
#include <cfloat>   // DBL_MAX
#include <cmath>
#include <algorithm>
#include <thread>

#if defined(GEODE_IS_WINDOWS)
#include <gl/gl.h>
#elif defined(GEODE_IS_ANDROID)
#include <GLES2/gl2.h>
#elif defined(GEODE_IS_IOS)
#include <OpenGLES/ES2/gl.h>
#elif defined(GEODE_IS_MACOS)
#include <OpenGL/gl.h>
#endif

// SIMD intrinsics for YUV→RGBA
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define PAIMON_VP_HAVE_NEON 1
#endif
#if defined(__SSE2__) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2) || defined(_M_X64)
#include <emmintrin.h>
#define PAIMON_VP_HAVE_SSE2 1
#endif

#include <Geode/cocos/CCDirector.h>

namespace paimon::video {

namespace {

static std::thread::id s_mainThreadId;
static std::once_flag s_mainThreadIdInit;

static void ensureMainThreadId() {
    std::call_once(s_mainThreadIdInit, []() {
        s_mainThreadId = std::this_thread::get_id();
    });
}

static bool isOnMainThread() {
    return std::this_thread::get_id() == s_mainThreadId;
}

FMOD::Channel* getMainBgChannel(FMODAudioEngine* engine) {
    if (!engine) return nullptr;
    if (auto* channel = engine->getActiveMusicChannel(0)) {
        return channel;
    }
    if (!engine->m_backgroundMusicChannel) return nullptr;
    int numCh = 0;
    engine->m_backgroundMusicChannel->getNumChannels(&numCh);
    if (numCh <= 0) return nullptr;
    FMOD::Channel* ch = nullptr;
    if (engine->m_backgroundMusicChannel->getChannel(0, &ch) != FMOD_OK) return nullptr;
    return ch;
}

std::string fileNameOnly(std::string const& path) {
    auto pos = path.find_last_of("\\/");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

bool currentSoundMatches(FMOD::Channel* ch, std::string const& expectedName) {
    if (!ch || expectedName.empty()) return false;

    FMOD::Sound* currentSound = nullptr;
    if (ch->getCurrentSound(&currentSound) != FMOD_OK || !currentSound) {
        return false;
    }

    char nameBuffer[512] = {};
    currentSound->getName(nameBuffer, sizeof(nameBuffer));
    std::string currentName(nameBuffer);
    if (currentName.empty()) return false;
    return fileNameOnly(currentName) == expectedName;
}

}

// ─────────────────────────────────────────────────────────────
// Factory
// ─────────────────────────────────────────────────────────────
std::unique_ptr<VideoPlayer> VideoPlayer::create(const std::string& videoPath) {
    auto ret = std::unique_ptr<VideoPlayer>(new (std::nothrow) VideoPlayer());
    if (ret && ret->init(videoPath, {})) {
        return ret;
    }
    return nullptr;
}

std::unique_ptr<VideoPlayer> VideoPlayer::create(const std::string& videoPath, const VideoPlayerCreateOptions& options) {
    auto ret = std::unique_ptr<VideoPlayer>(new (std::nothrow) VideoPlayer());
    if (ret && ret->init(videoPath, options)) {
        return ret;
    }
    return nullptr;
}

VideoPlayer::~VideoPlayer() {
    stopAudio(true);

    // Stop decoder first to prevent further callbacks
    if (m_decoder) {
        m_decoder->stopDecoding();
        if (!m_decoder->isTerminal()) {
            m_decoder.reset();
        } else {
            // A detached decoder thread may still touch decoder-owned state.
            // Leak the decoder object on process lifetime rather than freeing it
            // while the worker is still running.
            (void)m_decoder.release();
        }
    }

    // Off-main-thread safety: GL calls are only valid on the main thread.
    // If destroyed elsewhere (e.g. worker thread), post GL cleanup to main thread.
    if (!isOnMainThread()) {
        geode::log::warn("[VideoPlayer] Destructor called off main thread — deferring GL cleanup");
        geode::Loader::get()->queueInMainThread([
            tex = m_texture, texY = m_texY, texCb = m_texCb, texCr = m_texCr,
            spr = m_resolveSprite, rt = m_resolveRT, fbo = m_readbackFBO
        ]() {
            if (tex) tex->release();
            if (texY) texY->release();
            if (texCb) texCb->release();
            if (texCr) texCr->release();
            if (spr) spr->release();
            if (rt) rt->release();
            if (fbo) glDeleteFramebuffers(1, &fbo);
        });
        m_texture = nullptr;
        m_texY = nullptr;
        m_texCb = nullptr;
        m_texCr = nullptr;
        m_resolveSprite = nullptr;
        m_resolveRT = nullptr;
        m_readbackFBO = 0;
        m_resolvedRGBA = nullptr;
        delete[] m_rgbaBuffer;
        m_rgbaBuffer = nullptr;
        try {
            m_pboUploader.shutdown();
            m_pboUploaderYUV.shutdown();
        } catch (...) {
            // Ignore shutdown errors during app termination
        }
        return;
    }
    
    // NOTE: We intentionally do NOT restore the animation interval here.
    // Touching CCDirector::setAnimationInterval from multiple VideoPlayers
    // causes FPS fluctuations during LevelInfoLayer transitions.
    // The VideoPlayer uses its own m_targetFPS throttle instead.
    auto* director = cocos2d::CCDirector::get();

    // Clean up RGBA buffer
    delete[] m_rgbaBuffer;
    m_rgbaBuffer = nullptr;
    
    // Shut down PBO uploader before texture cleanup
    try {
        m_pboUploader.shutdown();
        m_pboUploaderYUV.shutdown();
    } catch (...) {
        // Ignore shutdown errors during app termination
    }
    
    // Release the texture we retained in initTexture()
    if (m_texture) {
        // Check if GL context is still valid before releasing
        if (director && director->getOpenGLView()) {
            m_texture->release();
        }
        m_texture = nullptr;
    }

    // Release YUV textures
    if (director && director->getOpenGLView()) {
        if (m_texY)  { m_texY->release();  m_texY = nullptr; }
        if (m_texCb) { m_texCb->release(); m_texCb = nullptr; }
        if (m_texCr) { m_texCr->release(); m_texCr = nullptr; }
        if (m_resolveSprite) { m_resolveSprite->release(); m_resolveSprite = nullptr; }
        if (m_resolveRT) { m_resolveRT->release(); m_resolveRT = nullptr; }
        if (m_readbackFBO) { glDeleteFramebuffers(1, &m_readbackFBO); m_readbackFBO = 0; }
    } else {
        m_texY = nullptr;
        m_texCb = nullptr;
        m_texCr = nullptr;
        m_resolveSprite = nullptr;
        m_resolveRT = nullptr;
        m_readbackFBO = 0;
    }
    m_resolvedRGBA = nullptr;
}

bool VideoPlayer::init(const std::string& videoPath, const VideoPlayerCreateOptions& options) {
    ensureMainThreadId();
    m_filePath = videoPath;
    m_createOptions = options;
    m_decoder = IVideoDecoder::create(videoPath);
    if (!m_decoder) {
        geode::log::warn("VideoPlayer: no decoder for {}", videoPath);
        return false;
    }

    // NOTE: We no longer save/restore the animation interval.
    // VideoPlayer uses its own m_targetFPS to throttle frame processing.
    // Touching the global interval caused FPS instability with multiple players.

    int w = m_decoder->getWidth();
    int h = m_decoder->getHeight();

    // RGBA staging buffer is allocated lazily on first uploadFrame() — only the
    // CPU YUV→RGBA path needs it. The GPU YUV path frees it the first time it
    // succeeds. Allocating eagerly wastes ~8 MB at 1080p when we end up using
    // the GPU path (the common case).

    m_texWidth = w;
    m_texHeight = h;

    // PBO init deferred to first uploadFrame() — must be on GL thread

    if (!initAudio(options) && options.enableAudio) {
        geode::log::warn("[VideoPlayer] Audio init failed for {}", videoPath);
    }

    return true;
}

bool VideoPlayer::initAudio(const VideoPlayerCreateOptions& options) {
    m_audioReady = false;
    m_audioPlaying->store(false, std::memory_order_release);
    m_audioInitFailed = false;
    m_audioSyncAccumulator = 0.0;
    m_audioPath.clear();
    m_audioName.clear();

    if (!options.enableAudio || m_filePath.empty()) {
        return true;
    }

#if defined(USE_MEDIA_FOUNDATION)
    std::string wavPath = getCachedWavPath(m_filePath);
    if (wavPath.empty()) {
        wavPath = extractAudioToWav(m_filePath);
    }

    if (wavPath.empty()) {
        m_audioInitFailed = true;
        return false;
    }

    m_audioPath = wavPath;
    m_audioName = fileNameOnly(wavPath);
    m_audioReady = true;
    return true;
#else
    (void)options;
    m_audioInitFailed = true;
    return false;
#endif
}

void VideoPlayer::playAudioFromCurrentTime(bool restartIfNeeded) {
    if (!m_audioReady || m_audioPath.empty()) return;

    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine) return;

    auto* bgCh = getMainBgChannel(engine);
    bool alreadyOurs = currentSoundMatches(bgCh, m_audioName);
    if (alreadyOurs && bgCh) {
        bgCh->setPaused(false);
        bgCh->setVolume(std::clamp(m_volume, 0.0f, 1.0f));
        m_audioPlaying->store(true, std::memory_order_release);
        syncAudioToPlaybackTime(true);
        return;
    }

    if (!restartIfNeeded && bgCh && !alreadyOurs) {
        return;
    }

    DynamicSongManager::s_selfPlayMusic = true;
    engine->playMusic(m_audioPath, m_loop, 0.0f, 0);
    DynamicSongManager::s_selfPlayMusic = false;

    bgCh = getMainBgChannel(engine);
    if (!bgCh || !currentSoundMatches(bgCh, m_audioName)) {
        m_audioPlaying->store(false, std::memory_order_release);
        return;
    }

    bgCh->setVolume(std::clamp(m_volume, 0.0f, 1.0f));
    bgCh->setPaused(false);
    m_audioPlaying->store(true, std::memory_order_release);
    syncAudioToPlaybackTime(true);
}

void VideoPlayer::pauseAudio() {
    if (!m_audioReady) return;
    auto* engine = FMODAudioEngine::sharedEngine();
    auto* bgCh = getMainBgChannel(engine);
    if (!bgCh || !currentSoundMatches(bgCh, m_audioName)) {
        m_audioPlaying->store(false, std::memory_order_release);
        return;
    }
    bgCh->setPaused(true);
    m_audioPlaying->store(false, std::memory_order_release);
}

void VideoPlayer::stopAudio(bool stopChannel) {
    if (!m_audioReady) return;
    ++(*m_audioFadeGeneration);
    auto* engine = FMODAudioEngine::sharedEngine();
    auto* bgCh = getMainBgChannel(engine);
    if (!bgCh || !currentSoundMatches(bgCh, m_audioName)) {
        m_audioPlaying->store(false, std::memory_order_release);
        return;
    }

    if (stopChannel) {
        bgCh->stop();
    } else {
        bgCh->setPaused(true);
    }
    m_audioPlaying->store(false, std::memory_order_release);
}

void VideoPlayer::syncAudioToPlaybackTime(bool force) {
    if (!m_audioReady || !m_audioPlaying->load(std::memory_order_acquire)) return;
    if (!force && m_audioSyncAccumulator < 0.25) return;

    auto* engine = FMODAudioEngine::sharedEngine();
    auto* bgCh = getMainBgChannel(engine);
    if (!bgCh || !currentSoundMatches(bgCh, m_audioName)) {
        m_audioPlaying->store(false, std::memory_order_release);
        return;
    }

    unsigned int targetMs = static_cast<unsigned int>(std::max(0.0, m_playbackTime) * 1000.0);
    unsigned int currentMs = 0;
    if (bgCh->getPosition(&currentMs, FMOD_TIMEUNIT_MS) == FMOD_OK) {
        int delta = static_cast<int>(targetMs) - static_cast<int>(currentMs);
        if (!force && std::abs(delta) < 120) {
            m_audioSyncAccumulator = 0.0;
            return;
        }
    }

    bgCh->setPosition(targetMs, FMOD_TIMEUNIT_MS);
    m_audioSyncAccumulator = 0.0;
}

// ─────────────────────────────────────────────────────────────
// Texture init (single RGBA texture, backward compatible)
// ─────────────────────────────────────────────────────────────
void VideoPlayer::initTexture(int width, int height) {
    if (m_texture) return;

    // Reject pathological dimensions before they overflow the int multiplication
    // below. Without this, a corrupt video reporting e.g. 65535x65535 produces
    // a negative size and a tiny allocation that gets blasted past in
    // glTexSubImage2D, scribbling over the heap.
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192) return;

    size_t dataSize = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    auto* data = new (std::nothrow) uint8_t[dataSize]();
    if (!data) return;

    m_texture = new (std::nothrow) cocos2d::CCTexture2D();
    if (m_texture) {
        m_texture->initWithData(data,
            cocos2d::kCCTexture2DPixelFormat_RGBA8888,
            width, height,
            cocos2d::CCSize(static_cast<float>(width),
                            static_cast<float>(height)));
        // The VideoPlayer must own the texture for its entire lifetime.
        // `new` already sets refcount=1 — that IS our ownership reference.
        // Do NOT autorelease — the player may outlive the sprites that
        // reference this texture (e.g. during shared-video TTL grace).
        // Matching release() is in ~VideoPlayer().
        // LINEAR filtering + CLAMP_TO_EDGE
        glBindTexture(GL_TEXTURE_2D, m_texture->getName());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    delete[] data;
}

// ─────────────────────────────────────────────────────────────
// GPU YUV texture init — 3 luminance textures for shader-based conversion
// ─────────────────────────────────────────────────────────────
void VideoPlayer::initYUVTextures(int width, int height) {
    if (m_texY) return;  // already initialized

    int uvW = (width + 1) / 2;
    int uvH = (height + 1) / 2;

    auto createLuminanceTex = [](int w, int h) -> cocos2d::CCTexture2D* {
        auto* tex = new (std::nothrow) cocos2d::CCTexture2D();
        if (!tex) return nullptr;
        auto* data = new (std::nothrow) uint8_t[w * h]();
        if (!data) { delete tex; return nullptr; }
        // Use I8 (GL_LUMINANCE) so the shader can read the value from .r
        // A8 (GL_ALPHA) stores data in .a only, causing .r=0 → green/black tint
        tex->initWithData(data, cocos2d::kCCTexture2DPixelFormat_I8, w, h,
                          cocos2d::CCSize(static_cast<float>(w), static_cast<float>(h)));
        delete[] data;
        // LINEAR + CLAMP
        glBindTexture(GL_TEXTURE_2D, tex->getName());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
        return tex;
    };

    m_texY  = createLuminanceTex(width, height);
    m_texCb = createLuminanceTex(uvW, uvH);
    m_texCr = createLuminanceTex(uvW, uvH);

    if (!m_texY || !m_texCb || !m_texCr) {
        // Cleanup on failure
        if (m_texY)  { m_texY->release();  m_texY = nullptr; }
        if (m_texCb) { m_texCb->release(); m_texCb = nullptr; }
        if (m_texCr) { m_texCr->release(); m_texCr = nullptr; }
        geode::log::warn("[VideoPlayer] Failed to create YUV textures, falling back to CPU path");
        return;
    }

    // Load the YUV shader
    m_yuvShader = paimon::shaders::getYUVShader();
    if (!m_yuvShader) {
        m_texY->release();  m_texY = nullptr;
        m_texCb->release(); m_texCb = nullptr;
        m_texCr->release(); m_texCr = nullptr;
        geode::log::warn("[VideoPlayer] YUV shader not available, falling back to CPU path");
        return;
    }

    m_useGPUYuv = true;
    geode::log::info("[VideoPlayer] GPU YUV→RGB path active ({}x{})", width, height);
}

// ─────────────────────────────────────────────────────────────
// GPU upload: upload Y/Cb/Cr planes directly to GPU textures
// ─────────────────────────────────────────────────────────────
bool VideoPlayer::uploadFrameGPU(const IVideoDecoder::Frame& frame) {
    if (!m_texY || !m_texCb || !m_texCr) return false;

    int w = frame.width;
    int h = frame.height;
    int uvW = (w + 1) / 2;
    int uvH = (h + 1) / 2;

    // Try PBO YUV path first
    if (m_pboUploaderYUV.isInitialized()) {
        if (m_pboUploaderYUV.upload(
                m_texY->getName(), m_texCb->getName(), m_texCr->getName(),
                frame.planeY, frame.strideY,
                frame.planeCb, frame.strideCb,
                frame.planeCr, frame.strideCr,
                w, h)) {
            m_hasVisibleFrame = true;
            return true;
        }
        // All PBO slots busy — fall through to direct upload
    }

    // Direct upload path (no PBO)
    auto uploadPlane = [](GLuint texId, const uint8_t* data, int stride, int planeW, int planeH) {
        glBindTexture(GL_TEXTURE_2D, texId);
        if (stride == planeW) {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, planeW, planeH,
                            GL_LUMINANCE, GL_UNSIGNED_BYTE, data);
        } else {
            // Row-by-row upload for non-contiguous strides
            for (int row = 0; row < planeH; ++row) {
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, row, planeW, 1,
                                GL_LUMINANCE, GL_UNSIGNED_BYTE, data + row * stride);
            }
        }
        glBindTexture(GL_TEXTURE_2D, 0);
    };

    uploadPlane(m_texY->getName(),  frame.planeY,  frame.strideY,  w, h);
    uploadPlane(m_texCb->getName(), frame.planeCb, frame.strideCb, uvW, uvH);
    uploadPlane(m_texCr->getName(), frame.planeCr, frame.strideCr, uvW, uvH);

    m_hasVisibleFrame = true;
    return true;
}

// ─────────────────────────────────────────────────────────────
// YUV→RGBA conversion — video-range (limited 16-235/16-240)
//
// Supports both BT.601 (SD, <720p) and BT.709 (HD, ≥720p) colour spaces.
// Colour-space auto-detected from frame size, matching the ITU conventions
// used by virtually all encoders.
//
// Coefficients are Q10 fixed-point:
//
//   Y'  = y_scale * (Y  - 16)         ; scale ≈ 1.164 → 1192/1024
//   Cb' = Cb - 128
//   Cr' = Cr - 128
//   R   = (Y' + cr_r * Cr')             >> 10
//   G   = (Y' - cb_g * Cb' - cr_g * Cr') >> 10
//   B   = (Y' + cb_b * Cb')             >> 10
//
// BT.709 (HD):
//   R = 1.164 Y' + 1.793 Cr'
//   G = 1.164 Y' - 0.213 Cb' - 0.533 Cr'
//   B = 1.164 Y' + 2.112 Cb'
//
// BT.601 (SD):
//   R = 1.164 Y' + 1.596 Cr'
//   G = 1.164 Y' - 0.392 Cb' - 0.813 Cr'
//   B = 1.164 Y' + 2.017 Cb'
//
// Products use int32 intermediates (via vmull / mulhi+mullo) to avoid the
// int16 overflow that plagued the previous BT.601 implementation and caused
// washed-out chroma on saturated colours.
// ─────────────────────────────────────────────────────────────
struct YUVCoefficients {
    int16_t y_scale;
    int16_t cr_r;
    int16_t cb_g;
    int16_t cr_g;
    int16_t cb_b;
};

// BT.709 limited-range, Q10
static constexpr YUVCoefficients kBT709VideoRange = {
    /* y_scale */ 1192,
    /* cr_r    */ 1836,
    /* cb_g    */ 218,
    /* cr_g    */ 546,
    /* cb_b    */ 2163,
};

// BT.601 limited-range, Q10
static constexpr YUVCoefficients kBT601VideoRange = {
    /* y_scale */ 1192,
    /* cr_r    */ 1634,
    /* cb_g    */ 401,
    /* cr_g    */ 832,
    /* cb_b    */ 2066,
};

static inline const YUVCoefficients& selectYUVCoefficients(int width, int height) {
    // Standard heuristic: HD (≥720p) uses BT.709, SD uses BT.601.
    // Width check catches widescreen 1280x544 etc. where height < 720.
    return (width >= 1280 || height >= 720) ? kBT709VideoRange : kBT601VideoRange;
}

static inline uint8_t clamp8(int v) {
    return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
}

// ─────────────────────────────────────────────────────────────
// Scalar reference implementation
// ─────────────────────────────────────────────────────────────
static void yuvToRgba_scalar(const uint8_t* planeY, int strideY,
                              const uint8_t* planeCb, int strideCb,
                              const uint8_t* planeCr, int strideCr,
                              uint8_t* rgba, int width, int height,
                              const YUVCoefficients& c) {
    int pairWidth = width & ~1;
    for (int y = 0; y < height; ++y) {
        const uint8_t* rowY  = planeY  + y * strideY;
        const uint8_t* rowCb = planeCb + (y >> 1) * strideCb;
        const uint8_t* rowCr = planeCr + (y >> 1) * strideCr;
        uint8_t* out = rgba + y * width * 4;

        for (int x = 0; x < pairWidth; x += 2) {
            int uvX = x >> 1;
            int Cb = rowCb[uvX] - 128;
            int Cr = rowCr[uvX] - 128;

            int crR = c.cr_r * Cr;
            int cbG = c.cb_g * Cb;
            int crG = c.cr_g * Cr;
            int cbB = c.cb_b * Cb;

            int y0 = c.y_scale * (rowY[x]     - 16);
            int y1 = c.y_scale * (rowY[x + 1] - 16);

            out[x * 4 + 0] = clamp8((y0 + crR) >> 10);
            out[x * 4 + 1] = clamp8((y0 - cbG - crG) >> 10);
            out[x * 4 + 2] = clamp8((y0 + cbB) >> 10);
            out[x * 4 + 3] = 255;

            out[x * 4 + 4] = clamp8((y1 + crR) >> 10);
            out[x * 4 + 5] = clamp8((y1 - cbG - crG) >> 10);
            out[x * 4 + 6] = clamp8((y1 + cbB) >> 10);
            out[x * 4 + 7] = 255;
        }

        if (pairWidth < width) {
            int x = pairWidth;
            int Cb = rowCb[x >> 1] - 128;
            int Cr = rowCr[x >> 1] - 128;
            int y0 = c.y_scale * (rowY[x] - 16);
            out[x * 4 + 0] = clamp8((y0 + c.cr_r * Cr) >> 10);
            out[x * 4 + 1] = clamp8((y0 - c.cb_g * Cb - c.cr_g * Cr) >> 10);
            out[x * 4 + 2] = clamp8((y0 + c.cb_b * Cb) >> 10);
            out[x * 4 + 3] = 255;
        }
    }
}

#if PAIMON_VP_HAVE_NEON
// ARM NEON: 8 output pixels per iteration, int32 intermediates via vmull_s16.
static void yuvToRgba_neon(const uint8_t* planeY, int strideY,
                            const uint8_t* planeCb, int strideCb,
                            const uint8_t* planeCr, int strideCr,
                            uint8_t* rgba, int width, int height,
                            const YUVCoefficients& c) {
    const int16x4_t vYs  = vdup_n_s16(c.y_scale);
    const int16x4_t vCrR = vdup_n_s16(c.cr_r);
    const int16x4_t vCbG = vdup_n_s16(c.cb_g);
    const int16x4_t vCrG = vdup_n_s16(c.cr_g);
    const int16x4_t vCbB = vdup_n_s16(c.cb_b);
    const int16x8_t v16  = vdupq_n_s16(16);
    const int16x8_t v128 = vdupq_n_s16(128);
    const uint8x8_t v255 = vdup_n_u8(255);

    int pairWidth = width & ~1;

    for (int y = 0; y < height; ++y) {
        const uint8_t* rowY  = planeY  + y * strideY;
        const uint8_t* rowCb = planeCb + (y >> 1) * strideCb;
        const uint8_t* rowCr = planeCr + (y >> 1) * strideCr;
        uint8_t* out = rgba + y * width * 4;

        int x = 0;
        int vecEnd = pairWidth & ~7;
        for (; x < vecEnd; x += 8) {
            uint8x8_t Y8  = vld1_u8(rowY + x);
            uint8x8_t Cb4 = vld1_u8(rowCb + (x >> 1));
            uint8x8_t Cr4 = vld1_u8(rowCr + (x >> 1));

            // Duplicate each chroma byte: [A B C D ...] → [A A B B C C D D]
            uint8x8x2_t cbDup = vzip_u8(Cb4, Cb4);
            uint8x8x2_t crDup = vzip_u8(Cr4, Cr4);
            uint8x8_t Cb8 = cbDup.val[0];
            uint8x8_t Cr8 = crDup.val[0];

            // Expand to int16 and apply offsets
            int16x8_t Y  = vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(Y8)),  v16);
            int16x8_t Cb = vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(Cb8)), v128);
            int16x8_t Cr = vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(Cr8)), v128);

            // Y' = y_scale * (Y - 16) — int32 product (no overflow)
            int32x4_t Y_lo = vmull_s16(vget_low_s16(Y),  vYs);
            int32x4_t Y_hi = vmull_s16(vget_high_s16(Y), vYs);

            // R = Y' + cr_r * Cr
            int32x4_t R_lo = vmlal_s16(Y_lo, vget_low_s16(Cr),  vCrR);
            int32x4_t R_hi = vmlal_s16(Y_hi, vget_high_s16(Cr), vCrR);

            // G = Y' - cb_g * Cb - cr_g * Cr
            int32x4_t G_lo = vmlsl_s16(Y_lo, vget_low_s16(Cb),  vCbG);
            int32x4_t G_hi = vmlsl_s16(Y_hi, vget_high_s16(Cb), vCbG);
            G_lo = vmlsl_s16(G_lo, vget_low_s16(Cr),  vCrG);
            G_hi = vmlsl_s16(G_hi, vget_high_s16(Cr), vCrG);

            // B = Y' + cb_b * Cb
            int32x4_t B_lo = vmlal_s16(Y_lo, vget_low_s16(Cb),  vCbB);
            int32x4_t B_hi = vmlal_s16(Y_hi, vget_high_s16(Cb), vCbB);

            // Shift right 10 with saturating narrow to int16, then saturating
            // narrow to uint8.  vqshrn_n_s32 saturates to [−32768, 32767];
            // vqmovun_s16 saturates to [0, 255].
            int16x8_t R16 = vcombine_s16(vqshrn_n_s32(R_lo, 10), vqshrn_n_s32(R_hi, 10));
            int16x8_t G16 = vcombine_s16(vqshrn_n_s32(G_lo, 10), vqshrn_n_s32(G_hi, 10));
            int16x8_t B16 = vcombine_s16(vqshrn_n_s32(B_lo, 10), vqshrn_n_s32(B_hi, 10));

            uint8x8_t R_u8 = vqmovun_s16(R16);
            uint8x8_t G_u8 = vqmovun_s16(G16);
            uint8x8_t B_u8 = vqmovun_s16(B16);

            uint8x8x4_t rgba8 = { R_u8, G_u8, B_u8, v255 };
            vst4_u8(out + x * 4, rgba8);
        }

        // Scalar tail
        for (; x < pairWidth; x += 2) {
            int uvX = x >> 1;
            int Cb = rowCb[uvX] - 128;
            int Cr = rowCr[uvX] - 128;
            int crR = c.cr_r * Cr;
            int cbG = c.cb_g * Cb;
            int crG = c.cr_g * Cr;
            int cbB = c.cb_b * Cb;
            int y0 = c.y_scale * (rowY[x]     - 16);
            int y1 = c.y_scale * (rowY[x + 1] - 16);
            out[x * 4 + 0] = clamp8((y0 + crR) >> 10);
            out[x * 4 + 1] = clamp8((y0 - cbG - crG) >> 10);
            out[x * 4 + 2] = clamp8((y0 + cbB) >> 10);
            out[x * 4 + 3] = 255;
            out[x * 4 + 4] = clamp8((y1 + crR) >> 10);
            out[x * 4 + 5] = clamp8((y1 - cbG - crG) >> 10);
            out[x * 4 + 6] = clamp8((y1 + cbB) >> 10);
            out[x * 4 + 7] = 255;
        }
        if (pairWidth < width) {
            int xx = pairWidth;
            int Cb = rowCb[xx >> 1] - 128;
            int Cr = rowCr[xx >> 1] - 128;
            int y0 = c.y_scale * (rowY[xx] - 16);
            out[xx * 4 + 0] = clamp8((y0 + c.cr_r * Cr) >> 10);
            out[xx * 4 + 1] = clamp8((y0 - c.cb_g * Cb - c.cr_g * Cr) >> 10);
            out[xx * 4 + 2] = clamp8((y0 + c.cb_b * Cb) >> 10);
            out[xx * 4 + 3] = 255;
        }
    }
}
#endif // NEON

#if PAIMON_VP_HAVE_SSE2
// SSE2: 8 output pixels per iteration, int32 intermediates via mulhi+mullo.
// _mm_mullo_epi16 returns low 16 bits (overflow-prone), so we reconstruct
// the full 32-bit product with unpack(lo, hi).
static inline void sse2Mul32(const __m128i& a, const __m128i& b,
                              __m128i& out_lo, __m128i& out_hi) {
    __m128i lo = _mm_mullo_epi16(a, b);
    __m128i hi = _mm_mulhi_epi16(a, b);
    out_lo = _mm_unpacklo_epi16(lo, hi);
    out_hi = _mm_unpackhi_epi16(lo, hi);
}

static void yuvToRgba_sse2(const uint8_t* planeY, int strideY,
                            const uint8_t* planeCb, int strideCb,
                            const uint8_t* planeCr, int strideCr,
                            uint8_t* rgba, int width, int height,
                            const YUVCoefficients& c) {
    const __m128i v16    = _mm_set1_epi16(16);
    const __m128i v128   = _mm_set1_epi16(128);
    const __m128i vYs    = _mm_set1_epi16(c.y_scale);
    const __m128i vCrR   = _mm_set1_epi16(c.cr_r);
    const __m128i vCbG   = _mm_set1_epi16(c.cb_g);
    const __m128i vCrG   = _mm_set1_epi16(c.cr_g);
    const __m128i vCbB   = _mm_set1_epi16(c.cb_b);
    const __m128i vAlpha = _mm_set1_epi8(static_cast<char>(0xFF));
    const __m128i zero   = _mm_setzero_si128();

    int pairWidth = width & ~1;

    for (int y = 0; y < height; ++y) {
        const uint8_t* rowY  = planeY  + y * strideY;
        const uint8_t* rowCb = planeCb + (y >> 1) * strideCb;
        const uint8_t* rowCr = planeCr + (y >> 1) * strideCr;
        uint8_t* out = rgba + y * width * 4;

        int x = 0;
        int vecEnd = pairWidth & ~7;
        for (; x < vecEnd; x += 8) {
            __m128i Y8 = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(rowY + x));
            __m128i Y  = _mm_sub_epi16(_mm_unpacklo_epi8(Y8, zero), v16);

            int uvX = x >> 1;
            int32_t cb4 = *reinterpret_cast<const int32_t*>(rowCb + uvX);
            int32_t cr4 = *reinterpret_cast<const int32_t*>(rowCr + uvX);
            __m128i Cb4 = _mm_cvtsi32_si128(cb4);
            __m128i Cr4 = _mm_cvtsi32_si128(cr4);
            __m128i Cb8 = _mm_unpacklo_epi8(Cb4, Cb4);
            __m128i Cr8 = _mm_unpacklo_epi8(Cr4, Cr4);
            __m128i Cb  = _mm_sub_epi16(_mm_unpacklo_epi8(Cb8, zero), v128);
            __m128i Cr  = _mm_sub_epi16(_mm_unpacklo_epi8(Cr8, zero), v128);

            // 32-bit products
            __m128i Y_p0,   Y_p1;    sse2Mul32(Y,  vYs,  Y_p0,   Y_p1);
            __m128i CrR_p0, CrR_p1;  sse2Mul32(Cr, vCrR, CrR_p0, CrR_p1);
            __m128i CbG_p0, CbG_p1;  sse2Mul32(Cb, vCbG, CbG_p0, CbG_p1);
            __m128i CrG_p0, CrG_p1;  sse2Mul32(Cr, vCrG, CrG_p0, CrG_p1);
            __m128i CbB_p0, CbB_p1;  sse2Mul32(Cb, vCbB, CbB_p0, CbB_p1);

            // R = (Y' + CrR) >> 10
            __m128i R_p0 = _mm_srai_epi32(_mm_add_epi32(Y_p0, CrR_p0), 10);
            __m128i R_p1 = _mm_srai_epi32(_mm_add_epi32(Y_p1, CrR_p1), 10);
            // G = (Y' - CbG - CrG) >> 10
            __m128i G_p0 = _mm_srai_epi32(_mm_sub_epi32(_mm_sub_epi32(Y_p0, CbG_p0), CrG_p0), 10);
            __m128i G_p1 = _mm_srai_epi32(_mm_sub_epi32(_mm_sub_epi32(Y_p1, CbG_p1), CrG_p1), 10);
            // B = (Y' + CbB) >> 10
            __m128i B_p0 = _mm_srai_epi32(_mm_add_epi32(Y_p0, CbB_p0), 10);
            __m128i B_p1 = _mm_srai_epi32(_mm_add_epi32(Y_p1, CbB_p1), 10);

            // Pack int32→int16 with saturation, then int16→uint8 with saturation
            __m128i R16 = _mm_packs_epi32(R_p0, R_p1);
            __m128i G16 = _mm_packs_epi32(G_p0, G_p1);
            __m128i B16 = _mm_packs_epi32(B_p0, B_p1);
            __m128i R8p = _mm_packus_epi16(R16, R16);
            __m128i G8p = _mm_packus_epi16(G16, G16);
            __m128i B8p = _mm_packus_epi16(B16, B16);

            // Interleave to RGBA
            __m128i RG = _mm_unpacklo_epi8(R8p, G8p);
            __m128i BA = _mm_unpacklo_epi8(B8p, vAlpha);
            __m128i lo = _mm_unpacklo_epi16(RG, BA);
            __m128i hi = _mm_unpackhi_epi16(RG, BA);

            _mm_storeu_si128(reinterpret_cast<__m128i*>(out + x * 4),      lo);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(out + x * 4 + 16), hi);
        }

        // Scalar tail
        for (; x < pairWidth; x += 2) {
            int uvX = x >> 1;
            int Cb = rowCb[uvX] - 128;
            int Cr = rowCr[uvX] - 128;
            int crR = c.cr_r * Cr;
            int cbG = c.cb_g * Cb;
            int crG = c.cr_g * Cr;
            int cbB = c.cb_b * Cb;
            int y0 = c.y_scale * (rowY[x]     - 16);
            int y1 = c.y_scale * (rowY[x + 1] - 16);
            out[x * 4 + 0] = clamp8((y0 + crR) >> 10);
            out[x * 4 + 1] = clamp8((y0 - cbG - crG) >> 10);
            out[x * 4 + 2] = clamp8((y0 + cbB) >> 10);
            out[x * 4 + 3] = 255;
            out[x * 4 + 4] = clamp8((y1 + crR) >> 10);
            out[x * 4 + 5] = clamp8((y1 - cbG - crG) >> 10);
            out[x * 4 + 6] = clamp8((y1 + cbB) >> 10);
            out[x * 4 + 7] = 255;
        }
        if (pairWidth < width) {
            int xx = pairWidth;
            int Cb = rowCb[xx >> 1] - 128;
            int Cr = rowCr[xx >> 1] - 128;
            int y0 = c.y_scale * (rowY[xx] - 16);
            out[xx * 4 + 0] = clamp8((y0 + c.cr_r * Cr) >> 10);
            out[xx * 4 + 1] = clamp8((y0 - c.cb_g * Cb - c.cr_g * Cr) >> 10);
            out[xx * 4 + 2] = clamp8((y0 + c.cb_b * Cb) >> 10);
            out[xx * 4 + 3] = 255;
        }
    }
}
#endif // SSE2

static inline void yuvToRgba(const uint8_t* planeY, int strideY,
                              const uint8_t* planeCb, int strideCb,
                              const uint8_t* planeCr, int strideCr,
                              uint8_t* rgba, int width, int height) {
    const YUVCoefficients& c = selectYUVCoefficients(width, height);
#if PAIMON_VP_HAVE_NEON
    yuvToRgba_neon(planeY, strideY, planeCb, strideCb, planeCr, strideCr, rgba, width, height, c);
#elif PAIMON_VP_HAVE_SSE2
    yuvToRgba_sse2(planeY, strideY, planeCb, strideCb, planeCr, strideCr, rgba, width, height, c);
#else
    yuvToRgba_scalar(planeY, strideY, planeCb, strideCb, planeCr, strideCr, rgba, width, height, c);
#endif
}

// ─────────────────────────────────────────────────────────────
// Pre-allocate textures + PBOs + resolve FBO before the first frame.
// Eliminates the multi-millisecond GL allocation pile-up that used to
// happen during the first uploadFrame() call (texture allocs, PBO
// glBufferData, etc.) and during the first getResolvedRGBATexture()
// call (FBO + sprite + uniform-loc cache).  Anything that has been
// initialised before is left alone, so this can be called eagerly
// from play() / onEnter() and still be cheap on subsequent calls.
// ─────────────────────────────────────────────────────────────
void VideoPlayer::prepareGPUPipeline() {
    if (!isOnMainThread()) return;
    if (m_texWidth <= 0 || m_texHeight <= 0) return;

    // 1) PBO + texture init — same path that uploadFrame() used to take
    //    on the very first frame.  Doing it here amortises the cost
    //    over the time between play() and the first decoded frame
    //    (typically 30–80 ms with a HW decoder), turning a one-shot
    //    main-thread stall into background warm-up work.
    if (!m_pboInitAttempted) {
        m_pboInitAttempted = true;

        if (!m_createOptions.forceRGBA) {
            initYUVTextures(m_texWidth, m_texHeight);
        }

        if (m_useGPUYuv) {
            int ySize  = m_texWidth * m_texHeight;
            int uvW    = (m_texWidth  + 1) / 2;
            int uvH    = (m_texHeight + 1) / 2;
            int cbSize = uvW * uvH;
            int crSize = uvW * uvH;
            if (!m_pboUploaderYUV.init(ySize, cbSize, crSize)) {
                geode::log::info("[VideoPlayer] YUV PBO init failed, using direct YUV upload");
            }
        } else {
            // CPU YUV→RGBA path.  Allocate the staging buffer here so the
            // very first uploadFrame() doesn't have to.  If allocation
            // fails (OOM) we leave m_rgbaBuffer nullptr — uploadFrame()
            // has a defensive retry that returns false on a second
            // failure rather than crashing inside yuvToRgba().
            if (!m_rgbaBuffer) {
                m_rgbaBuffer = new (std::nothrow) uint8_t[
                    static_cast<size_t>(m_texWidth) * m_texHeight * 4];
            }
            if (m_rgbaBuffer) {
                if (!m_pboUploader.init(m_texWidth * m_texHeight * 4)) {
                    geode::log::warn("VideoPlayer: PBO init failed, falling back to direct upload");
                }
            } else {
                // Don't try to init the PBO uploader if we don't have a
                // staging buffer — we can't even fall back to the direct
                // path without one.  uploadFrame() will retry the alloc.
                geode::log::warn("[VideoPlayer] RGBA staging buffer alloc failed in pre-warm "
                                 "({}x{}), will retry on first frame", m_texWidth, m_texHeight);
            }
        }
    }

    // 2) RGBA texture (CPU YUV→RGBA path).  GPU YUV path doesn't need it.
    if (!m_useGPUYuv && !m_texture) {
        initTexture(m_texWidth, m_texHeight);
    }

    // 3) Resolve FBO (GPU YUV path only).  This is what the very first
    //    getResolvedRGBATexture() call from VideoThumbnailSprite::update()
    //    would otherwise build up — CCRenderTexture, CCSprite, shader
    //    uniform locations.  Building it here removes the second visible
    //    hitch (right after the first frame uploads).  We don't render
    //    into it yet (no frame data); resolveYUVToRGBA() does that.
    if (m_useGPUYuv && m_texY && m_texCb && m_texCr && !m_resolveRT) {
        m_blitShader = paimon::shaders::getYUVBlitShader();
        if (m_blitShader) {
            int w = m_texWidth;
            int h = m_texHeight;

            m_resolveRT = cocos2d::CCRenderTexture::create(w, h);
            if (m_resolveRT) {
                m_resolveRT->retain();

                cocos2d::ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
                m_resolveRT->getSprite()->getTexture()->setTexParameters(&params);

                m_resolveSprite = cocos2d::CCSprite::createWithTexture(m_texY);
                if (m_resolveSprite) {
                    m_resolveSprite->retain();

                    float sx = static_cast<float>(w) / m_texY->getContentSize().width;
                    float sy = static_cast<float>(h) / m_texY->getContentSize().height;
                    m_resolveSprite->setScale(std::max(sx, sy));
                    m_resolveSprite->setAnchorPoint({0.5f, 0.5f});
                    m_resolveSprite->setPosition({static_cast<float>(w) * 0.5f, static_cast<float>(h) * 0.5f});
                    m_resolveSprite->setFlipY(true);
                    m_resolveSprite->setShaderProgram(m_blitShader);

                    m_locCb = m_blitShader->getUniformLocationForName("u_textureCb");
                    m_locCr = m_blitShader->getUniformLocationForName("u_textureCr");
                    m_locY  = m_blitShader->getUniformLocationForName("u_textureY");
                    m_locCS = m_blitShader->getUniformLocationForName("u_colorSpace");

                    m_colorSpace = (w >= 1280 || h >= 720) ? 1.0f : 0.0f;

                    m_blitShader->use();
                    if (m_locY  != -1) m_blitShader->setUniformLocationWith1i(m_locY,  0);
                    if (m_locCb != -1) m_blitShader->setUniformLocationWith1i(m_locCb, 1);
                    if (m_locCr != -1) m_blitShader->setUniformLocationWith1i(m_locCr, 2);
                    if (m_locCS != -1) m_blitShader->setUniformLocationWith1f(m_locCS, m_colorSpace);
                } else {
                    m_resolveRT->release();
                    m_resolveRT = nullptr;
                }
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────
// Upload frame: YUV→RGBA conversion + PBO async upload
// ─────────────────────────────────────────────────────────────
bool VideoPlayer::uploadFrame(const IVideoDecoder::Frame& frame) {
    // Pipeline init was hoisted to prepareGPUPipeline() (called from play()).
    // If the caller skipped play() somehow, do a lazy init here so the
    // GL state still ends up in a valid configuration.
    if (!m_pboInitAttempted) {
        prepareGPUPipeline();
    }

    // GPU YUV path — upload planes directly, shader does conversion
    if (m_useGPUYuv) {
        return uploadFrameGPU(frame);
    }

    // Lazily create RGBA texture on first upload (only needed for CPU path)
    if (!m_texture) {
        initTexture(m_texWidth, m_texHeight);
        if (!m_texture) return false;
    }

    // Defensive: prepareGPUPipeline() should have allocated this when the
    // GPU YUV path was unavailable, but if the allocation failed (OOM) or
    // we somehow took a path that bypassed the pre-warm, retry now.  The
    // crash this guards against is yuvToRgba() writing to a nullptr
    // m_rgbaBuffer (EXCEPTION_ACCESS_VIOLATION at 0x0).
    if (!m_rgbaBuffer) {
        m_rgbaBuffer = new (std::nothrow) uint8_t[
            static_cast<size_t>(m_texWidth) * m_texHeight * 4];
        if (!m_rgbaBuffer) {
            geode::log::error("[VideoPlayer] RGBA staging buffer allocation failed ({}x{})",
                              m_texWidth, m_texHeight);
            return false;
        }
    }

    int w = frame.width;
    int h = frame.height;

    if (m_pboUploader.isInitialized()) {
        // Zero-copy path: convert YUV straight into mapped PBO memory.  This
        // avoids the intermediate m_rgbaBuffer → PBO copy (~4MB at 1080p).
        //
        // Exception: on the very first uploaded frame we also fill the CPU
        // staging buffer so copyCurrentFramePixels() can cache it for the
        // thumbnail disk cache.  After that, the staging path is only taken
        // when all PBO slots are busy.
        bool isFirstFrame = !m_hasVisibleFrame;

        if (!isFirstFrame) {
            if (uint8_t* mapped = m_pboUploader.tryBeginRGBAUpload(w, h)) {
                yuvToRgba(frame.planeY, frame.strideY,
                          frame.planeCb, frame.strideCb,
                          frame.planeCr, frame.strideCr,
                          mapped, w, h);
                m_pboUploader.endRGBAUpload(m_texture->getName(), w, h);
                m_hasVisibleFrame = true;
                return true;
            }
        }

        // Fill staging buffer — either for first-frame thumbnail capture or
        // because all PBO slots are in flight and we need a place to hold
        // the converted frame until the next update()'s retry.
        yuvToRgba(frame.planeY, frame.strideY,
                  frame.planeCb, frame.strideCb,
                  frame.planeCr, frame.strideCr,
                  m_rgbaBuffer, w, h);
        if (!m_pboUploader.uploadRGBA(m_texture->getName(), m_rgbaBuffer, w, h)) {
            // Still busy — retry via m_rgbaBuffer next update().
            return false;
        }
        m_hasVisibleFrame = true;
        return true;
    }

    // No PBO available — direct upload path.  Convert into the staging
    // buffer (kept around so copyCurrentFramePixels() can still read it).
    yuvToRgba(frame.planeY, frame.strideY,
              frame.planeCb, frame.strideCb,
              frame.planeCr, frame.strideCr,
              m_rgbaBuffer, w, h);

    glBindTexture(GL_TEXTURE_2D, m_texture->getName());
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h,
                    GL_RGBA, GL_UNSIGNED_BYTE, m_rgbaBuffer);
    glBindTexture(GL_TEXTURE_2D, 0);

    m_hasVisibleFrame = true;
    return true;
}

// Retry an upload using the already-converted m_rgbaBuffer contents.
// Used when uploadFrame() previously failed because all PBO slots were busy:
// we don't need to re-run YUV→RGBA conversion, just re-attempt the upload.
bool VideoPlayer::retryUploadFromRgbaBuffer() {
    if (!m_texture || !m_rgbaBuffer) return false;
    if (m_pboUploader.isInitialized()) {
        if (!m_pboUploader.uploadRGBA(m_texture->getName(), m_rgbaBuffer,
                                       m_texWidth, m_texHeight)) {
            return false;
        }
    } else {
        glBindTexture(GL_TEXTURE_2D, m_texture->getName());
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_texWidth, m_texHeight,
                        GL_RGBA, GL_UNSIGNED_BYTE, m_rgbaBuffer);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    m_hasVisibleFrame = true;
    return true;
}

// ─────────────────────────────────────────────────────────────
// Update loop (called by owner every frame — main thread only)
// ─────────────────────────────────────────────────────────────
void VideoPlayer::update(float dt) {
    if (!m_playing || !m_decoder) return;

    // Only process once per director frame.  Multiple surviving
    // VideoBackgroundUpdateNodes can call us during a layer transition,
    // and running the logic more than once per frame desyncs the decoder.
    auto currentFrame = cocos2d::CCDirector::get()->getTotalFrames();
    if (currentFrame == m_lastUpdateFrame) return;
    m_lastUpdateFrame = currentFrame;

    m_playbackTime += static_cast<double>(dt);
    m_timeSinceLastUpload += static_cast<double>(dt);
    m_audioSyncAccumulator += static_cast<double>(dt);

    // ── Decoder stall detection (Android) ──
    // If the hardware decoder never produces a frame within 5 seconds of
    // play(), it's likely stuck on an unsupported color format or a driver
    // bug.  Stop gracefully to avoid leaving the player in a zombie state
    // that later causes ANR when releaseSharedVideo() tries to forceStop().
    if (!m_hasVisibleFrame && !m_decoderStalled) {
        m_timeSincePlay += static_cast<double>(dt);
        if (m_timeSincePlay > 5.0) {
            m_decoderStalled = true;
            geode::log::warn("[VideoPlayer] Decoder stall detected — no frame produced in 5s, "
                             "stopping playback: {}", m_filePath);
            m_playing = false;
            if (m_decoder) {
                m_decoder->stopDecoding();
            }
            if (m_onFinished) m_onFinished();
            return;
        }
    }

    double minInterval = 1.0 / std::max(m_targetFPS, 1);

    // Retry a pending upload first — the RGBA buffer still holds the last
    // converted frame data from a prior update(), just re-submit to GPU.
    if (m_pendingUpload) {
        if (retryUploadFromRgbaBuffer()) {
            m_pendingUpload = false;
            ++m_frameCounter;
            m_timeSinceLastUpload = 0.0;
        }
    }

    bool timeToProcess = m_timeSinceLastUpload >= minInterval;

    if (timeToProcess && !m_pendingUpload) {
        // Drain past-due frames, rendering only the most recent one.
        // Skip any past-due frame that has a newer past-due frame after it.
        while (true) {
            double headPTS = m_decoder->peekNextPTS();
            if (headPTS > m_playbackTime) break;          // nothing past-due

            double nextPTS = m_decoder->peekSecondPTS();
            if (nextPTS <= m_playbackTime) {
                // There's a newer past-due frame after the head — skip head.
                if (!m_decoder->skipFrame()) break;
                continue;
            }
            // Head is the most recent past-due frame — render it (zero-copy).
            const IVideoDecoder::Frame* f = m_decoder->peekFrame();
            if (!f) break;
            bool ok = uploadFrame(*f);
            m_decoder->releaseFrame();

            if (ok) {
                ++m_frameCounter;
                m_timeSinceLastUpload = 0.0;
            } else {
                // PBO slots all busy — m_rgbaBuffer holds the converted data,
                // retry next update().  The ring slot was already released
                // so the decode thread can keep producing.
                m_pendingUpload = true;
            }
            break;  // rendered (or deferred) one frame per update
        }
    } else if (!m_pendingUpload) {
        // Throttled below target FPS — drop past-due frames without decoding
        // them.  skipFrame() is O(1).
        while (m_decoder->peekNextPTS() <= m_playbackTime) {
            if (!m_decoder->skipFrame()) break;
        }
    }

    // Check if decoder finished and no more frames
    if (m_decoder->isFinished() && m_decoder->peekNextPTS() >= DBL_MAX) {
        geode::log::info("[VideoPlayer] end of stream reached, loop={}", m_loop);
        if (m_loop) {
            m_decoder->seekTo(0.0);
            m_decoder->startDecoding();
            m_playbackTime = 0.0;
            m_pendingUpload = false;
            m_timeSinceLastUpload = 0.0;
            playAudioFromCurrentTime(true);
        } else {
            m_playing = false;
            stopAudio(true);
            if (m_onFinished) m_onFinished();
        }
    }

    syncAudioToPlaybackTime(false);
}

// ─────────────────────────────────────────────────────────────
// Controls
// ─────────────────────────────────────────────────────────────
void VideoPlayer::play() {
    if (m_playing) return;
    if (!m_decoder || m_decoder->isTerminal()) return;
    m_playing = true;
    m_timeSincePlay = 0.0;
    m_decoderStalled = false;
    if (m_decoder) m_decoder->startDecoding();
    // Pre-allocate textures + PBOs + resolve FBO now while the decoder is
    // spinning up.  Doing this here moves ~10–25 ms of GL allocation
    // (glTexImage2D, glBufferData x N, FBO + sprite + uniform locations)
    // out of the first uploadFrame() call on the main thread.  Skipping
    // it would mean the first frame after the user enters a profile with
    // a video pays the entire init cost in a single update() invocation,
    // which is what produces the visible 360→120 fps drop.
    prepareGPUPipeline();
    playAudioFromCurrentTime(true);
}

void VideoPlayer::pause() {
    m_playing = false;
    m_pendingUpload = false;
    if (m_decoder) m_decoder->stopDecoding();
    pauseAudio();
    if (m_decoder && m_decoder->isTerminal()) {
        (void)m_decoder.release();
    }
}

void VideoPlayer::resume() {
    if (!m_decoder) return;
    if (m_decoder->isTerminal()) {
        m_playing = false;
        return;
    }
    
    // If the decoder has finished (end of stream), seek back to start
    // so the video can play again from the beginning
    if (m_decoder->isFinished() || m_decoder->peekNextPTS() >= DBL_MAX) {
        m_decoder->seekTo(0.0);
        m_playbackTime = 0.0;
        // Clear the visible frame flag so we don't show stale content
        // while the decoder restarts and produces new frames
        m_hasVisibleFrame = false;
        // Clear PBO fences to avoid stale data causing display issues
        if (m_pboUploader.isInitialized()) {
            m_pboUploader.clearFences();
        }
    } else {
        double nextPTS = m_decoder->peekNextPTS();
        if (nextPTS < DBL_MAX) {
            m_playbackTime = nextPTS;
        } else {
            m_playbackTime = 0.0;
        }
    }
    
    m_pendingUpload = false;
    m_timeSinceLastUpload = 0.0;

    // Always restart the decoder to ensure it's running
    // This fixes issues when reusing a player that was stopped/finished
    if (!m_playing) {
        m_playing = true;
        m_decoder->startDecoding();
    }
    playAudioFromCurrentTime(true);
}

void VideoPlayer::stop() {
    m_playing = false;
    m_pendingUpload = false;
    m_timeSinceLastUpload = 0.0;
    m_audioSyncAccumulator = 0.0;
    if (m_decoder) {
        m_decoder->stopDecoding();
        if (!m_decoder->isTerminal()) {
            m_decoder->seekTo(0.0);
        } else {
            (void)m_decoder.release();
        }
    }
    m_playbackTime = 0.0;
    stopAudio(true);
}

void VideoPlayer::forceStop() {
    m_playing = false;
    m_pendingUpload = false;
    m_timeSinceLastUpload = 0.0;
    m_audioSyncAccumulator = 0.0;
    if (m_decoder) {
        m_decoder->stopDecoding();
        // No seekTo — avoids SetCurrentPosition deadlock during shutdown
        if (m_decoder->isTerminal()) {
            (void)m_decoder.release();
        }
    }
    m_playbackTime = 0.0;
    stopAudio(true);
    // No setAnimationInterval — director may be shutting down
}

void VideoPlayer::setLoop(bool loop) { m_loop = loop; }
void VideoPlayer::setVolume(float volume) {
    m_volume = std::clamp(volume, 0.0f, 1.0f);
    auto* engine = FMODAudioEngine::sharedEngine();
    auto* bgCh = getMainBgChannel(engine);
    if (bgCh && currentSoundMatches(bgCh, m_audioName)) {
        bgCh->setVolume(m_volume);
    }
}
void VideoPlayer::setTargetFPS(int fps) { m_targetFPS = fps; }

bool VideoPlayer::isPlaying() const { return m_playing; }
bool VideoPlayer::hasVisibleFrame() const { return m_hasVisibleFrame; }
bool VideoPlayer::isTerminal() const { return m_decoder && m_decoder->isTerminal(); }
uint64_t VideoPlayer::getFrameCounter() const { return m_frameCounter; }

cocos2d::CCTexture2D* VideoPlayer::getCurrentFrameTexture() const {
    if (!m_hasVisibleFrame) return nullptr;
    if (m_useGPUYuv) return m_texY;  // Y plane — caller must apply YUV shader
    return m_texture;
}

cocos2d::CCTexture2D* VideoPlayer::getResolvedRGBATexture() {
    if (!m_hasVisibleFrame) return nullptr;

    // If not using GPU YUV, the texture is already RGBA
    if (!m_useGPUYuv) return m_texture;

    // Check if already resolved this frame
    if (m_resolvedRGBA && m_resolvedAtFrame == m_frameCounter) {
        return m_resolvedRGBA;
    }

    // Resolve YUV→RGBA via GPU FBO blit
    if (resolveYUVToRGBA()) {
        m_resolvedAtFrame = m_frameCounter;
        return m_resolvedRGBA;
    }

    // Fallback: return Y plane (will look wrong but won't crash)
    return m_texY;
}

bool VideoPlayer::resolveYUVToRGBA() {
    if (!m_texY || !m_texCb || !m_texCr) return false;

    int w = m_texWidth;
    int h = m_texHeight;

    // One-time setup: create FBO, sprite, cache shader + uniform locations
    if (!m_resolveRT) {
        m_blitShader = paimon::shaders::getYUVBlitShader();
        if (!m_blitShader) return false;

        m_resolveRT = cocos2d::CCRenderTexture::create(w, h);
        if (!m_resolveRT) return false;
        m_resolveRT->retain();

        cocos2d::ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
        m_resolveRT->getSprite()->getTexture()->setTexParameters(&params);

        // Create and cache the fullscreen quad sprite
        m_resolveSprite = cocos2d::CCSprite::createWithTexture(m_texY);
        if (!m_resolveSprite) { m_resolveRT->release(); m_resolveRT = nullptr; return false; }
        m_resolveSprite->retain();

        float sx = static_cast<float>(w) / m_texY->getContentSize().width;
        float sy = static_cast<float>(h) / m_texY->getContentSize().height;
        m_resolveSprite->setScale(std::max(sx, sy));
        m_resolveSprite->setAnchorPoint({0.5f, 0.5f});
        m_resolveSprite->setPosition({static_cast<float>(w) * 0.5f, static_cast<float>(h) * 0.5f});
        m_resolveSprite->setFlipY(true);
        m_resolveSprite->setShaderProgram(m_blitShader);

        // Cache uniform locations (never change for the lifetime of the shader)
        m_locCb = m_blitShader->getUniformLocationForName("u_textureCb");
        m_locCr = m_blitShader->getUniformLocationForName("u_textureCr");
        m_locY  = m_blitShader->getUniformLocationForName("u_textureY");
        m_locCS = m_blitShader->getUniformLocationForName("u_colorSpace");

        // Color space: BT.709 for HD, BT.601 for SD
        m_colorSpace = (w >= 1280 || h >= 720) ? 1.0f : 0.0f;

        // Set the sampler unit assignments ONCE — texture units 0/1/2 are
        // stable for the lifetime of this shader/sprite pairing, so there is
        // no reason to set them every frame. setUniformLocationWith1i issues
        // a glUniform1i syscall each time, which adds up at high refresh
        // rates with multiple video players visible.
        m_blitShader->use();
        if (m_locY  != -1) m_blitShader->setUniformLocationWith1i(m_locY,  0);
        if (m_locCb != -1) m_blitShader->setUniformLocationWith1i(m_locCb, 1);
        if (m_locCr != -1) m_blitShader->setUniformLocationWith1i(m_locCr, 2);
        if (m_locCS != -1) m_blitShader->setUniformLocationWith1f(m_locCS, m_colorSpace);
    }

    // Per-frame: bind textures and render to FBO. Sampler unit assignments
    // and color-space uniform are set once at init() above.
    m_blitShader->use();
    m_blitShader->setUniformsForBuiltins();

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_texCb->getName());

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_texCr->getName());

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_texY->getName());

    // Render to FBO — no clear needed, the quad covers the entire surface
    m_resolveRT->begin();
    m_resolveSprite->visit();
    m_resolveRT->end();

    m_resolvedRGBA = m_resolveRT->getSprite()->getTexture();
    return m_resolvedRGBA != nullptr;
}

cocos2d::CCGLProgram* VideoPlayer::getYUVShaderProgram() const {
    return m_useGPUYuv ? m_yuvShader : nullptr;
}

void VideoPlayer::releaseGPUResolveCache() {
    // Only meaningful for the GPU YUV path; the RGBA / CPU paths don't
    // allocate a resolve FBO.
    if (!m_useGPUYuv) return;

    // Must run on the GL/main thread.  Refuse off-thread calls so we never
    // touch GL state from the wrong context.
    if (!isOnMainThread()) {
        geode::log::warn("[VideoPlayer] releaseGPUResolveCache called off main thread - skipping");
        return;
    }

    if (m_resolveSprite) {
        m_resolveSprite->release();
        m_resolveSprite = nullptr;
    }
    if (m_resolveRT) {
        m_resolveRT->release();
        m_resolveRT = nullptr;
    }
    if (m_readbackFBO) {
        glDeleteFramebuffers(1, &m_readbackFBO);
        m_readbackFBO = 0;
    }
    // m_resolvedRGBA was a non-owning pointer into m_resolveRT's sprite -
    // drop it now that the owner is gone.
    m_resolvedRGBA = nullptr;
    m_resolvedAtFrame = 0;
    // m_blitShader is a process-wide singleton (paimon::shaders::getYUVBlitShader),
    // so we do NOT release it - just reset the cached uniform locations so
    // resolveYUVToRGBA() re-queries them on the next call (the FBO is
    // recreated lazily on demand).
    m_locCb = -1;
    m_locCr = -1;
    m_locY  = -1;
    m_locCS = -1;
}

cocos2d::CCTexture2D* VideoPlayer::getTextureCb() const {
    return m_useGPUYuv ? m_texCb : nullptr;
}

cocos2d::CCTexture2D* VideoPlayer::getTextureCr() const {
    return m_useGPUYuv ? m_texCr : nullptr;
}

bool VideoPlayer::isUsingGPUYuv() const {
    return m_useGPUYuv;
}

bool VideoPlayer::copyCurrentFramePixels(std::vector<uint8_t>& outPixels, int& outW, int& outH) const {
    if (!m_hasVisibleFrame) return false;
    if (m_useGPUYuv) {
        // GPU YUV path: resolve to RGBA via FBO and read back.
        // Use the cached resolve texture if available.
        if (m_resolvedRGBA && m_resolvedAtFrame == m_frameCounter) {
            outW = m_texWidth;
            outH = m_texHeight;
            size_t sz = static_cast<size_t>(outW) * outH * 4;
            outPixels.resize(sz);

            // Read pixels from the resolve FBO (FBO cached across calls).
            GLint oldFBO = 0;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFBO);
            if (m_readbackFBO == 0) {
                glGenFramebuffers(1, &m_readbackFBO);
            }
            glBindFramebuffer(GL_FRAMEBUFFER, m_readbackFBO);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                   m_resolvedRGBA->getName(), 0);
            glReadPixels(0, 0, outW, outH, GL_RGBA, GL_UNSIGNED_BYTE, outPixels.data());
            glBindFramebuffer(GL_FRAMEBUFFER, oldFBO);
            return true;
        }
        // No resolved texture yet — caller should call getResolvedRGBATexture() first
        // or wait for the next frame.
        return false;
    }
    if (!m_rgbaBuffer) return false;
    outW = m_texWidth;
    outH = m_texHeight;
    size_t sz = static_cast<size_t>(outW) * outH * 4;
    outPixels.assign(m_rgbaBuffer, m_rgbaBuffer + sz);
    return true;
}

int VideoPlayer::getWidth()  const { return m_decoder ? m_decoder->getWidth() : 0; }
int VideoPlayer::getHeight() const { return m_decoder ? m_decoder->getHeight() : 0; }
double VideoPlayer::getDuration() const { return m_decoder ? m_decoder->getDuration() : 0.0; }

size_t VideoPlayer::getEstimatedRAMBytes() const {
    if (!m_decoder) return 0;
    int w = m_decoder->getWidth();
    int h = m_decoder->getHeight();
    if (m_useGPUYuv) {
        // GPU path: Y + Cb + Cr textures (no RGBA buffer needed)
        size_t yTex  = static_cast<size_t>(w) * h;
        size_t uvTex = static_cast<size_t>((w + 1) / 2) * ((h + 1) / 2) * 2;
        size_t pbo   = (yTex + uvTex) * 3;  // 3-slot PBO rotation
        return yTex + uvTex + pbo;
    }
    size_t rgbaTex = static_cast<size_t>(w) * h * 4;
    size_t rgbaBuf = rgbaTex;
    // No more YUV working frame — peekFrame() gives zero-copy ring access.
    size_t pbo = rgbaTex * 3; // 3-slot PBO rotation
    return rgbaTex + rgbaBuf + pbo;
}

std::string const& VideoPlayer::getFilePath() const { return m_filePath; }

void VideoPlayer::setOnFinished(std::function<void()> cb) {
    m_onFinished = std::move(cb);
}

// ─────────────────────────────────────────────────────────────
// Audio integration — backed by the main FMOD music channel
// ─────────────────────────────────────────────────────────────
void VideoPlayer::fadeAudioIn(float duration) {
    if (!m_audioReady) return;

    auto generation = ++(*m_audioFadeGeneration);
    float targetVolume = std::clamp(m_volume > 0.0f ? m_volume : 1.0f, 0.0f, 1.0f);
    setVolume(0.0f);
    playAudioFromCurrentTime(true);

    if (!m_audioPlaying->load(std::memory_order_acquire)) {
        return;
    }

    int totalSteps = std::max(1, static_cast<int>(std::ceil(std::max(0.01f, duration) / 0.05f)));
    auto audioName = m_audioName;
    auto playingFlag = m_audioPlaying;
    auto fadeGeneration = m_audioFadeGeneration;

    auto fadeStep = std::make_shared<std::function<void(int)>>();
    *fadeStep = [generation, totalSteps, targetVolume, fadeStep, audioName, playingFlag, fadeGeneration](int step) {
        if (generation != fadeGeneration->load(std::memory_order_acquire)) return;
        auto* engine = FMODAudioEngine::sharedEngine();
        auto* bgCh = getMainBgChannel(engine);
        if (!bgCh || !currentSoundMatches(bgCh, audioName)) {
            playingFlag->store(false, std::memory_order_release);
            return;
        }

        float t = static_cast<float>(step) / static_cast<float>(totalSteps);
        bgCh->setVolume(std::clamp(targetVolume * t, 0.0f, 1.0f));
        if (step >= totalSteps) {
            return;
        }

        paimon::scheduleMainThreadDelay(0.05f, [fadeStep, step]() {
            (*fadeStep)(step + 1);
        });
    };

    (*fadeStep)(0);
}

void VideoPlayer::fadeAudioOut(float duration, std::function<void()> onComplete) {
    if (!m_audioReady) {
        if (onComplete) onComplete();
        return;
    }

    auto generation = ++(*m_audioFadeGeneration);
    auto* engine = FMODAudioEngine::sharedEngine();
    auto* bgCh = getMainBgChannel(engine);
    if (!bgCh || !currentSoundMatches(bgCh, m_audioName)) {
        m_audioPlaying->store(false, std::memory_order_release);
        if (onComplete) onComplete();
        return;
    }

    // Fast path: duration <= 0 means "stop immediately, run callback now".
    // The async ramp below would otherwise leave a 50 ms window during which
    // the bg channel is still tickling — that window is enough for
    // LevelInfoLayer's forcePlayDynamic to see videoAudioInteropState
    // unchanged and bail, leaving the level silent.  Doing it synchronously
    // closes the race.
    if (duration <= 0.0f) {
        bgCh->stop();
        m_audioPlaying->store(false, std::memory_order_release);
        if (onComplete) onComplete();
        return;
    }

    float startVol = 0.0f;
    bgCh->getVolume(&startVol);
    int totalSteps = std::max(1, static_cast<int>(std::ceil(std::max(0.01f, duration) / 0.05f)));
    auto callback = std::make_shared<std::function<void()>>(std::move(onComplete));
    auto audioName = m_audioName;
    auto playingFlag = m_audioPlaying;
    auto fadeGeneration = m_audioFadeGeneration;

    auto fadeStep = std::make_shared<std::function<void(int)>>();
    *fadeStep = [generation, totalSteps, startVol, fadeStep, callback, audioName, playingFlag, fadeGeneration](int step) {
        if (generation != fadeGeneration->load(std::memory_order_acquire)) return;
        auto* engine = FMODAudioEngine::sharedEngine();
        auto* bgCh = getMainBgChannel(engine);
        if (!bgCh || !currentSoundMatches(bgCh, audioName)) {
            playingFlag->store(false, std::memory_order_release);
            if (*callback) {
                auto cb = std::move(*callback);
                cb();
            }
            return;
        }

        float t = static_cast<float>(step) / static_cast<float>(totalSteps);
        bgCh->setVolume(std::clamp(startVol * (1.0f - t), 0.0f, 1.0f));
        if (step >= totalSteps) {
            bgCh->stop();
            playingFlag->store(false, std::memory_order_release);
            if (*callback) {
                auto cb = std::move(*callback);
                cb();
            }
            return;
        }

        paimon::scheduleMainThreadDelay(0.05f, [fadeStep, step]() {
            (*fadeStep)(step + 1);
        });
    };

    (*fadeStep)(0);
}
bool VideoPlayer::hasAudio() const { return m_audioReady; }
bool VideoPlayer::isAudioPlaying() const { return m_audioPlaying->load(std::memory_order_acquire); }
bool VideoPlayer::didAudioInitFail() const { return m_createOptions.enableAudio && m_audioInitFailed; }

void syncVideoAudioVolume() {
    auto* engine = FMODAudioEngine::sharedEngine();
    auto* bgCh = getMainBgChannel(engine);
    if (!engine || !bgCh) return;
    float target = std::clamp(engine->m_musicVolume, 0.0f, 1.0f);
    bgCh->setVolume(target);
}

} // namespace paimon::video
