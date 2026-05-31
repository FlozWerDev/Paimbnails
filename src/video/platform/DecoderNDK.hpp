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
    void stopDecoding() override;
    bool consumeFrame(Frame& outFrame) override;
    bool skipFrame() override;
    void seekTo(double seconds) override;
    double getDuration() const override;
    int getWidth() const override;
    int getHeight() const override;
    bool isFinished() const override;
    double peekNextPTS() const override;
    double peekSecondPTS() const override;
    const Frame* peekFrame() override;
    void releaseFrame() override;
    bool isTerminal() const override { return m_decodeThreadDetached.load(std::memory_order_acquire); }

    // Optional: set surface for direct rendering (zero-copy)
    void setSurface(ANativeWindow* window);

private:
    void decodeLoop();
    void closeInternal();
    bool findVideoTrack();
    void updateOutputFormat();
    // Returns true if the color format is known and safe to read from CPU.
    bool isReadableColorFormat(int colorFormat) const;
    // Returns true if the color format delivers YUV in semi-planar (NV12) layout.
    bool isSemiPlanar(int colorFormat) const;

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

    // Track codec state so we never call AMediaCodec_stop on an unstarted/
    // released codec — that crashes on some Mali/PowerVR drivers.
    bool             m_codecConfigured = false;
    bool             m_codecStarted    = false;

    // Set to true after a valid output format has been seen.  Until then
    // we must not attempt to read the output buffer as YUV (some drivers
    // deliver a dummy buffer before the first format change signal).
    std::atomic<bool> m_outputFormatValid{false};

    std::atomic<bool> m_decoding{false};
    std::atomic<bool> m_finished{false};
    std::atomic<bool> m_decodeThreadDetached{false};
    std::thread       m_thread;
};

} // namespace paimon

#endif // USE_MEDIA_NDK
