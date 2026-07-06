#include "AudioExtractor.hpp"

#if defined(USE_MEDIA_NDK)

#include "AudioWavCommon.hpp"
#include <Geode/loader/Log.hpp>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>
#include <cstring>
#include <filesystem>
#include <vector>

namespace paimon::video {

// Decode the first audio track of a media file to interleaved 16-bit PCM via
// MediaCodec and write it to a cached WAV. MediaCodec audio decoders output
// ENCODING_PCM_16BIT by default, so the WAV is always 16-bit.
std::string extractAudioToWav(const std::string& videoPath) {
    std::lock_guard<std::mutex> lock(detail::audioExtractorMutex());

    if (videoPath.empty()) return {};

    auto wavPath = detail::makeWavPath(videoPath);
    std::error_code ec;
    if (std::filesystem::exists(wavPath, ec) && !ec) {
        geode::log::info("[AudioExtract] cached WAV exists: {}", wavPath);
        return wavPath;
    }

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

    // Fallback sample-rate / channel-count from the input track format, in case
    // the codec produces output before emitting INFO_OUTPUT_FORMAT_CHANGED.
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

    while (true) {
        if (!inputDone) {
            ssize_t inIdx = AMediaCodec_dequeueInputBuffer(codec, 5000);
            if (inIdx >= 0) {
                size_t bufSize = 0;
                uint8_t* inBuf = AMediaCodec_getInputBuffer(codec, inIdx, &bufSize);
                if (inBuf) {
                    int sampleSize = AMediaExtractor_readSampleData(extractor, inBuf, bufSize);
                    if (sampleSize < 0) {
                        AMediaCodec_queueInputBuffer(codec, inIdx, 0, 0, 0,
                                                     AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                        inputDone = true;
                    } else {
                        int64_t pts = AMediaExtractor_getSampleTime(extractor);
                        AMediaCodec_queueInputBuffer(codec, inIdx, 0, sampleSize, pts, 0);
                        AMediaExtractor_advance(extractor);
                    }
                }
            }
        }

        AMediaCodecBufferInfo info;
        ssize_t outIdx = AMediaCodec_dequeueOutputBuffer(codec, &info, 5000);
        if (outIdx >= 0) {
            if (info.size > 0) {
                size_t outSize = 0;
                uint8_t* outBuf = AMediaCodec_getOutputBuffer(codec, outIdx, &outSize);
                if (outBuf) {
                    uint8_t* start = outBuf + info.offset;
                    pcm.insert(pcm.end(), start, start + info.size);
                }
            }
            bool eos = (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;
            AMediaCodec_releaseOutputBuffer(codec, outIdx, false);
            if (eos) break;
        } else if (outIdx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            AMediaFormat* outFmt = AMediaCodec_getOutputFormat(codec);
            if (outFmt) {
                AMediaFormat_getInt32(outFmt, AMEDIAFORMAT_KEY_SAMPLE_RATE, &sampleRate);
                AMediaFormat_getInt32(outFmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &channels);
                AMediaFormat_delete(outFmt);
            }
        }
        // INFO_TRY_AGAIN_LATER / BUFFERS_CHANGED: just loop.
    }

    AMediaCodec_stop(codec);
    AMediaCodec_delete(codec);
    AMediaExtractor_delete(extractor);

    if (pcm.empty() || sampleRate <= 0 || channels <= 0) {
        geode::log::warn("[AudioExtract] no audio data (bytes={} rate={} ch={}) from {}",
                         pcm.size(), sampleRate, channels, videoPath);
        return {};
    }

    if (!detail::writeWavFile(wavPath, pcm.data(), pcm.size(),
                              static_cast<uint16_t>(channels),
                              static_cast<uint32_t>(sampleRate), 16)) {
        return {};
    }

    geode::log::info("[AudioExtract] extracted {} bytes of PCM audio to {}",
                     pcm.size(), wavPath);
    return wavPath;
}

} // namespace paimon::video

#endif // USE_MEDIA_NDK
