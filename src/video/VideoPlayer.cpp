#include "VideoDecoder.hpp"
#include "VideoPlayer.hpp"
#include <Geode/loader/Log.hpp>
#include <cfloat>   // DBL_MAX
#include <cmath>

#if defined(GEODE_IS_WINDOWS)
#include <gl/gl.h>
#elif defined(GEODE_IS_ANDROID)
#include <GLES2/gl2.h>
#elif defined(GEODE_IS_IOS)
#include <OpenGLES/ES2/gl.h>
#elif defined(GEODE_IS_MACOS)
#include <OpenGL/gl.h>
#endif

#include <Geode/cocos/CCDirector.h>

namespace paimon::video {

// ─────────────────────────────────────────────────────────────
// Factory
// ─────────────────────────────────────────────────────────────
std::unique_ptr<VideoPlayer> VideoPlayer::create(const std::string& videoPath) {
    auto ret = std::unique_ptr<VideoPlayer>(new (std::nothrow) VideoPlayer());
    if (ret && ret->init(videoPath)) {
        return ret;
    }
    return nullptr;
}

std::unique_ptr<VideoPlayer> VideoPlayer::create(const std::string& videoPath, const VideoPlayerCreateOptions& /*options*/) {
    return create(videoPath);
}

VideoPlayer::~VideoPlayer() {
    if (m_decoder) {
        m_decoder->stopDecoding();
        m_decoder.reset();
    }
    // Restore FPS — director may be nullptr during app shutdown
    auto* director = cocos2d::CCDirector::sharedDirector();
    if (director) {
        director->setAnimationInterval(m_originalInterval);
    }
    VideoFrame::freeAligned(m_workingFrame.planeY);
    VideoFrame::freeAligned(m_workingFrame.planeCb);
    VideoFrame::freeAligned(m_workingFrame.planeCr);
    delete[] m_rgbaBuffer;
    m_rgbaBuffer = nullptr;
    m_pboUploader.shutdown();
    // Release the texture we retained in initTexture()
    if (m_texture) {
        m_texture->release();
        m_texture = nullptr;
    }
}

bool VideoPlayer::init(const std::string& videoPath) {
    m_filePath = videoPath;
    m_decoder = IVideoDecoder::create(videoPath);
    if (!m_decoder) {
        geode::log::warn("VideoPlayer: no decoder for {}", videoPath);
        return false;
    }

    // Save the game's current animation interval so we can restore it later
    m_originalInterval = cocos2d::CCDirector::sharedDirector()->getAnimationInterval();

    int w = m_decoder->getWidth();
    int h = m_decoder->getHeight();

    // Allocate working frame buffers (consumed from ring buffer into here)
    int uvH  = (h + 1) / 2;
    int uvW  = (w + 1) / 2;
    int alignedStrideY  = VideoFrame::alignedStride(w);
    int alignedStrideCb = VideoFrame::alignedStride(uvW);
    int alignedStrideCr = VideoFrame::alignedStride(uvW);

    m_workingFrame.planeY  = VideoFrame::allocAligned(VideoFrame::alignedSize(w, h));
    m_workingFrame.planeCb = VideoFrame::allocAligned(VideoFrame::alignedSize(uvW, uvH));
    m_workingFrame.planeCr = VideoFrame::allocAligned(VideoFrame::alignedSize(uvW, uvH));
    if (!m_workingFrame.planeY || !m_workingFrame.planeCb || !m_workingFrame.planeCr)
        return false;
    m_workingFrame.strideY  = alignedStrideY;
    m_workingFrame.strideCb = alignedStrideCb;
    m_workingFrame.strideCr = alignedStrideCr;
    m_workingFrame.width    = w;
    m_workingFrame.height   = h;

    // Allocate RGBA conversion buffer
    m_rgbaBuffer = new (std::nothrow) uint8_t[w * h * 4];
    if (!m_rgbaBuffer) return false;

    m_texWidth = w;
    m_texHeight = h;

    // PBO init deferred to first uploadFrame() — must be on GL thread

    return true;
}

// ─────────────────────────────────────────────────────────────
// Texture init (single RGBA texture, backward compatible)
// ─────────────────────────────────────────────────────────────
void VideoPlayer::initTexture(int width, int height) {
    if (m_texture) return;

    auto* data = new (std::nothrow) uint8_t[width * height * 4]();
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
// YUV→RGBA conversion (BT.601)
// ─────────────────────────────────────────────────────────────
static inline uint8_t clamp8(int v) {
    return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
}

static void yuvToRgba(const uint8_t* planeY, int strideY,
                       const uint8_t* planeCb, int strideCb,
                       const uint8_t* planeCr, int strideCr,
                       uint8_t* rgba, int width, int height) {
    // Process 2 horizontal pixels at a time — both share the same UV sample,
    // halving UV memory reads and chroma math.
    int pairWidth = width & ~1;  // round down to even
    for (int y = 0; y < height; ++y) {
        const uint8_t* rowY  = planeY  + y * strideY;
        const uint8_t* rowCb = planeCb + (y >> 1) * strideCb;
        const uint8_t* rowCr = planeCr + (y >> 1) * strideCr;
        uint8_t* out = rgba + y * width * 4;

        for (int x = 0; x < pairWidth; x += 2) {
            int uvX = x >> 1;
            int Cb = rowCb[uvX] - 128;
            int Cr = rowCr[uvX] - 128;

            // Pre-compute chroma contributions (shared by both pixels)
            int rAdd = (1436 * Cr) >> 10;
            int gSub = (352 * Cb + 731 * Cr) >> 10;
            int bAdd = (1815 * Cb) >> 10;

            int Y0 = rowY[x];
            out[x * 4 + 0] = clamp8(Y0 + rAdd);
            out[x * 4 + 1] = clamp8(Y0 - gSub);
            out[x * 4 + 2] = clamp8(Y0 + bAdd);
            out[x * 4 + 3] = 255;

            int Y1 = rowY[x + 1];
            out[x * 4 + 4] = clamp8(Y1 + rAdd);
            out[x * 4 + 5] = clamp8(Y1 - gSub);
            out[x * 4 + 6] = clamp8(Y1 + bAdd);
            out[x * 4 + 7] = 255;
        }

        // Handle odd trailing pixel
        if (pairWidth < width) {
            int x = pairWidth;
            int Cb = rowCb[x >> 1] - 128;
            int Cr = rowCr[x >> 1] - 128;
            int Y  = rowY[x];
            out[x * 4 + 0] = clamp8(Y + ((1436 * Cr) >> 10));
            out[x * 4 + 1] = clamp8(Y - ((352 * Cb + 731 * Cr) >> 10));
            out[x * 4 + 2] = clamp8(Y + ((1815 * Cb) >> 10));
            out[x * 4 + 3] = 255;
        }
    }
}

// ─────────────────────────────────────────────────────────────
// Upload frame: YUV→RGBA conversion + PBO async upload
// ─────────────────────────────────────────────────────────────
bool VideoPlayer::uploadFrame(const IVideoDecoder::Frame& frame) {
    // Lazily create texture on first upload (must be on main/GL thread)
    if (!m_texture) {
        initTexture(m_texWidth, m_texHeight);
        if (!m_texture) return false;
    }

    // Lazily init PBO uploader on first upload (GL context guaranteed here)
    if (!m_pboInitAttempted) {
        m_pboInitAttempted = true;
        if (!m_pboUploader.init(m_texWidth * m_texHeight * 4)) {
            geode::log::warn("VideoPlayer: PBO init failed, falling back to direct upload");
        }
    }

    int w = frame.width;
    int h = frame.height;

    // CPU YUV→RGBA conversion — single-threaded for correctness
    yuvToRgba(frame.planeY, frame.strideY,
              frame.planeCb, frame.strideCb,
              frame.planeCr, frame.strideCr,
              m_rgbaBuffer, w, h);

    if (m_pboUploader.isInitialized()) {
        // Upload RGBA via PBO — with fence sync, returns false if GPU busy
        if (!m_pboUploader.uploadRGBA(m_texture->getName(), m_rgbaBuffer, w, h)) {
            // All PBO slots still in use by GPU — skip this frame,
            // it will be retried on next update() call
            return false;
        }
    } else {
        // Fallback: direct synchronous upload (slower but always works)
        glBindTexture(GL_TEXTURE_2D, m_texture->getName());
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h,
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
    auto currentFrame = cocos2d::CCDirector::sharedDirector()->getTotalFrames();
    if (currentFrame == m_lastUpdateFrame) return;
    m_lastUpdateFrame = currentFrame;

    m_playbackTime += static_cast<double>(dt);
    m_timeSinceLastUpload += static_cast<double>(dt);

    double minInterval = 1.0 / std::max(m_targetFPS, 1);

    // Retry a pending upload immediately (data already converted, just GPU was busy)
    if (m_pendingUpload) {
        if (uploadFrame(m_workingFrame)) {
            m_pendingUpload = false;
            ++m_frameCounter;
            m_timeSinceLastUpload = 0.0;
        }
    }

    bool timeToProcess = m_timeSinceLastUpload >= minInterval;

    if (timeToProcess && !m_pendingUpload) {
        bool consumedNewFrame = false;
        double nextPTS = m_decoder->peekNextPTS();

        while (nextPTS <= m_playbackTime) {
            if (m_decoder->consumeFrame(m_workingFrame)) {
                consumedNewFrame = true;
                nextPTS = m_decoder->peekNextPTS();
            } else {
                break;
            }
        }

        if (consumedNewFrame) {
            if (!uploadFrame(m_workingFrame)) {
                m_pendingUpload = true;
            } else {
                ++m_frameCounter;
                m_timeSinceLastUpload = 0.0;
            }
        }
    } else if (!m_pendingUpload) {
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
        } else {
            m_playing = false;
            cocos2d::CCDirector::sharedDirector()->setAnimationInterval(m_originalInterval);
            if (m_onFinished) m_onFinished();
        }
    }
}

// ─────────────────────────────────────────────────────────────
// Controls
// ─────────────────────────────────────────────────────────────
void VideoPlayer::play() {
    if (m_playing) return;
    m_playing = true;
    if (m_decoder) m_decoder->startDecoding();
}

void VideoPlayer::pause() {
    m_playing = false;
    m_pendingUpload = false;
    if (m_decoder) m_decoder->stopDecoding();
}

void VideoPlayer::resume() {
    if (!m_decoder) return;
    double nextPTS = m_decoder->peekNextPTS();
    if (nextPTS < DBL_MAX) {
        m_playbackTime = nextPTS;
    } else {
        m_playbackTime = 0.0;
    }
    m_pendingUpload = false;
    m_timeSinceLastUpload = 0.0;
}

void VideoPlayer::stop() {
    m_playing = false;
    m_pendingUpload = false;
    m_timeSinceLastUpload = 0.0;
    if (m_decoder) {
        bool joined = m_decoder->stopDecoding();
        // Only seek back to the start when the decode thread was fully stopped.
        // If the thread was detached (timedJoin timed out), calling seekTo()
        // would race with the still-running thread accessing m_reader / m_codec.
        if (joined) {
            m_decoder->seekTo(0.0);
        }
    }
    m_playbackTime = 0.0;
    cocos2d::CCDirector::sharedDirector()->setAnimationInterval(m_originalInterval);
}

void VideoPlayer::forceStop() {
    m_playing = false;
    m_pendingUpload = false;
    m_timeSinceLastUpload = 0.0;
    if (m_decoder) {
        m_decoder->stopDecoding();
        // No seekTo — avoids SetCurrentPosition deadlock during shutdown
    }
    m_playbackTime = 0.0;
    // No setAnimationInterval — director may be shutting down
}

void VideoPlayer::setLoop(bool loop) { m_loop = loop; }
void VideoPlayer::setVolume(float volume) { m_volume = volume; }
void VideoPlayer::setTargetFPS(int fps) { m_targetFPS = fps; }

bool VideoPlayer::isPlaying() const { return m_playing; }
bool VideoPlayer::hasVisibleFrame() const { return m_hasVisibleFrame; }
uint64_t VideoPlayer::getFrameCounter() const { return m_frameCounter; }

cocos2d::CCTexture2D* VideoPlayer::getCurrentFrameTexture() const {
    return m_hasVisibleFrame ? m_texture : nullptr;
}

bool VideoPlayer::copyCurrentFramePixels(std::vector<uint8_t>& outPixels, int& outW, int& outH) const {
    if (!m_rgbaBuffer || !m_hasVisibleFrame) return false;
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
    size_t rgbaTex = static_cast<size_t>(w) * h * 4;
    size_t rgbaBuf = rgbaTex;
    size_t yuvWork = static_cast<size_t>(w) * h + 2 * ((w + 1) / 2) * ((h + 1) / 2);
    size_t pbo = rgbaTex * 3; // triple-buffered
    return rgbaTex + rgbaBuf + yuvWork + pbo;
}

std::string const& VideoPlayer::getFilePath() const { return m_filePath; }

void VideoPlayer::setOnFinished(std::function<void()> cb) {
    m_onFinished = std::move(cb);
}

// ─────────────────────────────────────────────────────────────
// Audio stubs — kept for LayerBackgroundManager API compatibility
// ─────────────────────────────────────────────────────────────
void VideoPlayer::fadeAudioIn(float /*duration*/) {}
void VideoPlayer::fadeAudioOut(float /*duration*/, std::function<void()> onComplete) {
    if (onComplete) onComplete();
}
bool VideoPlayer::hasAudio() const { return false; }
bool VideoPlayer::isAudioPlaying() const { return false; }
bool VideoPlayer::didAudioInitFail() const { return false; }

void syncVideoAudioVolume() {
    // Stub — no audio in current implementation
}

} // namespace paimon::video
