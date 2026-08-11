#include "DecoderNDK.hpp"

#if defined(USE_MEDIA_NDK)

#include <Geode/loader/Log.hpp>
#include "../../utils/TimedJoin.hpp"
#include <libyuv/planar_functions.h>
#include <cstring>
#include <chrono>
#include <algorithm>
#include <dlfcn.h>
#include <thread>

namespace paimon {

namespace {

// AImageReader is API 24 but Geode targets minSdk 23, so bind at runtime
// instead of link time.
struct ImageReaderApi {
    media_status_t (*newReader)(int32_t, int32_t, int32_t, int32_t, AImageReader**) = nullptr;
    void           (*deleteReader)(AImageReader*) = nullptr;
    media_status_t (*getWindow)(AImageReader*, ANativeWindow**) = nullptr;
    media_status_t (*acquireNext)(AImageReader*, AImage**) = nullptr;
    void           (*imageDelete)(AImage*) = nullptr;
    media_status_t (*planeData)(const AImage*, int, uint8_t**, int*) = nullptr;
    media_status_t (*planeRowStride)(const AImage*, int, int32_t*) = nullptr;
    media_status_t (*planePixelStride)(const AImage*, int, int32_t*) = nullptr;
    bool ok = false;
};

const ImageReaderApi& imageReaderApi() {
    static ImageReaderApi api = []() {
        ImageReaderApi a;
        void* lib = dlopen("libmediandk.so", RTLD_NOW | RTLD_LOCAL);
        if (!lib) return a;
        auto sym = [lib](const char* name) { return dlsym(lib, name); };
        a.newReader        = reinterpret_cast<decltype(a.newReader)>(sym("AImageReader_new"));
        a.deleteReader     = reinterpret_cast<decltype(a.deleteReader)>(sym("AImageReader_delete"));
        a.getWindow        = reinterpret_cast<decltype(a.getWindow)>(sym("AImageReader_getWindow"));
        a.acquireNext      = reinterpret_cast<decltype(a.acquireNext)>(sym("AImageReader_acquireNextImage"));
        a.imageDelete      = reinterpret_cast<decltype(a.imageDelete)>(sym("AImage_delete"));
        a.planeData        = reinterpret_cast<decltype(a.planeData)>(sym("AImage_getPlaneData"));
        a.planeRowStride   = reinterpret_cast<decltype(a.planeRowStride)>(sym("AImage_getPlaneRowStride"));
        a.planePixelStride = reinterpret_cast<decltype(a.planePixelStride)>(sym("AImage_getPlanePixelStride"));
        a.ok = a.newReader && a.deleteReader && a.getWindow && a.acquireNext &&
               a.imageDelete && a.planeData && a.planeRowStride && a.planePixelStride;
        if (!a.ok) {
            geode::log::info("DecoderNDK: AImageReader unavailable, using raw output buffers");
        }
        return a;
    }();
    return api;
}

// Copy one chroma plane honouring AImage's pixel stride: 1 is planar, 2 means
// the U/V samples are interleaved in a shared NV12 buffer.
void copyChromaPlane(const uint8_t* src, int rowStride, int pixelStride,
                     uint8_t* dst, int dstStride, int w, int h) {
    if (pixelStride == 1) {
        for (int r = 0; r < h; ++r) {
            std::memcpy(dst + r * dstStride, src + static_cast<size_t>(r) * rowStride, w);
        }
        return;
    }
    for (int r = 0; r < h; ++r) {
        const uint8_t* srcRow = src + static_cast<size_t>(r) * rowStride;
        uint8_t* dstRow = dst + r * dstStride;
        for (int c = 0; c < w; ++c) {
            dstRow[c] = srcRow[c * pixelStride];
        }
    }
}

}

// Local copies of OMX color formats; the NDK header is not available everywhere.
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

