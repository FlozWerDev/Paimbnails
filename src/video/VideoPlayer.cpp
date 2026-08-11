#include "VideoDecoder.hpp"
#include "VideoPlayer.hpp"
#include "../utils/GLSLLoader.hpp"
#include <Geode/loader/Log.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/cocos/misc_nodes/CCRenderTexture.h>
#include "../utils/MainThreadDelay.hpp"
#include "../core/RuntimeLifecycle.hpp"
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

#include <libyuv/convert_argb.h>

#include <Geode/cocos/CCDirector.h>

namespace paimon::video {

namespace {

// Backwards PTS jump larger than this means the decoder rewound for a loop.
constexpr double kLoopWrapGuard = 0.5;

static std::thread::id s_mainThreadId;
static std::once_flag s_mainThreadIdInit;
static std::atomic<bool> s_mainThreadIdBound{false};

static void ensureMainThreadId() {
    if (s_mainThreadIdBound.load(std::memory_order_acquire)) return;
    std::call_once(s_mainThreadIdInit, []() {
        s_mainThreadId = std::this_thread::get_id();
    });
}

static bool isOnMainThread() {
    return std::this_thread::get_id() == s_mainThreadId;
}

}

void VideoPlayer::bindMainThreadId() {
    s_mainThreadId = std::this_thread::get_id();
    s_mainThreadIdBound.store(true, std::memory_order_release);
}

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
    if (m_gpuInitGate) {
        m_gpuInitGate->fetch_add(1, std::memory_order_release);
    }
    m_gpuInitGeneration.fetch_add(1, std::memory_order_release);
    stopAudio(true);

    if (m_decoder) {
        m_decoder->stopDecoding();
        if (!m_decoder->isTerminal()) {
            m_decoder.reset();
        } else {
            (void)m_decoder.release();
        }
    }

    if (!isOnMainThread()) {
        geode::log::warn("[VideoPlayer] Destructor called off main thread - deferring GL cleanup");
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
        // PBO shutdown stays on the GL thread; context teardown reclaims them.
        return;
    }
    
    auto* director = cocos2d::CCDirector::get();

    delete[] m_rgbaBuffer;
    m_rgbaBuffer = nullptr;
    
    try {
        m_pboUploader.shutdown();
        m_pboUploaderYUV.shutdown();
    } catch (...) {}
    
    if (m_texture) {
        if (director && director->getOpenGLView()) {
            m_texture->release();
        }
        m_texture = nullptr;
    }

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


    int w = m_decoder->getWidth();
    int h = m_decoder->getHeight();


    m_texWidth = w;
    m_texHeight = h;


    if (!initAudio(options) && options.enableAudio) {
        geode::log::warn("[VideoPlayer] Audio init failed for {}", videoPath);
    }

    return true;
}

bool VideoPlayer::initAudio(const VideoPlayerCreateOptions& options) {
    m_audioInitFailed = false;
    m_audio.reset();

    if (!options.enableAudio || m_filePath.empty()) {
        return true;
    }

    m_audio = VideoAudioTrack::create(m_filePath);
    if (!m_audio) {
        m_audioInitFailed = true;
        return false;
    }
    m_audio->setLoop(m_loop);
    m_audio->setVolume(m_volume);
    return true;
}

void VideoPlayer::playAudioFromCurrentTime(bool) {
    if (!m_audio) return;
    m_audio->play(m_playbackTime);
}

void VideoPlayer::pauseAudio() {
    if (m_audio) m_audio->pause();
}

void VideoPlayer::stopAudio(bool stopChannel) {
    if (!m_audio) return;
    ++(*m_audioFadeGeneration);
    if (stopChannel) {
        m_audio->stop();
    } else {
        m_audio->pause();
    }
}

void VideoPlayer::initTexture(int width, int height) {
    if (m_texture) return;

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
        // The player owns the texture; shared-video sprites may outlive it.
        glBindTexture(GL_TEXTURE_2D, m_texture->getName());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    delete[] data;
}

