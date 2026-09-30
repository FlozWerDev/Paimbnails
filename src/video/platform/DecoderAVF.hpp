#pragma once

#include "../VideoDecoder.hpp"

#if defined(USE_AV_FOUNDATION)

#include <CoreFoundation/CoreFoundation.h>

namespace paimon {

class DecoderAVF final : public IVideoDecoder {
public:
    DecoderAVF() = default;
    ~DecoderAVF() override { closeInternal(); }

    bool open(const std::string& path) override;
    void startDecoding() override;
    void stopDecoding() override;
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
    VideoColorMatrix getColorMatrix() const override { return m_colorMatrix; }
    bool isFullRange() const override { return m_fullRange; }
    int getRotationDegrees() const override { return m_rotation; }
    bool setLooping(bool loop) override {
        m_looping.store(loop, std::memory_order_relaxed);
        return true;
    }

private:
    void decodeLoop();
    void closeInternal();
    // m_asset must already be set.
    bool buildReader(double startTimeSeconds);
    void readTrackMetadata();
    // reader only, never the asset; used by seekto().
    void releaseReaderOnly();

    // opaque pointers to obj-c objects (managed with arc in .mm)
    void* m_asset       = nullptr; // avasset*
    void* m_reader      = nullptr; // avassetreader*
    void* m_trackOutput = nullptr; // avassetreadertrackoutput*
    void* m_videoTrack  = nullptr; // avassettrack* (weak ref into asset)

    VideoRingBuffer  m_ring;
    int              m_width  = 0;
    int              m_height = 0;
    double           m_duration = 0.0;
    uint32_t         m_pixelFormat = 0;
    VideoColorMatrix m_colorMatrix = VideoColorMatrix::Auto;
    bool             m_fullRange = false;
    int              m_rotation = 0;

    std::atomic<bool> m_decoding{false};
    std::atomic<bool> m_finished{false};
    std::atomic<bool> m_looping{false};
    std::thread       m_thread;
};

} // namespace paimon

#endif // use_av_foundation
