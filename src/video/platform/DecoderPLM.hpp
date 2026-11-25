#pragma once

#include "../VideoDecoder.hpp"
#include <pl_mpeg.h>
#include <Geode/loader/Log.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/utils/general.hpp>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include "../../utils/JoinWithWarning.hpp"

namespace paimon {

// crash #9: everything the decode worker touches (plm handle, ring, flags)
// lives in State under shared_ptr. The worker captures its own copy at
// spawn, so a detach after a timed-out join never leaves the orphan
// writing freed memory. Local file reads return fast (corrupt streams fail
// via plm_decode_video == nullptr), and the stop flag is re-checked every
// iteration, so the bounded join below is the last resort, not the norm.
class DecoderPLM final : public IVideoDecoder {
public:
    DecoderPLM() = default;

    ~DecoderPLM() override { closeInternal(); }

    bool open(const std::string& path) override {
        closeInternal();
        auto st = std::make_shared<State>();
#ifdef _WIN32
        // fopen takes ansi paths, so a utf-8 folder (accents/cjk) never opens.
        FILE* fh = nullptr;
        if (_wfopen_s(&fh, geode::utils::string::utf8ToWide(path).c_str(), L"rb") != 0) return false;
        st->plm = plm_create_with_file(fh, TRUE);
#else
        st->plm = plm_create_with_filename(path.c_str());
#endif
        if (!st->plm) return false;

        plm_set_audio_enabled(st->plm, false);

        int w = plm_get_width(st->plm);
        int h = plm_get_height(st->plm);
        if (w <= 0 || h <= 0) return false;

        if (!st->ring.init(w, h)) return false;

        st->duration = plm_get_duration(st->plm);
        m_state = std::move(st);
        return true;
    }

    void startDecoding() override {
        auto st = m_state;
        if (!st || !st->plm) return;
        if (st->decoding.load(std::memory_order_relaxed)) return;
        // never run two workers on one state: an orphan detached by a
        // timed-out stop still owns this state until it exits.
        if (!st->threadDone.load(std::memory_order_acquire)) {
            geode::log::warn("DecoderPLM: previous worker still draining, start deferred");
            return;
        }
        st->decoding.store(true, std::memory_order_relaxed);
        st->finished.store(false, std::memory_order_relaxed);
        st->threadDone.store(false, std::memory_order_release);
        m_thread = std::thread([st] { st->decodeLoop(); });
    }

    void stopDecoding() override {
        auto st = m_state;
        if (!st) {
            if (m_thread.joinable()) m_thread.join();
            return;
        }
        st->decoding.store(false, std::memory_order_relaxed);
        st->ring.wakeAll();
        if (m_thread.joinable()) {
            paimon::joinOrDetachWithWarning(m_thread, std::chrono::seconds(3), "DecoderPLM", [&] {
                return st->threadDone.load(std::memory_order_acquire);
            });
        }
    }

    bool skipFrame() override {
        auto st = m_state;
        return st ? st->ring.skipRead() : false;
    }

    void seekTo(double seconds) override {
        auto st = m_state;
        if (!st || !st->plm) return;
        bool wasDecoding = st->decoding.load(std::memory_order_relaxed);
        stopDecoding();
        // an orphan detached by a timed-out stop still owns the stream.
        if (!st->threadDone.load(std::memory_order_acquire)) {
            geode::log::warn("DecoderPLM: orphan worker still owns stream, seek skipped");
            return;
        }

        while (st->ring.nextRead()) st->ring.commitRead();

        plm_seek(st->plm, seconds, false);
        st->finished.store(false, std::memory_order_relaxed);

        if (wasDecoding) startDecoding();
    }

    double getDuration() const override {
        auto st = m_state;
        return st ? st->duration : 0.0;
    }
    int getWidth()  const override {
        auto st = m_state;
        return st ? st->ring.getWidth() : 0;
    }
    int getHeight() const override {
        auto st = m_state;
        return st ? st->ring.getHeight() : 0;
    }
    // mpeg-1 streams are bt.601 limited; range/rotation defaults already fit.
    VideoColorMatrix getColorMatrix() const override { return VideoColorMatrix::BT601; }

