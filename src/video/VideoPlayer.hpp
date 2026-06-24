#pragma once

#include "VideoDecoder.hpp"
#include "PBOUploader.hpp"
#include <Geode/cocos/textures/CCTexture2D.h>
#include <Geode/cocos/misc_nodes/CCRenderTexture.h>
#include <Geode/cocos/include/ccTypes.h>
#include <functional>
#include <memory>
#include <vector>
#include <cstdint>
#include <atomic>

namespace paimon::video {

struct VideoPlayerCreateOptions {
    bool requireCanonicalAudio = false;
    bool enableAudio = false;
    // DEPRECATED: forces the RGBA CPU conversion path. The GPU YUV path now
    // auto-resolves to RGBA via FBO blit when getResolvedRGBATexture() is used.
    bool forceRGBA = false;
};

class VideoPlayer {
public:
    // Call once from mod bootstrap on the Cocos main thread.
    static void bindMainThreadId();

    static std::unique_ptr<VideoPlayer> create(const std::string& videoPath);
    static std::unique_ptr<VideoPlayer> create(const std::string& videoPath, const VideoPlayerCreateOptions& options);

    ~VideoPlayer();

    void play();
    void pause();
    void resume();  // Resume decoder if it stopped (for shared video reuse)
    void stop();
    void forceStop();  // Stop without seeking — safe during shutdown
    void setLoop(bool loop);
    void setVolume(float volume);
    void setTargetFPS(int fps);

    void update(float dt);

    bool isPlaying() const;
    bool hasVisibleFrame() const;
    bool isTerminal() const;
    uint64_t getFrameCounter() const;

    cocos2d::CCTexture2D* getCurrentFrameTexture() const;

    /// Returns an RGBA texture regardless of whether GPU YUV mode is active.
    /// When GPU YUV is active, this resolves the YUV planes to an RGBA FBO
    /// on the GPU (no CPU conversion). The result is cached per-frame.
    /// When GPU YUV is not active, returns the same as getCurrentFrameTexture().
    cocos2d::CCTexture2D* getResolvedRGBATexture();

    /// When GPU YUV mode is active, the returned texture is the Y plane.
    /// The caller must apply the YUV shader and bind Cb/Cr textures.
    /// Returns nullptr if GPU YUV is not active.
    cocos2d::CCGLProgram* getYUVShaderProgram() const;
    cocos2d::CCTexture2D* getTextureCb() const;
    cocos2d::CCTexture2D* getTextureCr() const;
    bool isUsingGPUYuv() const;

    bool copyCurrentFramePixels(std::vector<uint8_t>& outPixels, int& outW, int& outH) const;

    int getWidth()  const;
    int getHeight() const;
    int getVideoWidth()  const { return getWidth(); }
    int getVideoHeight() const { return getHeight(); }
    double getDuration() const;

    size_t getEstimatedRAMBytes() const;
    std::string const& getFilePath() const;

    void setOnFinished(std::function<void()> cb);

    // Release the GPU YUV→RGBA resolve FBO and cached sprite. Recreated on
    // demand by getResolvedRGBATexture(). Safe to call repeatedly; no-op on
    // CPU/RGBA paths. Must be called on the main / GL thread.
    void releaseGPUResolveCache();

    // Audio stubs — kept for API compatibility (LayerBackgroundManager)
    void fadeAudioIn(float duration = 0.5f);
    void fadeAudioOut(float duration = 0.5f, std::function<void()> onComplete = nullptr);
    bool hasAudio() const;
    bool isAudioPlaying() const;
    bool didAudioInitFail() const;

private:
    VideoPlayer() = default;
    bool init(const std::string& videoPath, const VideoPlayerCreateOptions& options);

    void initTexture(int width, int height);
    void initYUVTextures(int width, int height);
    /// Pre-allocate the GL upload pipeline so the first uploadFrame() /
    /// getResolvedRGBATexture() don't pay GL allocation cost on the main
    /// thread. Safe to call multiple times; no-op if dimensions are invalid.
    /// MUST be called on the GL/main thread.
    void prepareGPUPipeline();
    bool uploadFrameGPU(const IVideoDecoder::Frame& frame);
    bool uploadFrame(const IVideoDecoder::Frame& frame);
    bool retryUploadFromRgbaBuffer();
    bool initAudio(const VideoPlayerCreateOptions& options);
    void playAudioFromCurrentTime(bool restartIfNeeded = false);
    void pauseAudio();
    void stopAudio(bool stopChannel);
    void syncAudioToPlaybackTime(bool force = false);

