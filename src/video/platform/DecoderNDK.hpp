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
    bool setLooping(bool loop) override {
        m_looping.store(loop, std::memory_order_relaxed);
        return true;
    }

private:
    void decodeLoop();
    void closeInternal();
    bool findVideoTrack();
    void updateOutputFormat();
    // container keys first, codec output format refines; unknown keys keep auto.
    void readColorAspects(AMediaFormat* fmt);
    bool isReadableColorFormat(int colorFormat) const;
    bool isSemiPlanar(int colorFormat) const;

    // yuv_420_888 sidesteps vendor color-format guessing.
    bool setupImageReader();
    void releaseImageReader();
    bool drainImageReader(int64_t presentationTimeUs);

    AMediaExtractor* m_extractor = nullptr;
    AMediaCodec*     m_codec     = nullptr;
    AImageReader*    m_imageReader = nullptr;
    ANativeWindow*   m_readerWindow = nullptr; // owned by m_imagereader
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

    // never stop an unstarted codec; crashes some mali/powervr drivers.
    bool             m_codecConfigured = false;
    bool             m_codecStarted    = false;

    // some drivers emit a dummy buffer before format-change.
    std::atomic<bool> m_outputFormatValid{false};

    std::atomic<bool> m_decoding{false};
    std::atomic<bool> m_finished{false};
    std::atomic<bool> m_looping{false};
    std::thread       m_thread;
};

} // namespace paimon

#endif // use_media_ndk