    bool isFinished() const override {
        auto st = m_state;
        return st ? st->finished.load(std::memory_order_acquire) : false;
    }

    double peekNextPTS() const override {
        auto st = m_state;
        return st ? st->ring.peekNextPTS() : DBL_MAX;
    }

    double peekSecondPTS() const override {
        auto st = m_state;
        return st ? st->ring.peekSecondPTS() : DBL_MAX;
    }

    const Frame* peekFrame() override {
        auto st = m_state;
        return st ? st->ring.peekRead() : nullptr;
    }

    void releaseFrame() override {
        auto st = m_state;
        // guard so releaseframe() on an empty ring doesn't advance read idx.
        if (st && st->ring.peekRead()) st->ring.commitRead();
    }

    bool setLooping(bool loop) override {
        if (auto st = m_state) st->looping.store(loop, std::memory_order_relaxed);
        return true;
    }

private:
    struct State {
        ~State() {
            if (plm) {
                plm_destroy(plm);
                plm = nullptr;
            }
        }

        void decodeLoop() {
            ThreadExitSignal exitSignal(threadDone);
            geode::utils::thread::setName("PaimonDecodePLM");
            plm_set_video_decode_callback(plm, nullptr, nullptr);

            while (decoding.load(std::memory_order_relaxed)) {
            // block briefly if the ring is full, then re-check decoding.
            if (ring.isFull()) {
                    ring.waitForWritable(50, &decoding);
                    continue;
                }

                plm_frame_t* frame = plm_decode_video(plm);
                if (!frame) {
                    if (!looping.load(std::memory_order_relaxed)) {
                        finished.store(true, std::memory_order_release);
                        break;
                    }
                    plm_seek(plm, 0.0, false);
                    frame = plm_decode_video(plm);
                    if (!frame) {
                        finished.store(true, std::memory_order_release);
                        break;
                    }
                }
                // stop requested while blocked above: never publish a frame
                // the owner may already be tearing down for.
                if (!decoding.load(std::memory_order_relaxed)) break;

                auto* slot = ring.nextWrite();
                if (!slot) {
                    ring.waitForWritable(50, &decoding);
                    continue;
                }

                // mpeg planes include macroblock padding beyond the visible frame.
                copyPlane(frame->y, slot->planeY, slot->strideY, slot->width, slot->height);
                int const uvWidth = (slot->width + 1) / 2;
                int const uvHeight = (slot->height + 1) / 2;
                copyPlane(frame->cb, slot->planeCb, slot->strideCb, uvWidth, uvHeight);
                copyPlane(frame->cr, slot->planeCr, slot->strideCr, uvWidth, uvHeight);

                slot->pts = frame->time;
                ring.commitWrite();
            }
        }

        // plm planes have no stride; width is the row stride.
        static void copyPlane(const plm_plane_t& plane, uint8_t* dst, int dstStride, int width, int height) {
            int rowBytes = std::min(width, static_cast<int>(plane.width));
            int rows = std::min(height, static_cast<int>(plane.height));
            copyStridedRows(dst, static_cast<size_t>(dstStride), plane.data,
                            plane.width, static_cast<size_t>(rowBytes), rows);
        }

        plm_t*            plm = nullptr;
        VideoRingBuffer   ring;
        double            duration = 0.0;
        std::atomic<bool> decoding{false};
        std::atomic<bool> finished{false};
        std::atomic<bool> looping{false};
        // set on every worker exit path; bounds the join in stopDecoding().
        std::atomic<bool> threadDone{true};
    };

    void closeInternal() {
        stopDecoding();
        // plm handle + ring live in State: a detached orphan keeps its copy
        // alive until it exits, so this never frees memory it still uses.
        m_state.reset();
    }

    std::shared_ptr<State> m_state;
    std::thread            m_thread;
};

} // namespace paimon