    std::unique_ptr<IVideoDecoder> m_decoder;

    // Single RGBA texture — uploaded via PBO for async GPU transfer.
    // Raw pointer: we do manual retain/release because the texture must outlive
    // any CCSprites that reference it during shared-video teardown.
    cocos2d::CCTexture2D* m_texture = nullptr;
    uint8_t* m_rgbaBuffer = nullptr;

    // GPU YUV→RGB path: 3 luminance textures + shader conversion
    cocos2d::CCTexture2D* m_texY  = nullptr;
    cocos2d::CCTexture2D* m_texCb = nullptr;
    cocos2d::CCTexture2D* m_texCr = nullptr;
    cocos2d::CCGLProgram* m_yuvShader = nullptr;
    bool m_useGPUYuv = false;  // true when GPU path is active

    // PBO-based async GPU uploader (RGBA mode or YUV mode)
    PBOUploader m_pboUploader;
    PBOUploader m_pboUploaderYUV;  // separate uploader for YUV 3-plane path
    bool m_pboInitAttempted = false;
    std::atomic<uint64_t> m_gpuInitGeneration{0};
    // Outlives the player so deferred GPU-init lambdas never read freed memory.
    std::shared_ptr<std::atomic<uint64_t>> m_gpuInitGate =
        std::make_shared<std::atomic<uint64_t>>(0);

    int m_texWidth  = 0;
    int m_texHeight = 0;

    double m_playbackTime = 0.0;
    float  m_volume       = 1.0f;
    int    m_targetFPS    = 30;
    bool   m_playing      = false;
    bool   m_loop         = false;
    bool   m_hasVisibleFrame = false;
    bool   m_pendingUpload = false;  // true if uploadFrame failed (GPU busy) and needs retry
    bool   m_decoderStalled = false; // true if decoder never produced a frame within timeout

    std::string m_filePath;

    double m_timeSinceLastUpload = 0.0;
    double m_timeSincePlay = 0.0;  // tracks time since play() was called, for stall detection
    uint64_t m_frameCounter = 0;

    // Guard against multiple update() calls in the same frame (can happen when
    // several scene-graph nodes share the same player and more than one survives
    // a layer transition).
    unsigned int m_lastUpdateFrame = 0;

    std::function<void()> m_onFinished;

    VideoPlayerCreateOptions m_createOptions{};
    std::string m_audioPath;
    std::string m_audioName;
    bool m_audioReady = false;
    bool m_audioInitFailed = false;
    double m_audioSyncAccumulator = 0.0;
    std::shared_ptr<std::atomic<bool>> m_audioPlaying = std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<std::atomic<uint32_t>> m_audioFadeGeneration = std::make_shared<std::atomic<uint32_t>>(0);

    // GPU YUV→RGBA resolve FBO (eliminates CPU SIMD conversion for forceRGBA callers)
    cocos2d::CCTexture2D* m_resolvedRGBA = nullptr;
    cocos2d::CCRenderTexture* m_resolveRT = nullptr;
    cocos2d::CCSprite* m_resolveSprite = nullptr;  // cached quad for FBO blit
    cocos2d::CCGLProgram* m_blitShader = nullptr;  // cached blit shader
    GLint m_locCb = -1;   // cached uniform locations
    GLint m_locCr = -1;
    GLint m_locY  = -1;
    GLint m_locCS = -1;
    float m_colorSpace = 0.0f;  // 0=BT.601, 1=BT.709
    uint64_t m_resolvedAtFrame = 0;  // frame counter when last resolved
    mutable GLuint m_readbackFBO = 0;  // cached FBO for copyCurrentFramePixels readback (const accessor)
    bool resolveYUVToRGBA();  // internal: render YUV planes to RGBA FBO
};

// Sync video audio volume with FMOD.
void syncVideoAudioVolume();

} // namespace paimon::video