void VideoPlayer::initYUVTextures(int width, int height) {
    if (m_texY) return;

    int uvW = (width + 1) / 2;
    int uvH = (height + 1) / 2;

    auto createLuminanceTex = [](int w, int h) -> cocos2d::CCTexture2D* {
        auto* tex = new (std::nothrow) cocos2d::CCTexture2D();
        if (!tex) return nullptr;
        auto* data = new (std::nothrow) uint8_t[w * h]();
        if (!data) { tex->release(); return nullptr; }
        // I8 keeps the plane in .r for the YUV shader.
        tex->initWithData(data, cocos2d::kCCTexture2DPixelFormat_I8, w, h,
                          cocos2d::CCSize(static_cast<float>(w), static_cast<float>(h)));
        delete[] data;
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
        if (m_texY)  { m_texY->release();  m_texY = nullptr; }
        if (m_texCb) { m_texCb->release(); m_texCb = nullptr; }
        if (m_texCr) { m_texCr->release(); m_texCr = nullptr; }
        geode::log::warn("[VideoPlayer] Failed to create YUV textures, falling back to CPU path");
        return;
    }

    m_yuvShader = paimon::shaders::getYUVShader();
    if (!m_yuvShader) {
        m_texY->release();  m_texY = nullptr;
        m_texCb->release(); m_texCb = nullptr;
        m_texCr->release(); m_texCr = nullptr;
        geode::log::warn("[VideoPlayer] YUV shader not available, falling back to CPU path");
        return;
    }

    m_useGPUYuv = true;
    geode::log::info("[VideoPlayer] GPU YUV->RGB path active ({}x{})", width, height);
}

bool VideoPlayer::uploadFrameGPU(const IVideoDecoder::Frame& frame) {
    if (!m_texY || !m_texCb || !m_texCr) return false;

    int w = frame.width;
    int h = frame.height;
    int uvW = (w + 1) / 2;
    int uvH = (h + 1) / 2;

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
    }

    auto uploadPlane = [](GLuint texId, const uint8_t* data, int stride, int planeW, int planeH) {
        glBindTexture(GL_TEXTURE_2D, texId);
        if (stride == planeW) {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, planeW, planeH,
                            GL_LUMINANCE, GL_UNSIGNED_BYTE, data);
        } else {
            thread_local std::vector<uint8_t> scratch;
            size_t needed = static_cast<size_t>(planeW) * planeH;
            scratch.resize(needed);
            for (int row = 0; row < planeH; ++row) {
                std::memcpy(scratch.data() + row * planeW, data + row * stride, planeW);
            }
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, planeW, planeH,
                            GL_LUMINANCE, GL_UNSIGNED_BYTE, scratch.data());
        }
        glBindTexture(GL_TEXTURE_2D, 0);
    };

    uploadPlane(m_texY->getName(),  frame.planeY,  frame.strideY,  w, h);
    uploadPlane(m_texCb->getName(), frame.planeCb, frame.strideCb, uvW, uvH);
    uploadPlane(m_texCr->getName(), frame.planeCr, frame.strideCr, uvW, uvH);

    m_hasVisibleFrame = true;
    return true;
}
// libyuv picks the best SIMD path per CPU. ABGR in libyuv byte order is
// R,G,B,A in memory, which is what the GL_RGBA upload expects.
static inline void yuvToRgba(const uint8_t* planeY, int strideY,
                              const uint8_t* planeCb, int strideCb,
                              const uint8_t* planeCr, int strideCr,
                              uint8_t* rgba, int width, int height) {
    // Width catches widescreen videos whose height is below 720.
    auto convert = (width >= 1280 || height >= 720) ? libyuv::H420ToABGR
                                                    : libyuv::I420ToABGR;
    convert(planeY, strideY, planeCb, strideCb, planeCr, strideCr,
            rgba, width * 4, width, height);
}

