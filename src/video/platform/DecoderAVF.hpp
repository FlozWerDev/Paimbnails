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

private:
    void decodeLoop();
    void closeInternal();
    // Build a fresh AVAssetReader over the current asset, starting at the
    // given time offset.  Called from open() with offset=0 and from seekTo().
    // On success m_reader / m_trackOutput are populated and startReading has
    // been issued.  m_asset must already be set.
    bool buildReader(double startTimeSeconds);
    // Release just the reader/trackOutput (not the asset) without disturbing
    // the decode thread state.  Used by seekTo() to rewind.
    void releaseReaderOnly();

    // Opaque pointers to Obj-C objects (managed with ARC in .mm)
    void* m_asset       = nullptr; // AVAsset*
    void* m_reader      = nullptr; // AVAssetReader*
    void* m_trackOutput = nullptr; // AVAssetReaderTrackOutput*
    void* m_videoTrack  = nullptr; // AVAssetTrack* (weak ref into asset)

    VideoRingBuffer  m_ring;
    int              m_width  = 0;
    int              m_height = 0;
    double           m_duration = 0.0;
    // Tracks the pixel format we ended up with; we store it so the decode
    // loop can handle both planar (kCVPixelFormatType_420YpCbCr8Planar) and
    // bi-planar NV12 (kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange).
    uint32_t         m_pixelFormat = 0;

    std::atomic<bool> m_decoding{false};
    std::atomic<bool> m_finished{false};
    std::atomic<bool> m_decodeThreadDetached{false};
    std::thread       m_thread;
};

} // namespace paimon

#endif // USE_AV_FOUNDATION