    geode::log::info("DecoderNDK: output format - {}x{} stride={} slice={} color-format=0x{:X}",
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

// Prefer AImageReader; it normalises the output layout across vendors.
    m_useImageReader = !(m_surface && m_useSurface) && setupImageReader();

    ANativeWindow* target = nullptr;
    if (m_surface && m_useSurface)      target = m_surface;
    else if (m_useImageReader)          target = m_readerWindow;

// Avoid opaque output: the raw-buffer path needs CPU-readable planes.
    if (!target) {
        AMediaFormat_setInt32(trackFmt, "color-format", kCF_YUV420Flexible);
    }

    media_status_t status = AMediaCodec_configure(m_codec, trackFmt, target, nullptr, 0);
    AMediaFormat_delete(trackFmt);

    if (status != AMEDIA_OK) {
        geode::log::warn("DecoderNDK: configure failed ({})", static_cast<int>(status));
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

bool DecoderNDK::setupImageReader() {
    const auto& api = imageReaderApi();
    if (!api.ok || m_width <= 0 || m_height <= 0) return false;

    // Four slots: enough for the codec to stay ahead without holding the ring.
    if (api.newReader(m_width, m_height, AIMAGE_FORMAT_YUV_420_888, 4, &m_imageReader) != AMEDIA_OK
        || !m_imageReader) {
        m_imageReader = nullptr;
        return false;
    }

    if (api.getWindow(m_imageReader, &m_readerWindow) != AMEDIA_OK || !m_readerWindow) {
        releaseImageReader();
        return false;
    }

    geode::log::info("DecoderNDK: using AImageReader ({}x{}, YUV_420_888)", m_width, m_height);
    return true;
}

void DecoderNDK::releaseImageReader() {
    const auto& api = imageReaderApi();
    if (m_imageReader && api.deleteReader) {
        api.deleteReader(m_imageReader);
    }
    m_imageReader = nullptr;
    m_readerWindow = nullptr;
    m_useImageReader = false;
}

bool DecoderNDK::drainImageReader(int64_t presentationTimeUs) {
    const auto& api = imageReaderApi();
    if (!m_imageReader) return false;

    AImage* image = nullptr;
    // The buffer lands a moment after releaseOutputBuffer; give it a few tries.
    for (int attempt = 0; attempt < 8 && !image; ++attempt) {
        if (api.acquireNext(m_imageReader, &image) == AMEDIA_OK && image) break;
        image = nullptr;
        if (!m_decoding.load(std::memory_order_relaxed)) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!image) return false;

    auto* slot = m_ring.nextWrite();
    if (!slot) {
        api.imageDelete(image);
        return false;
    }

    uint8_t* yData = nullptr;
    uint8_t* uData = nullptr;
    uint8_t* vData = nullptr;
    int yLen = 0, uLen = 0, vLen = 0;
    int32_t yRow = 0, uRow = 0, vRow = 0;
    int32_t uPix = 1, vPix = 1;

    bool ok = api.planeData(image, 0, &yData, &yLen) == AMEDIA_OK &&
              api.planeData(image, 1, &uData, &uLen) == AMEDIA_OK &&
              api.planeData(image, 2, &vData, &vLen) == AMEDIA_OK &&
              api.planeRowStride(image, 0, &yRow) == AMEDIA_OK &&
              api.planeRowStride(image, 1, &uRow) == AMEDIA_OK &&
              api.planeRowStride(image, 2, &vRow) == AMEDIA_OK &&
              api.planePixelStride(image, 1, &uPix) == AMEDIA_OK &&
              api.planePixelStride(image, 2, &vPix) == AMEDIA_OK;

    if (!ok || !yData || !uData || !vData) {
        api.imageDelete(image);
        return false;
    }

    int uvW = (m_width + 1) / 2;
    int uvH = (m_height + 1) / 2;

    int yCopy = std::min(m_width, yRow);
    for (int r = 0; r < m_height; ++r) {
        std::memcpy(slot->planeY + r * slot->strideY,
                    yData + static_cast<size_t>(r) * yRow, yCopy);
    }

    // NV12-backed images expose V as U+1 in one buffer; libyuv splits that fast.
    if (uPix == 2 && vPix == 2 && vData == uData + 1) {
        libyuv::SplitUVPlane(uData, uRow,
                             slot->planeCb, slot->strideCb,
                             slot->planeCr, slot->strideCr,
                             uvW, uvH);
    } else {
        copyChromaPlane(uData, uRow, uPix, slot->planeCb, slot->strideCb, uvW, uvH);
        copyChromaPlane(vData, vRow, vPix, slot->planeCr, slot->strideCr, uvW, uvH);
    }

    slot->pts = static_cast<double>(presentationTimeUs) / 1000000.0;
    m_ring.commitWrite();
    api.imageDelete(image);
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
            if (!m_decodeThreadDetached.exchange(true, std::memory_order_acq_rel))
                noteDetachedDecoder("MediaNDK");
        }
    }
}

void DecoderNDK::decodeLoop() {
    bool inputDone = false;
    int frameCount = 0;
    int skippedBeforeFormat = 0;  // count buffers skipped waiting for format change

    while (m_decoding.load(std::memory_order_relaxed)) {
        if (!inputDone) {
            ssize_t inputIdx = AMediaCodec_dequeueInputBuffer(m_codec, 5000);
            if (inputIdx >= 0) {
                size_t bufSize = 0;
                uint8_t* inputBuf = AMediaCodec_getInputBuffer(m_codec, inputIdx, &bufSize);
                if (inputBuf) {
                    int sampleSize = AMediaExtractor_readSampleData(m_extractor, inputBuf, bufSize);
                    if (sampleSize < 0 && m_looping.load(std::memory_order_relaxed)) {
// Rewind the demuxer instead of draining: PTS restarts at 0 and the ring
// stays fed across the loop point.
                        AMediaExtractor_seekTo(m_extractor, 0, AMEDIAEXTRACTOR_SEEK_CLOSEST_SYNC);
                        sampleSize = AMediaExtractor_readSampleData(m_extractor, inputBuf, bufSize);
                    }
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
                AMediaCodec_releaseOutputBuffer(m_codec, outputIdx, true);
                continue;
            }

            if (m_useImageReader) {
                int64_t pts = info.presentationTimeUs;
                AMediaCodec_releaseOutputBuffer(m_codec, outputIdx, true);
                if (drainImageReader(pts)) {
                    ++frameCount;
                    if (frameCount == 1) {
                        geode::log::info("DecoderNDK: first frame via AImageReader ({}x{})",
                                         m_width, m_height);
                    }
                }
                continue;
            }

// Do not touch buffers until the output layout is known.
            if (!m_outputFormatValid.load(std::memory_order_acquire)) {
                ++skippedBeforeFormat;
// Some drivers omit INFO_OUTPUT_FORMAT_CHANGED; query after a few buffers.
                if (skippedBeforeFormat >= 3) {
                    geode::log::info("DecoderNDK: no FORMAT_CHANGED after {} buffers, "
                                     "force-querying output format", skippedBeforeFormat);
                    updateOutputFormat();
                    if (!m_outputFormatValid.load(std::memory_order_acquire)) {
                        geode::log::warn("DecoderNDK: forced format query still invalid, aborting");
                        AMediaCodec_releaseOutputBuffer(m_codec, outputIdx, false);
                        m_finished.store(true, std::memory_order_release);
                        break;
                    }
                } else {
                    AMediaCodec_releaseOutputBuffer(m_codec, outputIdx, false);
                    continue;
                }
            }

            if (!isReadableColorFormat(m_outputColorFormat)) {
// Opaque, tiled, or unknown formats are not CPU-readable.
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

            if (outSize < minYBytes) {
                geode::log::warn("DecoderNDK: output buffer too small ({} < {})",
                                 outSize, minYBytes);
                AMediaCodec_releaseOutputBuffer(m_codec, outputIdx, false);
                continue;
            }

// Some Samsung/Mali drivers report semi-planar for planar-sized buffers.
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
                libyuv::SplitUVPlane(outBuf + yPlaneBytes, stride,
                                     slot->planeCb, slot->strideCb,
                                     slot->planeCr, slot->strideCr,
                                     uvW, uvH);
            } else if (outSize >= neededPlanar) {
// Planar formats use I420 order (Y, Cb, Cr).
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
    }
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
// Revalidate after flush in case the driver changes stride or layout.

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

    if (m_decodeThreadDetached.load(std::memory_order_acquire)) {
        geode::log::warn("DecoderNDK: closeInternal: decode thread detached, "
                         "leaking codec/extractor to avoid UAF");
        m_codec = nullptr;
        m_extractor = nullptr;
        m_imageReader = nullptr;
        m_readerWindow = nullptr;
        m_useImageReader = false;
        m_codecConfigured = false;
        m_codecStarted = false;
        m_trackIdx = -1;
        return;
    }

    if (m_codec) {
// Some drivers crash if stop() is called before the codec starts.
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
    releaseImageReader();
    m_codecConfigured = false;
    m_codecStarted = false;
    m_trackIdx = -1;
    m_outputFormatValid.store(false, std::memory_order_release);
}

}

#endif