// Build GL resources before the first upload.
void VideoPlayer::prepareGPUPipeline() {
    if (!isOnMainThread()) return;
    if (m_texWidth <= 0 || m_texHeight <= 0) return;

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
            if (!m_rgbaBuffer) {
                m_rgbaBuffer = new (std::nothrow) uint8_t[
                    static_cast<size_t>(m_texWidth) * m_texHeight * 4];
            }
            if (m_rgbaBuffer) {
                if (!m_pboUploader.init(m_texWidth * m_texHeight * 4)) {
                    geode::log::warn("VideoPlayer: PBO init failed, falling back to direct upload");
                }
            } else {
                // Without staging data there is no CPU-upload fallback.
                geode::log::warn("[VideoPlayer] RGBA staging buffer alloc failed in pre-warm "
                                 "({}x{}), will retry on first frame", m_texWidth, m_texHeight);
            }
        }
    }

    if (!m_useGPUYuv && !m_texture) {
        initTexture(m_texWidth, m_texHeight);
    }

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

bool VideoPlayer::uploadFrame(const IVideoDecoder::Frame& frame) {
    if (!m_pboInitAttempted) {
        prepareGPUPipeline();
    }

    if (m_useGPUYuv) {
        return uploadFrameGPU(frame);
    }

    if (!m_texture) {
        initTexture(m_texWidth, m_texHeight);
        if (!m_texture) return false;
    }

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

    // Defer conversion during a busy frame.
    auto* director = cocos2d::CCDirector::get();
    if (!director) return false;
    float dt = director->getDeltaTime();
    bool isFrameLag = dt > 0.020f;  // > 20ms = severe lag @ 60fps

    if (isFrameLag && !m_hasVisibleFrame) {
    return false;
    }

    if (m_pboUploader.isInitialized()) {
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

        yuvToRgba(frame.planeY, frame.strideY,
                  frame.planeCb, frame.strideCb,
                  frame.planeCr, frame.strideCr,
                  m_rgbaBuffer, w, h);
        if (!m_pboUploader.uploadRGBA(m_texture->getName(), m_rgbaBuffer, w, h)) {
            return false;
        }
        m_hasVisibleFrame = true;
        return true;
    }

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

void VideoPlayer::update(float dt) {
    if (!m_playing || !m_decoder) return;
    auto* director = cocos2d::CCDirector::get();
    if (!director) return;

    // Advance the decoder once per director frame.
    auto currentFrame = director->getTotalFrames();
    if (currentFrame == m_lastUpdateFrame) return;
    m_lastUpdateFrame = currentFrame;

    // Anchor the clock to the first uploaded PTS so decoder/GPU warm-up does not
    // make the first update skip a burst of frames.
    if (!m_hasVisibleFrame) {
        if (!m_decoderStalled && !m_pendingUpload) {
            m_timeSincePlay += static_cast<double>(dt);
            if (m_timeSincePlay > 5.0) {
                m_decoderStalled = true;
                geode::log::warn("[VideoPlayer] Decoder stall detected - no frame produced in 5s, "
                                 "stopping playback: {}", m_filePath);
                m_playing = false;
                if (m_decoder) m_decoder->stopDecoding();
                if (m_onFinished) m_onFinished();
                return;
            }
        }

        if (m_pendingUpload) {
            if (retryUploadFromRgbaBuffer()) {
                m_pendingUpload = false;
                m_timeSinceLastUpload = 0.0;
                ++m_frameCounter;
            }
            return;
        }

        if (const IVideoDecoder::Frame* f = m_decoder->peekFrame()) {
            double firstPTS = f->pts;
            bool ok = uploadFrame(*f);
            m_decoder->releaseFrame();
            if (ok) {
                m_playbackTime = firstPTS;
                m_timeSinceLastUpload = 0.0;
                ++m_frameCounter;
            } else {
                m_pendingUpload = true;
            }
        }
        return;
    }

    // Cap clock advance after a hitch.
    double advance = std::min(static_cast<double>(dt), 0.1);
    m_timeSinceLastUpload += advance;

    // Slave video to the audio clock so drift is absorbed by dropping frames
    // instead of re-seeking the channel, which is audible.
    double audioPos = m_audio ? m_audio->positionSeconds() : -1.0;
    if (audioPos >= 0.0 && m_audio->isPlaying()) {
        m_playbackTime = audioPos;
    } else {
        m_playbackTime += advance;
    }

    double minInterval = 1.0 / std::max(m_targetFPS, 1);

    if (m_decoderLoops) {
        double headPTS = m_decoder->peekNextPTS();
        if (headPTS < DBL_MAX && headPTS + kLoopWrapGuard < m_playbackTime) {
            m_playbackTime = headPTS;
            m_timeSinceLastUpload = minInterval;
            playAudioFromCurrentTime(true);
        }
    }

    if (m_pendingUpload) {
        if (retryUploadFromRgbaBuffer()) {
            m_pendingUpload = false;
            ++m_frameCounter;
            m_timeSinceLastUpload = 0.0;
        }
    }

    bool timeToProcess = m_timeSinceLastUpload >= minInterval;

    if (timeToProcess && !m_pendingUpload) {
        while (true) {
            double headPTS = m_decoder->peekNextPTS();
            if (headPTS > m_playbackTime) break;

            double nextPTS = m_decoder->peekSecondPTS();
            if (nextPTS <= m_playbackTime) {
                if (!m_decoder->skipFrame()) break;
                continue;
            }
            const IVideoDecoder::Frame* f = m_decoder->peekFrame();
            if (!f) break;
            bool ok = uploadFrame(*f);
            m_decoder->releaseFrame();

            if (ok) {
                ++m_frameCounter;
                m_timeSinceLastUpload = 0.0;
            } else {
                m_pendingUpload = true;
            }
            break;
        }
    } else if (!m_pendingUpload) {
        while (m_decoder->peekNextPTS() <= m_playbackTime) {
            if (!m_decoder->skipFrame()) break;
        }
    }

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
}

void VideoPlayer::play() {
    if (m_playing) return;
    if (!m_decoder || m_decoder->isTerminal()) return;
    m_playing = true;
    m_timeSincePlay = 0.0;
    m_decoderStalled = false;
    if (m_decoder) m_decoder->startDecoding();
    
    if (!m_pboInitAttempted && m_texWidth > 0 && m_texHeight > 0) {
        auto gate = m_gpuInitGate;
        if (!gate) {
            gate = std::make_shared<std::atomic<uint64_t>>(0);
            m_gpuInitGate = gate;
        }
        uint64_t const gen = gate->load(std::memory_order_acquire);
        geode::Loader::get()->queueInMainThread([this, gate, gen]() {
            if (paimon::isRuntimeShuttingDown()) return;
            if (gen != gate->load(std::memory_order_acquire)) return;
            if (m_playing && !m_pboInitAttempted) {
                prepareGPUPipeline();
            }
        });
    }
    
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
    
    if (m_decoder->isFinished() || m_decoder->peekNextPTS() >= DBL_MAX) {
        m_decoder->seekTo(0.0);
        m_playbackTime = 0.0;
        m_hasVisibleFrame = false;
        if (m_pboUploader.isInitialized()) {
            m_pboUploader.clearFences();
        }
        if (m_pboUploaderYUV.isInitialized()) {
            m_pboUploaderYUV.clearFences();
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
    if (m_gpuInitGate) {
        m_gpuInitGate->fetch_add(1, std::memory_order_release);
    }
    m_gpuInitGeneration.fetch_add(1, std::memory_order_release);
    m_playing = false;
    m_pendingUpload = false;
    m_timeSinceLastUpload = 0.0;
    if (m_decoder) {
        m_decoder->stopDecoding();
        if (m_decoder->isTerminal()) {
            (void)m_decoder.release();
        }
    }
    m_playbackTime = 0.0;
    stopAudio(true);
}

void VideoPlayer::setLoop(bool loop) {
    m_loop = loop;
    m_decoderLoops = m_decoder && m_decoder->setLooping(loop);
    if (m_audio) m_audio->setLoop(loop);
}
void VideoPlayer::setVolume(float volume) {
    m_volume = std::clamp(volume, 0.0f, 1.0f);
    if (m_audio) m_audio->setVolume(m_volume);
}
void VideoPlayer::setTargetFPS(int fps) { m_targetFPS = fps; }

bool VideoPlayer::isPlaying() const { return m_playing; }
bool VideoPlayer::hasVisibleFrame() const { return m_hasVisibleFrame; }
bool VideoPlayer::isTerminal() const { return m_decoder && m_decoder->isTerminal(); }
uint64_t VideoPlayer::getFrameCounter() const { return m_frameCounter; }

cocos2d::CCTexture2D* VideoPlayer::getCurrentFrameTexture() const {
    if (!m_hasVisibleFrame) return nullptr;
    if (m_useGPUYuv) return m_texY;  // caller must apply the YUV shader
    return m_texture;
}

cocos2d::CCTexture2D* VideoPlayer::getResolvedRGBATexture() {
    if (!m_hasVisibleFrame) return nullptr;

    if (!m_useGPUYuv) return m_texture;

    if (m_resolvedRGBA && m_resolvedAtFrame == m_frameCounter) {
        return m_resolvedRGBA;
    }

    if (resolveYUVToRGBA()) {
        m_resolvedAtFrame = m_frameCounter;
        return m_resolvedRGBA;
    }

    return m_texY;
}

bool VideoPlayer::resolveYUVToRGBA() {
    if (!m_texY || !m_texCb || !m_texCr) return false;

    int w = m_texWidth;
    int h = m_texHeight;

    if (!m_resolveRT) {
        m_blitShader = paimon::shaders::getYUVBlitShader();
        if (!m_blitShader) return false;

        m_resolveRT = cocos2d::CCRenderTexture::create(w, h);
        if (!m_resolveRT) return false;
        m_resolveRT->retain();

        cocos2d::ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
        m_resolveRT->getSprite()->getTexture()->setTexParameters(&params);

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

        m_locCb = m_blitShader->getUniformLocationForName("u_textureCb");
        m_locCr = m_blitShader->getUniformLocationForName("u_textureCr");
        m_locY  = m_blitShader->getUniformLocationForName("u_textureY");
        m_locCS = m_blitShader->getUniformLocationForName("u_colorSpace");

        m_colorSpace = (w >= 1280 || h >= 720) ? 1.0f : 0.0f;

        // Sampler units stay fixed; avoid setting them every frame.
        m_blitShader->use();
        if (m_locY  != -1) m_blitShader->setUniformLocationWith1i(m_locY,  0);
        if (m_locCb != -1) m_blitShader->setUniformLocationWith1i(m_locCb, 1);
        if (m_locCr != -1) m_blitShader->setUniformLocationWith1i(m_locCr, 2);
        if (m_locCS != -1) m_blitShader->setUniformLocationWith1f(m_locCS, m_colorSpace);
    }

    m_blitShader->use();
    m_blitShader->setUniformsForBuiltins();

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_texCb->getName());

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_texCr->getName());

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_texY->getName());

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
    if (!m_useGPUYuv) return;

    // GL releases stay on the main thread.
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
    m_resolvedRGBA = nullptr;
    m_resolvedAtFrame = 0;
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
        if (m_resolvedRGBA && m_resolvedAtFrame == m_frameCounter) {
            outW = m_texWidth;
            outH = m_texHeight;
            size_t sz = static_cast<size_t>(outW) * outH * 4;
            outPixels.resize(sz);

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
        size_t yTex  = static_cast<size_t>(w) * h;
        size_t uvTex = static_cast<size_t>((w + 1) / 2) * ((h + 1) / 2) * 2;
        size_t pbo   = (yTex + uvTex) * 3;
        return yTex + uvTex + pbo;
    }
    size_t rgbaTex = static_cast<size_t>(w) * h * 4;
    size_t rgbaBuf = rgbaTex;
    size_t pbo = rgbaTex * 3;
    return rgbaTex + rgbaBuf + pbo;
}

