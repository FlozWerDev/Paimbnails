#pragma once

#include "../VideoDecoder.hpp"
#include <pl_mpeg.h>
#include <algorithm>
#include <cstring>
#include "../../utils/TimedJoin.hpp"

namespace paimon {

class DecoderPLM final : public IVideoDecoder {
public:
    DecoderPLM() = default;

    ~DecoderPLM() override { closeInternal(); }

    bool open(const std::string& path) override {
        closeInternal();
#ifdef _WIN32
        // pl_mpeg uses fopen which needs ANSI paths on Windows.
        m_plm = plm_create_with_filename(path.c_str());
#else
        m_plm = plm_create_with_filename(path.c_str());
#endif
        if (!m_plm) return false;

        plm_set_audio_enabled(m_plm, false);

        int w = plm_get_width(m_plm);
        int h = plm_get_height(m_plm);
        if (w <= 0 || h <= 0) {
            closeInternal();
            return false;
        }

        if (!m_ring.init(w, h)) {
            closeInternal();
            return false;
        }

        m_duration = plm_get_duration(m_plm);
        m_finished.store(false, std::memory_order_relaxed);
        m_decoding.store(false, std::memory_order_relaxed);
        return true;
    }

    void startDecoding() override {
        if (m_decodeThreadDetached.load(std::memory_order_acquire)) return;
        if (m_decoding.load(std::memory_order_relaxed)) return;
        m_decoding.store(true, std::memory_order_relaxed);
        m_finished.store(false, std::memory_order_relaxed);
        m_thread = std::thread(&DecoderPLM::decodeLoop, this);
    }

    void stopDecoding() override {
        m_decoding.store(false, std::memory_order_relaxed);
        m_ring.wakeAll();  // unblock any waiter immediately
        if (m_thread.joinable() && !paimon::timedJoin(m_thread, std::chrono::seconds(3))) {
            m_decodeThreadDetached.store(true, std::memory_order_release);
        }
    }

    bool consumeFrame(Frame& outFrame) override {
        auto* slot = m_ring.nextRead();
        if (!slot) return false;

        int ySize  = slot->strideY * slot->height;
        int uvH    = (slot->height + 1) / 2;
        int uvSize = slot->strideCb * uvH;

        std::memcpy(outFrame.planeY,  slot->planeY,  ySize);
        std::memcpy(outFrame.planeCb, slot->planeCb, uvSize);
        std::memcpy(outFrame.planeCr, slot->planeCr, uvSize);
        outFrame.strideY  = slot->strideY;
        outFrame.strideCb = slot->strideCb;
        outFrame.strideCr = slot->strideCr;
        outFrame.width    = slot->width;
        outFrame.height   = slot->height;
        outFrame.pts      = slot->pts;

        m_ring.commitRead();
        return true;
    }

    bool skipFrame() override {
        return m_ring.skipRead();
    }

    void seekTo(double seconds) override {
        if (!m_plm) return;
        bool wasDecoding = m_decoding.load(std::memory_order_relaxed);
        stopDecoding();
        if (m_decodeThreadDetached.load(std::memory_order_acquire)) return;

        // Drain ring buffer
        while (m_ring.nextRead()) m_ring.commitRead();

        plm_seek(m_plm, seconds, false);
        m_finished.store(false, std::memory_order_relaxed);

        if (wasDecoding) startDecoding();
    }

    double getDuration() const override { return m_duration; }
    int getWidth()  const override { return m_ring.getWidth(); }
    int getHeight() const override { return m_ring.getHeight(); }

    bool isFinished() const override {
        return m_finished.load(std::memory_order_acquire);
    }

    double peekNextPTS() const override {
        return m_ring.peekNextPTS();
    }

    double peekSecondPTS() const override {
        return m_ring.peekSecondPTS();
    }

    const Frame* peekFrame() override {
        return m_ring.peekRead();
    }

    void releaseFrame() override {
        // Only commit if a slot is still held.  commitRead is idempotent-safe
        // to re-entry thanks to the ring's ready-flag gating, but we guard
        // anyway so releaseFrame() on an empty ring doesn't advance read idx.
        if (m_ring.peekRead()) m_ring.commitRead();
    }

    bool isTerminal() const override {
        return m_decodeThreadDetached.load(std::memory_order_acquire);
    }

private:
    void decodeLoop() {
        plm_set_video_decode_callback(m_plm, nullptr, nullptr);

        while (m_decoding.load(std::memory_order_relaxed)) {
            // Wait if ring buffer is full — block on cv up to 50 ms then
            // re-check m_decoding so shutdown is responsive.
            if (m_ring.isFull()) {
                m_ring.waitForWritable(50, &m_decoding);
                continue;
            }

            plm_frame_t* frame = plm_decode_video(m_plm);
            if (!frame) {
                m_finished.store(true, std::memory_order_release);
                break;
            }

            auto* slot = m_ring.nextWrite();
            if (!slot) {
                m_ring.waitForWritable(50, &m_decoding);
                continue;
            }

            // Copy Y plane (plm_plane_t has no stride; width is the row stride)
            int yRows = frame->y.height;
            int yStride = frame->y.width;
            for (int r = 0; r < yRows; ++r) {
                std::memcpy(slot->planeY + r * slot->strideY,
                            frame->y.data  + r * yStride,
                            std::min(slot->strideY, yStride));
            }

            // Copy Cb plane
            int cbRows = frame->cb.height;
            int cbStride = frame->cb.width;
            for (int r = 0; r < cbRows; ++r) {
                std::memcpy(slot->planeCb + r * slot->strideCb,
                            frame->cb.data  + r * cbStride,
                            std::min(slot->strideCb, cbStride));
            }

            // Copy Cr plane
            int crRows = frame->cr.height;
            int crStride = frame->cr.width;
            for (int r = 0; r < crRows; ++r) {
                std::memcpy(slot->planeCr + r * slot->strideCr,
                            frame->cr.data  + r * crStride,
                            std::min(slot->strideCr, crStride));
            }

            slot->pts = frame->time;
            m_ring.commitWrite();
        }
    }

    void closeInternal() {
        stopDecoding();
        if (m_decodeThreadDetached.load(std::memory_order_acquire)) {
            m_plm = nullptr;
            return;
        }
        if (m_plm) {
            plm_destroy(m_plm);
            m_plm = nullptr;
        }
    }

    plm_t*              m_plm = nullptr;
    VideoRingBuffer     m_ring;
    double              m_duration = 0.0;
    std::atomic<bool>   m_decoding{false};
    std::atomic<bool>   m_finished{false};
    std::atomic<bool>   m_decodeThreadDetached{false};
    std::thread         m_thread;
};

} // namespace paimon
