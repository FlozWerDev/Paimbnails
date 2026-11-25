#pragma once

#include "../VideoDecoder.hpp"

#if defined(USE_AV_FOUNDATION)

#include <CoreFoundation/CoreFoundation.h>
#include <memory>

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
    VideoColorMatrix getColorMatrix() const override {
        auto st = m_state;
        return st ? st->m_colorMatrix : VideoColorMatrix::Auto;
    }
    bool isFullRange() const override {
        auto st = m_state;
        return st ? st->m_fullRange : false;
    }
    int getRotationDegrees() const override {
        auto st = m_state;
        return st ? st->m_rotation : 0;
    }
    bool setLooping(bool loop) override {
        if (auto st = m_state) st->m_looping.store(loop, std::memory_order_relaxed);
        return true;
    }

private:
    // crash #9: everything the decode worker touches (ring, reader, asset,
    // flags) lives here under shared_ptr. The worker captures its own copy
    // at spawn, so a detach after a timed-out join never leaves the orphan
    // writing freed memory. The decoder shell only owns the std::thread
    // plus its own copy of the state. Public API unchanged.
    struct State {
        ~State();

        void decodeLoop();
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
        // set on every worker exit path (ThreadExitSignal); bounds the join.
        std::atomic<bool> m_threadDone{true};
    };

    void closeInternal();

    std::shared_ptr<State> m_state;
    std::thread            m_thread;
};

} // namespace paimon

#endif // use_av_foundation