std::string const& VideoPlayer::getFilePath() const { return m_filePath; }

void VideoPlayer::setOnFinished(std::function<void()> cb) {
    m_onFinished = std::move(cb);
}

void VideoPlayer::fadeAudioIn(float duration) {
    if (!m_audio) return;

    auto generation = ++(*m_audioFadeGeneration);
    float targetVolume = std::clamp(m_volume > 0.0f ? m_volume : 1.0f, 0.0f, 1.0f);
    m_audio->setVolume(0.0f);
    m_audio->play(m_playbackTime);

    if (!m_audio->isPlaying()) return;

    int totalSteps = std::max(1, static_cast<int>(std::ceil(std::max(0.01f, duration) / 0.05f)));
    auto fadeGeneration = m_audioFadeGeneration;
    auto* track = m_audio.get();

    auto fadeStep = std::make_shared<std::function<void(int)>>();
    std::weak_ptr<std::function<void(int)>> weakFadeStep = fadeStep;
    *fadeStep = [generation, totalSteps, targetVolume, weakFadeStep, track, fadeGeneration](int step) {
        // A newer fade (or a stop) bumps the generation and owns the track from then on.
        if (generation != fadeGeneration->load(std::memory_order_acquire)) return;

        float t = static_cast<float>(step) / static_cast<float>(totalSteps);
        track->setVolume(std::clamp(targetVolume * t, 0.0f, 1.0f));
        if (step >= totalSteps) return;

        if (auto strong = weakFadeStep.lock()) {
            paimon::scheduleMainThreadDelay(0.05f, [strong, step]() {
                (*strong)(step + 1);
            });
        }
    };

    (*fadeStep)(0);
}

