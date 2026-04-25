#include "DecoderNDK.hpp"

#if defined(USE_MEDIA_NDK)

#include <Geode/loader/Log.hpp>
#include <cstring>
#include <chrono>
#include <algorithm>
#include "../../utils/TimedJoin.hpp"

namespace paimon {

static constexpr int kColorFormatYUV420SemiPlanar = 21;
static constexpr int kColorFormatYUV420Flexible = 0x7F420888;
static constexpr int kColorFormatQCOMYUV420SemiPlanar = 0x7FA30C00;

static int getFormatInt32(AMediaFormat* fmt, const char* key, int fallback) {
    int32_t value = fallback;
    if (fmt) AMediaFormat_getInt32(fmt, key, &value);
    return static_cast<int>(value);
}

static bool isSemiPlanarFormat(int colorFormat) {
    return colorFormat == kColorFormatYUV420SemiPlanar ||
           colorFormat == kColorFormatYUV420Flexible ||
           colorFormat == kColorFormatQCOMYUV420SemiPlanar ||
           colorFormat == 0;
}

void DecoderNDK::updateOutputFormat() {
    if (!m_codec) return;
    AMediaFormat* fmt = AMediaCodec_getOutputFormat(m_codec);
    if (!fmt) return;
    m_outputStride = std::max(1, getFormatInt32(fmt, "stride", m_width));
    m_outputSliceHeight = std::max(m_height, getFormatInt32(fmt, "slice-height", m_height));
    m_outputColorFormat = getFormatInt32(fmt, "color-format", m_outputColorFormat);
    AMediaFormat_delete(fmt);
}

// ─────────────────────────────────────────────────────────────
// Open
// ─────────────────────────────────────────────────────────────
bool DecoderNDK::open(const std::string& path) {
    closeInternal();

    m_extractor = AMediaExtractor_new();
    if (!m_extractor) {
        geode::log::warn("DecoderNDK: AMediaExtractor_new failed");
        return false;
    }

    int rc = AMediaExtractor_setDataSource(m_extractor, path.c_str());
    if (rc != AMEDIA_OK) {
        geode::log::warn("DecoderNDK: setDataSource failed ({})", rc);
        closeInternal();
        return false;
    }

    if (!findVideoTrack()) {
        geode::log::warn("DecoderNDK: no video track found");
        closeInternal();
        return false;
    }

    // Create decoder
    AMediaFormat* trackFmt = AMediaExtractor_getTrackFormat(m_extractor, m_trackIdx);
    const char* mime = nullptr;
    AMediaFormat_getString(trackFmt, AMEDIAFORMAT_KEY_MIME, &mime);
    if (!mime) {
        AMediaFormat_delete(trackFmt);
        closeInternal();
        return false;
    }

    m_codec = AMediaCodec_createDecoderByType(mime);
    AMediaFormat_delete(trackFmt);
    if (!m_codec) {
        geode::log::warn("DecoderNDK: createDecoderByType({}) failed", mime);
        closeInternal();
        return false;
    }

    // Configure: with or without surface
    AMediaFormat* fmt = AMediaExtractor_getTrackFormat(m_extractor, m_trackIdx);
    media_status_t status;
    if (m_surface && m_useSurface) {
        status = AMediaCodec_configure(m_codec, fmt, m_surface, nullptr, 0);
    } else {
        status = AMediaCodec_configure(m_codec, fmt, nullptr, nullptr, 0);
    }
    AMediaFormat_delete(fmt);

    if (status != AMEDIA_OK) {
        geode::log::warn("DecoderNDK: configure failed ({})", static_cast<int>(status));
        closeInternal();
        return false;
    }

    status = AMediaCodec_start(m_codec);
    if (status != AMEDIA_OK) {
        geode::log::warn("DecoderNDK: start failed ({})", static_cast<int>(status));
        closeInternal();
        return false;
    }

    AMediaExtractor_selectTrack(m_extractor, m_trackIdx);
    updateOutputFormat();

    if (!m_ring.init(m_width, m_height)) {
        closeInternal();
        return false;
    }

    m_finished.store(false, std::memory_order_relaxed);
    m_decoding.store(false, std::memory_order_relaxed);
    return true;
}

bool DecoderNDK::findVideoTrack() {
    size_t numTracks = AMediaExtractor_getTrackCount(m_extractor);
    for (size_t i = 0; i < numTracks; ++i) {
        AMediaFormat* fmt = AMediaExtractor_getTrackFormat(m_extractor, i);
        const char* mime = nullptr;
        AMediaFormat_getString(fmt, AMEDIAFORMAT_KEY_MIME, &mime);
        if (mime && (strncmp(mime, "video/", 6) == 0)) {
            m_trackIdx = static_cast<int>(i);

            int32_t w = 0, h = 0;
            AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_WIDTH, &w);
            AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_HEIGHT, &h);
            m_width = w;
            m_height = h;

            int64_t dur = 0;
            AMediaFormat_getInt64(fmt, AMEDIAFORMAT_KEY_DURATION, &dur);
            m_duration = static_cast<double>(dur) / 1000000.0;

            AMediaFormat_delete(fmt);
            return true;
        }
        AMediaFormat_delete(fmt);
    }
    return false;
}

