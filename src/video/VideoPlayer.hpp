#pragma once

#include "VideoDecoder.hpp"
#include "PBOUploader.hpp"
#include "VideoAudioTrack.hpp"
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
    // Deprecated CPU path; GPU YUV resolves through an FBO on demand.
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
    void resume();
    void stop();
    void forceStop();
    void setLoop(bool loop);
    void setVolume(float volume);
    void setTargetFPS(int fps);

    void update(float dt);

    bool isPlaying() const;
    bool hasVisibleFrame() const;
    bool isTerminal() const;
    uint64_t getFrameCounter() const;

    cocos2d::CCTexture2D* getCurrentFrameTexture() const;

    /// Return an RGBA texture, resolving GPU YUV through a cached FBO when needed.
    cocos2d::CCTexture2D* getResolvedRGBATexture();

    /// In GPU YUV mode, return the Y plane; callers bind Cb/Cr and the shader.
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

    // Release the YUV resolve cache; it is recreated on demand on the GL thread.
    void releaseGPUResolveCache();

    // Audio API kept for LayerBackgroundManager compatibility.
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
    /// Pre-allocate the GL upload pipeline; call on the GL thread.
    void prepareGPUPipeline();
    bool uploadFrameGPU(const IVideoDecoder::Frame& frame);
    bool uploadFrame(const IVideoDecoder::Frame& frame);
    bool retryUploadFromRgbaBuffer();
    bool initAudio(const VideoPlayerCreateOptions& options);
    void playAudioFromCurrentTime(bool restartIfNeeded = false);
    void pauseAudio();
    void stopAudio(bool stopChannel);

    std::unique_ptr<IVideoDecoder> m_decoder;

    // PBO-uploaded RGBA texture; manual retain/release keeps shared sprites safe.
    cocos2d::CCTexture2D* m_texture = nullptr;
    uint8_t* m_rgbaBuffer = nullptr;

    // GPU YUV→RGB path: three luminance textures plus shader conversion.
    cocos2d::CCTexture2D* m_texY  = nullptr;
    cocos2d::CCTexture2D* m_texCb = nullptr;
    cocos2d::CCTexture2D* m_texCr = nullptr;
    cocos2d::CCGLProgram* m_yuvShader = nullptr;
    bool m_useGPUYuv = false;

    // PBO-based async uploaders for RGBA and YUV paths.
    PBOUploader m_pboUploader;
    PBOUploader m_pboUploaderYUV;
    bool m_pboInitAttempted = false;
    std::atomic<uint64_t> m_gpuInitGeneration{0};
    // Outlives the player so deferred GPU-init lambdas cannot use freed state.
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
    bool   m_pendingUpload = false;
    bool   m_decoderStalled = false;
    // Decoder rewinds itself at EOS; playback time follows the PTS jump.
    bool   m_decoderLoops = false;

    std::string m_filePath;

    double m_timeSinceLastUpload = 0.0;
    double m_timeSincePlay = 0.0;  // stall detection
    uint64_t m_frameCounter = 0;

    // Avoid duplicate updates when several nodes share a player.
    unsigned int m_lastUpdateFrame = 0;

    std::function<void()> m_onFinished;

    VideoPlayerCreateOptions m_createOptions{};
    std::unique_ptr<VideoAudioTrack> m_audio;
    bool m_audioInitFailed = false;
    std::shared_ptr<std::atomic<uint32_t>> m_audioFadeGeneration = std::make_shared<std::atomic<uint32_t>>(0);

    // GPU YUV→RGBA resolve FBO.
    cocos2d::CCTexture2D* m_resolvedRGBA = nullptr;
    cocos2d::CCRenderTexture* m_resolveRT = nullptr;
    cocos2d::CCSprite* m_resolveSprite = nullptr;
    cocos2d::CCGLProgram* m_blitShader = nullptr;
    GLint m_locCb = -1;
    GLint m_locCr = -1;
    GLint m_locY  = -1;
    GLint m_locCS = -1;
    float m_colorSpace = 0.0f;  // 0=BT.601, 1=BT.709
    uint64_t m_resolvedAtFrame = 0;
    mutable GLuint m_readbackFBO = 0;
    bool resolveYUVToRGBA();
};

// Re-apply GD's music volume to every live video audio track.
void syncVideoAudioVolume();

}