void VideoPlayer::fadeAudioOut(float duration, std::function<void()> onComplete) {
    if (!m_audio || !m_audio->isPlaying()) {
        if (onComplete) onComplete();
        return;
    }

    auto generation = ++(*m_audioFadeGeneration);

    if (duration <= 0.0f) {
        m_audio->stop();
        if (onComplete) onComplete();
        return;
    }

    float startVol = m_audio->getVolume();
    int totalSteps = std::max(1, static_cast<int>(std::ceil(std::max(0.01f, duration) / 0.05f)));
    auto callback = std::make_shared<std::function<void()>>(std::move(onComplete));
    auto fadeGeneration = m_audioFadeGeneration;
    auto* track = m_audio.get();

    auto fadeStep = std::make_shared<std::function<void(int)>>();
    std::weak_ptr<std::function<void(int)>> weakFadeStep = fadeStep;
    *fadeStep = [generation, totalSteps, startVol, weakFadeStep, callback, track, fadeGeneration](int step) {
        if (generation != fadeGeneration->load(std::memory_order_acquire)) return;

        float t = static_cast<float>(step) / static_cast<float>(totalSteps);
        track->setVolume(std::clamp(startVol * (1.0f - t), 0.0f, 1.0f));
        if (step >= totalSteps) {
            track->stop();
            if (*callback) {
                auto cb = std::move(*callback);
                cb();
            }
            return;
        }

        if (auto strong = weakFadeStep.lock()) {
            paimon::scheduleMainThreadDelay(0.05f, [strong, step]() {
                (*strong)(step + 1);
            });
        }
    };

    (*fadeStep)(0);
}

bool VideoPlayer::hasAudio() const { return m_audio != nullptr; }
bool VideoPlayer::isAudioPlaying() const { return m_audio && m_audio->isPlaying(); }
bool VideoPlayer::didAudioInitFail() const { return m_createOptions.enableAudio && m_audioInitFailed; }

void syncVideoAudioVolume() {
    VideoAudioTrack::syncAllVolumes();
}

}