// ─────────────────────────────────────────────────────────────
// Decode loop
// ─────────────────────────────────────────────────────────────
void DecoderNDK::startDecoding() {
    if (m_decoding.load(std::memory_order_relaxed)) return;
    m_decoding.store(true, std::memory_order_relaxed);
    m_finished.store(false, std::memory_order_relaxed);
    uint64_t gen = ++m_generation;
    m_thread = std::thread([this, gen]() { decodeLoop(gen); });
}

bool DecoderNDK::stopDecoding() {
    bool wasDecoding = m_decoding.exchange(false, std::memory_order_acq_rel);
    if (wasDecoding) ++m_generation;  // invalidate old thread's generation
    bool joined = true;
    if (m_thread.joinable()) joined = paimon::timedJoin(m_thread, std::chrono::seconds(3));
    return joined;
}

void DecoderNDK::decodeLoop(uint64_t gen) {
    m_threadRunning.store(true, std::memory_order_release);

    bool inputDone = false;

    while (m_decoding.load(std::memory_order_relaxed) &&
           gen == m_generation.load(std::memory_order_relaxed)) {
        // ── Feed input ──
        if (!inputDone) {
            // Use a short timeout (1 ms) so the thread checks m_decoding
            // and the generation counter frequently and exits promptly on stop.
            ssize_t inputIdx = AMediaCodec_dequeueInputBuffer(m_codec, 1000);
            if (inputIdx >= 0) {
                size_t bufSize = 0;
                uint8_t* inputBuf = AMediaCodec_getInputBuffer(m_codec, inputIdx, &bufSize);
                if (inputBuf) {
                    int sampleSize = AMediaExtractor_readSampleData(m_extractor, inputBuf, bufSize);
                    if (sampleSize < 0) {
                        // EOS
                        AMediaCodec_queueInputBuffer(m_codec, inputIdx, 0, 0, 0,
                                                     AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                        inputDone = true;
                    } else {
                        int64_t presentationTimeUs = AMediaExtractor_getSampleTime(m_extractor);
                        AMediaCodec_queueInputBuffer(m_codec, inputIdx, 0,
                                                     sampleSize, presentationTimeUs, 0);
                        AMediaExtractor_advance(m_extractor);
                    }
                }
            }
        }

        // ── Drain output ──
        if (m_ring.isFull()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        AMediaCodecBufferInfo info;
        // Use a short timeout (1 ms) so the thread checks m_decoding
        // and the generation counter frequently and exits promptly on stop.
        ssize_t outputIdx = AMediaCodec_dequeueOutputBuffer(m_codec, &info, 1000);

        if (outputIdx >= 0) {
            if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
                AMediaCodec_releaseOutputBuffer(m_codec, outputIdx, false);
                m_finished.store(true, std::memory_order_release);
                break;
            }

            if (m_surface && m_useSurface) {
                // Surface mode: render directly, no CPU copy
                AMediaCodec_releaseOutputBuffer(m_codec, outputIdx, true);
            } else {
                // Buffer mode: copy YUV data to ring buffer
                size_t outSize = 0;
                uint8_t* outBuf = AMediaCodec_getOutputBuffer(m_codec, outputIdx, &outSize);

                auto* slot = m_ring.nextWrite();
                if (slot && outBuf) {
                    // Android MediaCodec output is typically NV12 or YV12
                    // We detect stride from info and copy accordingly
                    int stride = std::max(m_width, m_outputStride);
                    int sliceHeight = std::max(m_height, m_outputSliceHeight);
                    int uvH = (m_height + 1) / 2;
                    int uvW = (m_width + 1) / 2;
                    bool semiPlanar = isSemiPlanarFormat(m_outputColorFormat);
                    size_t yPlaneBytes = static_cast<size_t>(stride) * sliceHeight;
                    size_t minYBytes = static_cast<size_t>(stride) * m_height;
                    size_t neededSemi = yPlaneBytes + static_cast<size_t>(stride) * uvH;
                    size_t neededPlanar = yPlaneBytes + static_cast<size_t>(std::max(uvW, stride / 2)) * uvH * 2;
                    if (outSize < minYBytes) {
                        AMediaCodec_releaseOutputBuffer(m_codec, outputIdx, false);
                        continue;
                    }
                    if (semiPlanar && outSize < neededSemi && outSize >= neededPlanar) {
                        semiPlanar = false;
                    }

                    // Assume NV12 (most common from HW decoder)
                    // Y plane
                    for (int r = 0; r < m_height; ++r) {
                        std::memcpy(slot->planeY + r * slot->strideY,
                                    outBuf + static_cast<size_t>(r) * stride,
                                    m_width);
                    }
                    if (semiPlanar && outSize >= neededSemi) {
                        uint8_t* uvStart = outBuf + yPlaneBytes;
                        for (int r = 0; r < uvH; ++r) {
                            for (int c = 0; c < uvW; ++c) {
                                size_t idx = static_cast<size_t>(r) * stride + c * 2;
                                slot->planeCb[r * slot->strideCb + c] = uvStart[idx];
                                slot->planeCr[r * slot->strideCr + c] = uvStart[idx + 1];
                            }
                        }
                    } else if (outSize >= neededPlanar) {
                        int uvStride = std::max(uvW, stride / 2);
                        uint8_t* uStart = outBuf + yPlaneBytes;
                        uint8_t* vStart = uStart + static_cast<size_t>(uvStride) * uvH;
                        for (int r = 0; r < uvH; ++r) {
                            std::memcpy(slot->planeCb + r * slot->strideCb,
                                        uStart + static_cast<size_t>(r) * uvStride,
                                        uvW);
                            std::memcpy(slot->planeCr + r * slot->strideCr,
                                        vStart + static_cast<size_t>(r) * uvStride,
                                        uvW);
                        }
                    } else {
                        AMediaCodec_releaseOutputBuffer(m_codec, outputIdx, false);
                        continue;
                    }

                    slot->pts = static_cast<double>(info.presentationTimeUs) / 1000000.0;
                    m_ring.commitWrite();
                }

                AMediaCodec_releaseOutputBuffer(m_codec, outputIdx, false);
            }
        } else if (outputIdx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            updateOutputFormat();
        }
        // AMEDIACODEC_INFO_TRY_AGAIN_LATER: just loop
    }

    m_threadRunning.store(false, std::memory_order_release);
}

// ─────────────────────────────────────────────────────────────
// Consume / Seek / Accessors
// ─────────────────────────────────────────────────────────────
bool DecoderNDK::consumeFrame(Frame& outFrame) {
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

void DecoderNDK::seekTo(double seconds) {
    if (!m_extractor) return;
    bool wasDecoding = m_decoding.load(std::memory_order_relaxed);
    stopDecoding();

    while (m_ring.nextRead()) m_ring.commitRead();

    AMediaExtractor_seekTo(m_extractor,
                           static_cast<int64_t>(seconds * 1000000.0),
                           AMEDIAEXTRACTOR_SEEK_CLOSEST_SYNC);

    if (m_codec) {
        AMediaCodec_flush(m_codec);
    }
    m_finished.store(false, std::memory_order_relaxed);

    if (wasDecoding) startDecoding();
}

bool DecoderNDK::skipFrame() {
    return m_ring.skipRead();
}

double DecoderNDK::getDuration() const { return m_duration; }
int DecoderNDK::getWidth()  const { return m_width; }
int DecoderNDK::getHeight() const { return m_height; }
bool DecoderNDK::isFinished() const {
    return m_finished.load(std::memory_order_acquire);
}

double DecoderNDK::peekNextPTS() const {
    return m_ring.peekNextPTS();
}

void DecoderNDK::setSurface(ANativeWindow* window) {
    m_surface = window;
    m_useSurface = (window != nullptr);
}

// ─────────────────────────────────────────────────────────────
// Close
// ─────────────────────────────────────────────────────────────
void DecoderNDK::closeInternal() {
    stopDecoding();

    // If the decode thread was detached (timedJoin timed out), wait for it to
    // actually finish before releasing the codec and extractor it may still be
    // accessing.  With 1 ms dequeue timeouts the thread exits within ~2 ms, so
    // this busy-wait is very short in practice.
    if (m_threadRunning.load(std::memory_order_acquire)) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
        while (m_threadRunning.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (m_threadRunning.load(std::memory_order_acquire)) {
            geode::log::warn("DecoderNDK: thread still running during destruction after 200 ms wait");
        }
    }

    if (m_codec) {
        AMediaCodec_stop(m_codec);
        AMediaCodec_delete(m_codec);
        m_codec = nullptr;
    }
    if (m_extractor) {
        AMediaExtractor_delete(m_extractor);
        m_extractor = nullptr;
    }
    m_trackIdx = -1;
}

} // namespace paimon

#endif // USE_MEDIA_NDK
