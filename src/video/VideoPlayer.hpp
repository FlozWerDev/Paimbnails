#pragma once

#include "VideoDecoder.hpp"
#include "PBOUploader.hpp"
#include <Geode/cocos/textures/CCTexture2D.h>
#include <Geode/cocos/include/ccTypes.h>
#include <functional>
#include <memory>
#include <vector>
#include <cstdint>

namespace paimon::video {

struct VideoPlayerCreateOptions {
    bool requireCanonicalAudio = false;
    bool enableAudio = false;
};

class VideoPlayer {
public:
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
    uint64_t getFrameCounter() const;

    cocos2d::CCTexture2D* getCurrentFrameTexture() const;

    bool copyCurrentFramePixels(std::vector<uint8_t>& outPixels, int& outW, int& outH) const;

    int getWidth()  const;
    int getHeight() const;
    int getVideoWidth()  const { return getWidth(); }
    int getVideoHeight() const { return getHeight(); }
    double getDuration() const;

    size_t getEstimatedRAMBytes() const;
    std::string const& getFilePath() const;

    void setOnFinished(std::function<void()> cb);

    // Audio stubs — kept for API compatibility (LayerBackgroundManager)
    void fadeAudioIn(float duration = 0.5f);
    void fadeAudioOut(float duration = 0.5f, std::function<void()> onComplete = nullptr);
    bool hasAudio() const;
    bool isAudioPlaying() const;
    bool didAudioInitFail() const;

private:
    VideoPlayer() = default;
    bool init(const std::string& videoPath);

    void initTexture(int width, int height);
    bool uploadFrame(const IVideoDecoder::Frame& frame);

    std::unique_ptr<IVideoDecoder> m_decoder;

    // Single RGBA texture — uploaded via PBO for async GPU transfer.
    // Raw pointer: we do manual retain/release because the texture must outlive
    // any CCSprites that reference it during shared-video teardown.
    cocos2d::CCTexture2D* m_texture = nullptr;
    uint8_t* m_rgbaBuffer = nullptr;

    // PBO-based async GPU uploader (RGBA mode)
    PBOUploader m_pboUploader;
    bool m_pboInitAttempted = false;

    int m_texWidth  = 0;
    int m_texHeight = 0;

    // Working frame (consumed from ring buffer)
    IVideoDecoder::Frame m_workingFrame{};

    double m_playbackTime = 0.0;
    float  m_volume       = 1.0f;
    int    m_targetFPS    = 30;
    bool   m_playing      = false;
    bool   m_loop         = false;
    bool   m_hasVisibleFrame = false;
    bool   m_pendingUpload = false;  // true if uploadFrame failed (GPU busy) and needs retry

    std::string m_filePath;

    double m_originalInterval = 1.0 / 60.0;
    double m_timeSinceLastUpload = 0.0;
    uint64_t m_frameCounter = 0;

    // Guard against multiple update() calls in the same frame (can happen when
    // several scene-graph nodes share the same player and more than one survives
    // a layer transition).
    unsigned int m_lastUpdateFrame = 0;

    std::function<void()> m_onFinished;
};

// Sync video audio volume with FMOD (stub — no audio in current impl)
void syncVideoAudioVolume();

} // namespace paimon::video
