#include "AudioExtractor.hpp"

#if defined(USE_MEDIA_NDK)

#include "AudioWavCommon.hpp"
#include "../core/RuntimeLifecycle.hpp"
#include <Geode/loader/Log.hpp>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <vector>

namespace paimon::video {

// mediacodec output is 16-bit pcm by default.
AudioPcm extractAudioToPcm(const std::string& videoPath) {
    std::lock_guard lock(detail::audioExtractorMutex());

    if (videoPath.empty()) return {};

    AMediaExtractor* extractor = AMediaExtractor_new();
    if (!extractor) return {};

    if (AMediaExtractor_setDataSource(extractor, videoPath.c_str()) != AMEDIA_OK) {
        geode::log::warn("[AudioExtract] setDataSource failed: {}", videoPath);
        AMediaExtractor_delete(extractor);
        return {};
    }

    int audioTrack = -1;
    AMediaFormat* trackFmt = nullptr;
    const char* mime = nullptr;
    size_t numTracks = AMediaExtractor_getTrackCount(extractor);
    for (size_t i = 0; i < numTracks; ++i) {
        AMediaFormat* fmt = AMediaExtractor_getTrackFormat(extractor, i);
        const char* m = nullptr;
        if (AMediaFormat_getString(fmt, AMEDIAFORMAT_KEY_MIME, &m) && m &&
            strncmp(m, "audio/", 6) == 0) {
            audioTrack = static_cast<int>(i);
            trackFmt = fmt;
            mime = m;
            break;
        }
        AMediaFormat_delete(fmt);
    }

    if (audioTrack < 0 || !trackFmt || !mime) {
        geode::log::warn("[AudioExtract] no audio track in {}", videoPath);
        AMediaExtractor_delete(extractor);
        return {};
    }

    AMediaCodec* codec = AMediaCodec_createDecoderByType(mime);
    if (!codec) {
        geode::log::warn("[AudioExtract] createDecoderByType({}) failed", mime);
        AMediaFormat_delete(trackFmt);
        AMediaExtractor_delete(extractor);
        return {};
    }

    // track-format fallback until info_output_format_changed arrives.
    int32_t sampleRate = 0;
    int32_t channels = 0;
    AMediaFormat_getInt32(trackFmt, AMEDIAFORMAT_KEY_SAMPLE_RATE, &sampleRate);
    AMediaFormat_getInt32(trackFmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &channels);

    if (AMediaCodec_configure(codec, trackFmt, nullptr, nullptr, 0) != AMEDIA_OK ||
        AMediaCodec_start(codec) != AMEDIA_OK) {
        geode::log::warn("[AudioExtract] codec configure/start failed");
        AMediaCodec_delete(codec);
        AMediaFormat_delete(trackFmt);
        AMediaExtractor_delete(extractor);
        return {};
    }
    AMediaFormat_delete(trackFmt);
    AMediaExtractor_selectTrack(extractor, audioTrack);

    std::vector<uint8_t> pcm;
    pcm.reserve(1u << 20);
    bool inputDone = false;
    bool failed = false;
    auto lastProgress = std::chrono::steady_clock::now();
    constexpr size_t kMaxPcmBytes = 256u * 1024u * 1024u;

    while (true) {
        if (paimon::isRuntimeShuttingDown() ||
            std::chrono::steady_clock::now() - lastProgress > std::chrono::seconds(10)) {
            failed = true;
            break;
        }
        if (!inputDone) {
            ssize_t inIdx = AMediaCodec_dequeueInputBuffer(codec, 5000);
            if (inIdx >= 0) {
                size_t bufSize = 0;
                uint8_t* inBuf = AMediaCodec_getInputBuffer(codec, inIdx, &bufSize);
                if (inBuf) {
                    int sampleSize = AMediaExtractor_readSampleData(extractor, inBuf, bufSize);
                    if (sampleSize < 0) {
                        if (AMediaCodec_queueInputBuffer(codec, inIdx, 0, 0, 0,
                                AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != AMEDIA_OK) {
                            failed = true;
                            break;
                        }
                        inputDone = true;
                    } else {
                        int64_t pts = AMediaExtractor_getSampleTime(extractor);
                        if (AMediaCodec_queueInputBuffer(codec, inIdx, 0, sampleSize, pts, 0) != AMEDIA_OK) {
                            failed = true;
                            break;
                        }
                        AMediaExtractor_advance(extractor);
                    }
                } else { failed = true; break; }
            } else if (inIdx != AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
                failed = true;
                break;
            }
        }

        AMediaCodecBufferInfo info;
        ssize_t outIdx = AMediaCodec_dequeueOutputBuffer(codec, &info, 5000);
        if (outIdx >= 0) {
            lastProgress = std::chrono::steady_clock::now();
            if (info.size > 0) {
                size_t outSize = 0;
                uint8_t* outBuf = AMediaCodec_getOutputBuffer(codec, outIdx, &outSize);
                if (outBuf && info.offset >= 0 && static_cast<size_t>(info.offset) <= outSize &&
                    static_cast<size_t>(info.size) <= outSize - static_cast<size_t>(info.offset) &&
                    static_cast<size_t>(info.size) <= kMaxPcmBytes - pcm.size()) {
                    uint8_t* start = outBuf + info.offset;
                    pcm.insert(pcm.end(), start, start + info.size);
                } else { failed = true; }
            }
            bool eos = (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;
            AMediaCodec_releaseOutputBuffer(codec, outIdx, false);
            if (eos || failed) break;
        } else if (outIdx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            AMediaFormat* outFmt = AMediaCodec_getOutputFormat(codec);
            if (outFmt) {
                AMediaFormat_getInt32(outFmt, AMEDIAFORMAT_KEY_SAMPLE_RATE, &sampleRate);
                AMediaFormat_getInt32(outFmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &channels);
                AMediaFormat_delete(outFmt);
            }
        } else if (outIdx != AMEDIACODEC_INFO_TRY_AGAIN_LATER &&
                   outIdx != AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
            failed = true;
            break;
        }
    }

    AMediaCodec_stop(codec);
    AMediaCodec_delete(codec);
    AMediaExtractor_delete(extractor);

    if (failed || pcm.empty() || sampleRate <= 0 || channels <= 0) {
        geode::log::warn("[AudioExtract] no audio data (bytes={} rate={} ch={}) from {}",
                         pcm.size(), sampleRate, channels, videoPath);
        return {};
    }

    AudioPcm out;
    out.data          = std::move(pcm);
    out.channels      = channels;
    out.sampleRate    = sampleRate;
    out.bitsPerSample = 16;
    geode::log::info("[AudioExtract] decoded {} bytes ({} ch, {} Hz) from {}",
                     out.data.size(), out.channels, out.sampleRate, videoPath);
    return out;
}

} // namespace paimon::video

#endif // use_media_ndk
