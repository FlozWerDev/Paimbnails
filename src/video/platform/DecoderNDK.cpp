#include "DecoderNDK.hpp"

#if defined(USE_MEDIA_NDK)

#include <Geode/loader/Log.hpp>
#include "../../utils/TimedJoin.hpp"
#include <cstring>
#include <chrono>
#include <algorithm>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define PAIMON_HAVE_NEON 1
#endif

namespace paimon {

// Android color-format constants (from OMX_IVCommon.h / MediaCodecInfo.java)
// We can't rely on <OMX_IVCommon.h> being on the NDK include path, so we
// redeclare just the formats we care about.
static constexpr int kCF_YUV420Planar           = 19;   // OMX_COLOR_FormatYUV420Planar (I420)
static constexpr int kCF_YUV420SemiPlanar       = 21;   // OMX_COLOR_FormatYUV420SemiPlanar (NV12)
static constexpr int kCF_YUV420PackedPlanar     = 20;   // OMX_COLOR_FormatYUV420PackedPlanar
static constexpr int kCF_YUV420PackedSemiPlanar = 39;   // OMX_COLOR_FormatYUV420PackedSemiPlanar
static constexpr int kCF_YUV420Flexible         = 0x7F420888; // COLOR_FormatYUV420Flexible — semi-planar in practice
static constexpr int kCF_QCOM_YUV420SemiPlanar  = 0x7FA30C00; // Qualcomm vendor NV12
static constexpr int kCF_QCOM_YUV420SP32m       = 0x7FA30C04; // Qualcomm tiled
static constexpr int kCF_AndroidOpaque          = 0x7F000789; // NOT CPU-readable

static int getFormatInt32(AMediaFormat* fmt, const char* key, int fallback) {
    int32_t value = fallback;
    if (fmt) AMediaFormat_getInt32(fmt, key, &value);
    return static_cast<int>(value);
}

bool DecoderNDK::isSemiPlanar(int colorFormat) const {
    switch (colorFormat) {
        case kCF_YUV420SemiPlanar:
        case kCF_YUV420PackedSemiPlanar:
        case kCF_YUV420Flexible:
        case kCF_QCOM_YUV420SemiPlanar:
            return true;
        default:
            return false;
    }
}

bool DecoderNDK::isReadableColorFormat(int colorFormat) const {
    switch (colorFormat) {
        case kCF_YUV420Planar:
        case kCF_YUV420PackedPlanar:
        case kCF_YUV420SemiPlanar:
        case kCF_YUV420PackedSemiPlanar:
        case kCF_YUV420Flexible:
        case kCF_QCOM_YUV420SemiPlanar:
            return true;
        case kCF_QCOM_YUV420SP32m:  // tiled — not trivially readable
        case kCF_AndroidOpaque:     // opaque — would need GL reading
            return false;
        default:
            // Unknown: assume unreadable; caller should bail out rather than
            // scribbling random bytes into frame planes.
            return false;
    }
}

void DecoderNDK::updateOutputFormat() {
    if (!m_codec) return;
    AMediaFormat* fmt = AMediaCodec_getOutputFormat(m_codec);
    if (!fmt) return;
    m_outputStride = std::max(1, getFormatInt32(fmt, "stride", m_width));
    m_outputSliceHeight = std::max(m_height, getFormatInt32(fmt, "slice-height", m_height));
    int cf = getFormatInt32(fmt, "color-format", m_outputColorFormat);
    m_outputColorFormat = cf;
    m_outputFormatValid.store(true, std::memory_order_release);

    geode::log::info("DecoderNDK: output format — {}x{} stride={} slice={} color-format=0x{:X}",
                     m_width, m_height, m_outputStride, m_outputSliceHeight,
                     static_cast<unsigned>(cf));

    if (!isReadableColorFormat(cf)) {
        geode::log::warn("DecoderNDK: color-format 0x{:X} is not CPU-readable, "
                         "decoding will be aborted", static_cast<unsigned>(cf));
    }
    AMediaFormat_delete(fmt);
}

bool DecoderNDK::open(const std::string& path) {
    closeInternal();
    m_decodeThreadDetached.store(false, std::memory_order_release);

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

    AMediaFormat* trackFmt = AMediaExtractor_getTrackFormat(m_extractor, m_trackIdx);
    const char* mime = nullptr;
    AMediaFormat_getString(trackFmt, AMEDIAFORMAT_KEY_MIME, &mime);
    if (!mime) {
        AMediaFormat_delete(trackFmt);
        closeInternal();
        return false;
    }

    m_codec = AMediaCodec_createDecoderByType(mime);
    if (!m_codec) {
        geode::log::warn("DecoderNDK: createDecoderByType({}) failed", mime);
        AMediaFormat_delete(trackFmt);
        closeInternal();
        return false;
    }

    // Request a CPU-readable output color format explicitly.
    // Without this request, some HW codecs default to kCF_AndroidOpaque which
    // cannot be mapped for CPU reads and causes segfaults in decodeLoop().
    if (!m_surface || !m_useSurface) {
        AMediaFormat_setInt32(trackFmt, "color-format", kCF_YUV420Flexible);
    }

    // Configure: with or without surface
    media_status_t status;
    if (m_surface && m_useSurface) {
        status = AMediaCodec_configure(m_codec, trackFmt, m_surface, nullptr, 0);
    } else {
        status = AMediaCodec_configure(m_codec, trackFmt, nullptr, nullptr, 0);
    }
    AMediaFormat_delete(trackFmt);

    if (status != AMEDIA_OK) {
        geode::log::warn("DecoderNDK: configure failed ({})", static_cast<int>(status));
        // Codec was created but not configured — release directly, don't call stop.
        AMediaCodec_delete(m_codec);
        m_codec = nullptr;
        closeInternal();
        return false;
    }
    m_codecConfigured = true;

    status = AMediaCodec_start(m_codec);
    if (status != AMEDIA_OK) {
        geode::log::warn("DecoderNDK: start failed ({})", static_cast<int>(status));
        closeInternal();
        return false;
    }
    m_codecStarted = true;

    AMediaExtractor_selectTrack(m_extractor, m_trackIdx);
    // Don't call updateOutputFormat here — before the first decoded frame
    // the driver may return a generic format. We'll update lazily from the
    // decode loop when we see AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED.
    m_outputStride = m_width;
    m_outputSliceHeight = m_height;
    m_outputColorFormat = 0;
    m_outputFormatValid.store(false, std::memory_order_release);

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

// NEON-optimized NV12 deinterleave
static inline void deinterleaveNV12Row(const uint8_t* uv, uint8_t* cb, uint8_t* cr, int uvW) {
#if PAIMON_HAVE_NEON
    int c = 0;
    int vecEnd = uvW & ~15;  // 16-pixel blocks
    for (; c < vecEnd; c += 16) {
        uint8x16x2_t d = vld2q_u8(uv + c * 2);
        vst1q_u8(cb + c, d.val[0]);
        vst1q_u8(cr + c, d.val[1]);
    }
    for (; c < uvW; ++c) {
        cb[c] = uv[c * 2];
        cr[c] = uv[c * 2 + 1];
    }
#else
    for (int c = 0; c < uvW; ++c) {
        cb[c] = uv[c * 2];
        cr[c] = uv[c * 2 + 1];
    }
#endif
}

void DecoderNDK::startDecoding() {
    // A detached worker (its join timed out) may still be running decodeLoop;
    // spawning a second thread would put two producers on the SPSC ring and call
    // the non-thread-safe AMediaCodec concurrently. Treat detached as terminal,
    // matching DecoderPLM/DecoderAVF and the isTerminal() contract that
    // VideoPlayer relies on to drop and recreate the decoder.
    if (m_decodeThreadDetached.load(std::memory_order_acquire)) return;
    if (m_decoding.load(std::memory_order_relaxed)) return;
    if (!m_codec || !m_codecStarted) return;
    m_decoding.store(true, std::memory_order_relaxed);
    m_finished.store(false, std::memory_order_relaxed);
    m_thread = std::thread(&DecoderNDK::decodeLoop, this);
}

void DecoderNDK::stopDecoding() {
    m_decoding.store(false, std::memory_order_relaxed);
    m_ring.wakeAll();  // unblock any cv waits ASAP
    if (m_thread.joinable()) {
        // If the codec is stuck, detach rather than block the main thread.
        if (!paimon::timedJoin(m_thread, std::chrono::seconds(3), &m_decoding)) {
            m_decodeThreadDetached.store(true, std::memory_order_release);
        }
    }
}

void DecoderNDK::decodeLoop() {
    bool inputDone = false;
    int frameCount = 0;
    int skippedBeforeFormat = 0;  // count buffers skipped waiting for format change

    while (m_decoding.load(std::memory_order_relaxed)) {
        // Feed input
        if (!inputDone) {
            ssize_t inputIdx = AMediaCodec_dequeueInputBuffer(m_codec, 5000);
            if (inputIdx >= 0) {
                size_t bufSize = 0;
                uint8_t* inputBuf = AMediaCodec_getInputBuffer(m_codec, inputIdx, &bufSize);
                if (inputBuf) {
                    int sampleSize = AMediaExtractor_readSampleData(m_extractor, inputBuf, bufSize);
                    if (sampleSize < 0) {
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

        // Drain output
        if (m_ring.isFull()) {
            m_ring.waitForWritable(50, &m_decoding);
            continue;
        }

        AMediaCodecBufferInfo info;
        ssize_t outputIdx = AMediaCodec_dequeueOutputBuffer(m_codec, &info, 5000);

        if (outputIdx >= 0) {
            if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
                AMediaCodec_releaseOutputBuffer(m_codec, outputIdx, false);
                m_finished.store(true, std::memory_order_release);
                break;
            }

            if (m_surface && m_useSurface) {
                // Surface mode: render directly, no CPU copy
                AMediaCodec_releaseOutputBuffer(m_codec, outputIdx, true);
                continue;
            }

            // Buffer mode — require a valid color format before touching data.
            // If we never saw INFO_OUTPUT_FORMAT_CHANGED, we still don't know
            // the real layout; refuse to read random bytes.
            if (!m_outputFormatValid.load(std::memory_order_acquire)) {
                ++skippedBeforeFormat;
                // Some devices (Samsung Exynos, older Qualcomm) never emit
                // INFO_OUTPUT_FORMAT_CHANGED.  After skipping a few buffers,
                // force-query the output format so we can start decoding.
                if (skippedBeforeFormat >= 3) {
                    geode::log::info("DecoderNDK: no FORMAT_CHANGED after {} buffers, "
                                     "force-querying output format", skippedBeforeFormat);
                    updateOutputFormat();
                    if (!m_outputFormatValid.load(std::memory_order_acquire)) {
                        // Still invalid — give up
                        geode::log::warn("DecoderNDK: forced format query still invalid, aborting");
                        AMediaCodec_releaseOutputBuffer(m_codec, outputIdx, false);
                        m_finished.store(true, std::memory_order_release);
                        break;
                    }
                    // Format is now valid — fall through to process this buffer
                } else {
                    // Treat first output buffers before format-change as setup:
                    // release them and let the codec emit OUTPUT_FORMAT_CHANGED.
                    AMediaCodec_releaseOutputBuffer(m_codec, outputIdx, false);
                    continue;
                }
            }

            if (!isReadableColorFormat(m_outputColorFormat)) {
                // Opaque / tiled / unknown format — we can't read this on CPU.
                // Release the buffer and abort decoding; the caller will fall
                // back to software decode (pl_mpeg) or fail gracefully.
                geode::log::warn("DecoderNDK: unreadable color-format 0x{:X}, stopping",
                                 static_cast<unsigned>(m_outputColorFormat));
                AMediaCodec_releaseOutputBuffer(m_codec, outputIdx, false);
                m_finished.store(true, std::memory_order_release);
                break;
            }

            size_t outSize = 0;
            uint8_t* outBuf = AMediaCodec_getOutputBuffer(m_codec, outputIdx, &outSize);
            if (!outBuf || outSize == 0) {
                AMediaCodec_releaseOutputBuffer(m_codec, outputIdx, false);
                continue;
            }

            auto* slot = m_ring.nextWrite();
            if (!slot) {
                AMediaCodec_releaseOutputBuffer(m_codec, outputIdx, false);
                m_ring.waitForWritable(50, &m_decoding);
                continue;
            }

            int stride = std::max(m_width, m_outputStride);
            int sliceHeight = std::max(m_height, m_outputSliceHeight);
            int uvH = (m_height + 1) / 2;
            int uvW = (m_width + 1) / 2;
            bool semiPlanar = isSemiPlanar(m_outputColorFormat);

            size_t yPlaneBytes = static_cast<size_t>(stride) * sliceHeight;
            size_t minYBytes   = static_cast<size_t>(stride) * m_height;
            size_t neededSemi  = yPlaneBytes + static_cast<size_t>(stride) * uvH;
            int planarUvStride = std::max(uvW, stride / 2);
            size_t neededPlanar = yPlaneBytes + static_cast<size_t>(planarUvStride) * uvH * 2;

            // Buffer too small to even contain Y plane — skip.
            if (outSize < minYBytes) {
                geode::log::warn("DecoderNDK: output buffer too small ({} < {})",
                                 outSize, minYBytes);
                AMediaCodec_releaseOutputBuffer(m_codec, outputIdx, false);
                continue;
            }

            // If the color-format says semi-planar but the buffer is sized for
            // planar (some Samsung/Mali drivers lie), switch layout.
            if (semiPlanar && outSize < neededSemi && outSize >= neededPlanar) {
                semiPlanar = false;
            }

            int yCopy = std::min(m_width, stride);
            if (slot->strideY == stride) {
                std::memcpy(slot->planeY, outBuf, static_cast<size_t>(stride) * m_height);
            } else {
                for (int r = 0; r < m_height; ++r) {
                    std::memcpy(slot->planeY + r * slot->strideY,
                                outBuf + static_cast<size_t>(r) * stride,
                                yCopy);
                }
            }

            if (semiPlanar && outSize >= neededSemi) {
                // NV12: interleaved UV plane — deinterleave row by row.
                const uint8_t* uvStart = outBuf + yPlaneBytes;
                for (int r = 0; r < uvH; ++r) {
                    deinterleaveNV12Row(uvStart + static_cast<size_t>(r) * stride,
                                        slot->planeCb + r * slot->strideCb,
                                        slot->planeCr + r * slot->strideCr,
                                        uvW);
                }
            } else if (outSize >= neededPlanar) {
                // I420 / YV12: two separate planes.
                // We default to I420 order (Y→Cb→Cr).  Format 0x13 (YUV420Planar)
                // is I420 on all documented devices.
                const uint8_t* uStart = outBuf + yPlaneBytes;
                const uint8_t* vStart = uStart + static_cast<size_t>(planarUvStride) * uvH;
                for (int r = 0; r < uvH; ++r) {
                    std::memcpy(slot->planeCb + r * slot->strideCb,
                                uStart + static_cast<size_t>(r) * planarUvStride,
                                uvW);
                    std::memcpy(slot->planeCr + r * slot->strideCr,
                                vStart + static_cast<size_t>(r) * planarUvStride,
                                uvW);
                }
            } else {
                // Buffer size mismatch — skip rather than scribble.
                AMediaCodec_releaseOutputBuffer(m_codec, outputIdx, false);
                continue;
            }

            slot->pts = static_cast<double>(info.presentationTimeUs) / 1000000.0;
            m_ring.commitWrite();
            ++frameCount;
            if (frameCount == 1) {
                geode::log::info("DecoderNDK: first frame decoded ({}x{}, color-format=0x{:X}, layout={})",
                                 m_width, m_height, static_cast<unsigned>(m_outputColorFormat),
                                 semiPlanar ? "semi-planar" : "planar");
            }

            AMediaCodec_releaseOutputBuffer(m_codec, outputIdx, false);
        } else if (outputIdx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            updateOutputFormat();
        }
        // AMEDIACODEC_INFO_TRY_AGAIN_LATER / BUFFERS_CHANGED: just loop
    }
}

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
    if (m_decodeThreadDetached.load(std::memory_order_acquire)) return;
    bool wasDecoding = m_decoding.load(std::memory_order_relaxed);
    stopDecoding();
    if (m_decodeThreadDetached.load(std::memory_order_acquire)) return;

    while (m_ring.nextRead()) m_ring.commitRead();

    AMediaExtractor_seekTo(m_extractor,
                           static_cast<int64_t>(seconds * 1000000.0),
                           AMEDIAEXTRACTOR_SEEK_CLOSEST_SYNC);

    if (m_codec && m_codecStarted) {
        AMediaCodec_flush(m_codec);
    }
    m_finished.store(false, std::memory_order_relaxed);
    // Output format should remain valid across a flush, but clear the flag
    // to force revalidation in case the driver emits a new format-change.
    // (Conservative — avoids reading stale stride if the driver changes format.)

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

double DecoderNDK::peekSecondPTS() const {
    return m_ring.peekSecondPTS();
}

const VideoFrame* DecoderNDK::peekFrame() {
    return m_ring.peekRead();
}

void DecoderNDK::releaseFrame() {
    if (m_ring.peekRead()) m_ring.commitRead();
}

void DecoderNDK::setSurface(ANativeWindow* window) {
    m_surface = window;
    m_useSurface = (window != nullptr);
}

void DecoderNDK::closeInternal() {
    stopDecoding();

    // Detached thread may still be in AMediaCodec_* calls; leak to avoid UAF.
    if (m_decodeThreadDetached.load(std::memory_order_acquire)) {
        geode::log::warn("DecoderNDK: closeInternal: decode thread detached, "
                         "leaking codec/extractor to avoid UAF");
        m_codec = nullptr;
        m_extractor = nullptr;
        m_codecConfigured = false;
        m_codecStarted = false;
        m_trackIdx = -1;
        return;
    }

    if (m_codec) {
        // Only call stop() if the codec was actually started.  Calling stop()
        // on a configured-but-not-started (or unconfigured) codec crashes on
        // some Mali/PowerVR/Adreno drivers.
        if (m_codecStarted) {
            AMediaCodec_stop(m_codec);
        }
        AMediaCodec_delete(m_codec);
        m_codec = nullptr;
    }
    if (m_extractor) {
        AMediaExtractor_delete(m_extractor);
        m_extractor = nullptr;
    }
    m_codecConfigured = false;
    m_codecStarted = false;
    m_trackIdx = -1;
    m_outputFormatValid.store(false, std::memory_order_release);
}

} // namespace paimon

#endif // USE_MEDIA_NDK
