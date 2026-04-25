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

private:
    void decodeLoop();
    void closeInternal();

    // Opaque pointers to Obj-C objects (managed with ARC in .mm)
    void* m_asset       = nullptr; // AVAsset*
    void* m_reader      = nullptr; // AVAssetReader*
    void* m_trackOutput = nullptr; // AVAssetReaderTrackOutput*

    VideoRingBuffer  m_ring;
    int              m_width  = 0;
    int              m_height = 0;
    double           m_duration = 0.0;

    std::atomic<bool> m_decoding{false};
    std::atomic<bool> m_finished{false};
    std::thread       m_thread;
};

} // namespace paimon

#endif // USE_AV_FOUNDATION
