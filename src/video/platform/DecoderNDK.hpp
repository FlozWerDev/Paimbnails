#pragma once

#include "../VideoDecoder.hpp"

#if defined(USE_MEDIA_NDK)

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>
#include <android/native_window.h>

namespace paimon {

class DecoderNDK final : public IVideoDecoder {
public:
    DecoderNDK() = default;
    ~DecoderNDK() override { closeInternal(); }

    bool open(const std::string& path) override;
    void startDecoding() override;
    bool stopDecoding() override;
    bool consumeFrame(Frame& outFrame) override;
    bool skipFrame() override;
    void seekTo(double seconds) override;
    double getDuration() const override;
    int getWidth() const override;
    int getHeight() const override;
    bool isFinished() const override;
    double peekNextPTS() const override;

    // Optional: set surface for direct rendering (zero-copy)
    void setSurface(ANativeWindow* window);

private:
    void decodeLoop(uint64_t gen);
    void closeInternal();
    bool findVideoTrack();
    void updateOutputFormat();

    AMediaExtractor* m_extractor = nullptr;
    AMediaCodec*     m_codec     = nullptr;
    ANativeWindow*   m_surface   = nullptr; // not owned
    int              m_trackIdx  = -1;

    VideoRingBuffer  m_ring;
    int              m_width  = 0;
    int              m_height = 0;
    int              m_outputStride = 0;
    int              m_outputSliceHeight = 0;
    int              m_outputColorFormat = 0;
    double           m_duration = 0.0;
    bool             m_useSurface = false;

    std::atomic<bool>     m_decoding{false};
    std::atomic<bool>     m_finished{false};
    std::atomic<uint64_t> m_generation{0};
    // Set to true when decode thread starts, false when it exits.
    // Used to ensure closeInternal() doesn't destroy the codec while
    // a detached thread is still accessing it.
    std::atomic<bool>     m_threadRunning{false};
    std::thread           m_thread;
};

} // namespace paimon

#endif // USE_MEDIA_NDK
