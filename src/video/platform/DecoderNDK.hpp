#pragma once

#include "../VideoDecoder.hpp"

#if defined(USE_MEDIA_NDK)

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>
#include <android/native_window.h>

namespace paimon {

class DecoderNDK final : public IVideoDecoder {
public:
    DecoderNDK() = default;
    ~DecoderNDK() override { closeInternal(); }

    bool open(const std::string& path) override;
    void startDecoding() override;
    void stopDecoding() override;
    bool skipFrame() override;
    void seekTo(double seconds) override;
    double getDuration() const override;
    int getWidth() const override;
    int getHeight() const override;
    VideoColorMatrix getColorMatrix() const override;
    bool isFullRange() const override;
    int getRotationDegrees() const override;
    bool isFinished() const override;
    double peekNextPTS() const override;
    double peekSecondPTS() const override;
    const Frame* peekFrame() override;
    void releaseFrame() override;
    bool isTerminal() const override { return m_decodeThreadDetached.load(std::memory_order_acquire); }
    bool setLooping(bool loop) override {
        m_looping.store(loop, std::memory_order_relaxed);
        return true;
    }

private:
    void decodeLoop();
    void closeInternal();
    bool findVideoTrack();
    void updateOutputFormat();
    // Container keys first, codec output format refines; unknown keys keep Auto.
    void readColorAspects(AMediaFormat* fmt);
    // Returns true if the color format is known and safe to read from CPU.
    bool isReadableColorFormat(int colorFormat) const;
    // Returns true if the color format delivers YUV in semi-planar (NV12) layout.
    bool isSemiPlanar(int colorFormat) const;

    // AImageReader hands back YUV_420_888 with documented per-plane strides,
    // which sidesteps the vendor color-format guessing of the raw buffer path.
    bool setupImageReader();
    void releaseImageReader();
    bool drainImageReader(int64_t presentationTimeUs);

    AMediaExtractor* m_extractor = nullptr;
    AMediaCodec*     m_codec     = nullptr;
    AImageReader*    m_imageReader = nullptr;
    ANativeWindow*   m_readerWindow = nullptr; // owned by m_imageReader
    bool             m_useImageReader = false;
    int              m_trackIdx  = -1;

    VideoRingBuffer  m_ring;
    int              m_width  = 0;
    int              m_height = 0;
    int              m_outputStride = 0;
    int              m_outputSliceHeight = 0;
    int              m_outputColorFormat = 0;
    VideoColorMatrix m_colorMatrix = VideoColorMatrix::Auto;
    bool             m_fullRange = false;
    int              m_rotation = 0;
    double           m_duration = 0.0;

    // Track codec state so we never call AMediaCodec_stop on an unstarted/
    // released codec — that crashes on some Mali/PowerVR drivers.
    bool             m_codecConfigured = false;
    bool             m_codecStarted    = false;

    // No YUV reads until a valid output format arrives; some drivers emit
    // a dummy buffer before the first format-change signal.
    std::atomic<bool> m_outputFormatValid{false};

    std::atomic<bool> m_decoding{false};
    std::atomic<bool> m_finished{false};
    std::atomic<bool> m_looping{false};
    std::atomic<bool> m_decodeThreadDetached{false};
    std::thread       m_thread;
};

} // namespace paimon

#endif // USE_MEDIA_NDK
