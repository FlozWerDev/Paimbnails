#pragma once

#include "../VideoDecoder.hpp"
#include <pl_mpeg.h>
#include <Geode/utils/string.hpp>
#include <Geode/utils/general.hpp>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include "../../utils/JoinWithWarning.hpp"

namespace paimon {

class DecoderPLM final : public IVideoDecoder {
public:
    DecoderPLM() = default;

    ~DecoderPLM() override { closeInternal(); }

    bool open(const std::string& path) override {
        closeInternal();
#ifdef _WIN32
        // fopen takes ansi paths, so a utf-8 folder (accents/cjk) never opens.
        FILE* fh = nullptr;
        if (_wfopen_s(&fh, geode::utils::string::utf8ToWide(path).c_str(), L"rb") != 0) return false;
        m_plm = plm_create_with_file(fh, TRUE);
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
        if (m_decoding.load(std::memory_order_relaxed)) return;
        if (!m_plm) return;
        m_decoding.store(true, std::memory_order_relaxed);
        m_finished.store(false, std::memory_order_relaxed);
        m_thread = std::thread(&DecoderPLM::decodeLoop, this);
    }

    void stopDecoding() override {
        m_decoding.store(false, std::memory_order_relaxed);
        m_ring.wakeAll();
        if (m_thread.joinable()) paimon::joinWithWarning(m_thread, std::chrono::seconds(3));
    }

    bool skipFrame() override {
        return m_ring.skipRead();
    }

    void seekTo(double seconds) override {
        if (!m_plm) return;
        bool wasDecoding = m_decoding.load(std::memory_order_relaxed);
        stopDecoding();

        while (m_ring.nextRead()) m_ring.commitRead();

        plm_seek(m_plm, seconds, false);
        m_finished.store(false, std::memory_order_relaxed);

        if (wasDecoding) startDecoding();
    }

    double getDuration() const override { return m_duration; }
    int getWidth()  const override { return m_ring.getWidth(); }
    int getHeight() const override { return m_ring.getHeight(); }
    // mpeg-1 streams are bt.601 limited; range/rotation defaults already fit.
    VideoColorMatrix getColorMatrix() const override { return VideoColorMatrix::BT601; }

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
        // guard so releaseframe() on an empty ring doesn't advance read idx.
        if (m_ring.peekRead()) m_ring.commitRead();
    }

    bool setLooping(bool loop) override {
        m_looping.store(loop, std::memory_order_relaxed);
        return true;
    }

private:
    // plm planes have no stride; width is the row stride.
    static void copyPlane(const plm_plane_t& plane, uint8_t* dst, int dstStride, int width, int height) {
        int rowBytes = std::min(width, static_cast<int>(plane.width));
        int rows = std::min(height, static_cast<int>(plane.height));
        for (int r = 0; r < rows; ++r) {
            std::memcpy(dst + r * dstStride,
                        plane.data + r * plane.width, rowBytes);
        }
    }

    void decodeLoop() {
        geode::utils::thread::setName("PaimonDecodePLM");
        plm_set_video_decode_callback(m_plm, nullptr, nullptr);

        while (m_decoding.load(std::memory_order_relaxed)) {
        // block briefly if the ring is full, then re-check m_decoding.
        if (m_ring.isFull()) {
                m_ring.waitForWritable(50, &m_decoding);
                continue;
            }

            plm_frame_t* frame = plm_decode_video(m_plm);
            if (!frame) {
                if (!m_looping.load(std::memory_order_relaxed)) {
                    m_finished.store(true, std::memory_order_release);
                    break;
                }
                plm_seek(m_plm, 0.0, false);
                frame = plm_decode_video(m_plm);
                if (!frame) {
                    m_finished.store(true, std::memory_order_release);
                    break;
                }
            }

            auto* slot = m_ring.nextWrite();
            if (!slot) {
                m_ring.waitForWritable(50, &m_decoding);
                continue;
            }

            // mpeg planes include macroblock padding beyond the visible frame.
            copyPlane(frame->y, slot->planeY, slot->strideY, slot->width, slot->height);
            int const uvWidth = (slot->width + 1) / 2;
            int const uvHeight = (slot->height + 1) / 2;
            copyPlane(frame->cb, slot->planeCb, slot->strideCb, uvWidth, uvHeight);
            copyPlane(frame->cr, slot->planeCr, slot->strideCr, uvWidth, uvHeight);

            slot->pts = frame->time;
            m_ring.commitWrite();
        }
    }

    void closeInternal() {
        stopDecoding();
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
    std::atomic<bool>   m_looping{false};
    std::thread         m_thread;
};

} // namespace paimon
