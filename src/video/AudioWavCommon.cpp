#include "AudioWavCommon.hpp"
#include "AudioExtractor.hpp"

#include <Geode/loader/Log.hpp>
#include <Geode/utils/string.hpp>
#include <cstdio>
#include <filesystem>
#include <functional>

namespace paimon::video {

namespace detail {

std::mutex& audioExtractorMutex() {
    static std::mutex mtx;
    return mtx;
}

static std::filesystem::path getAudioCacheDir() {
    auto dir = std::filesystem::temp_directory_path() / "paimbnails_audio_cache";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::string makeWavPath(const std::string& videoPath) {
    std::size_t h = std::hash<std::string>{}(videoPath);
    auto wavFile = getAudioCacheDir() / ("vid_" + std::to_string(h) + ".wav");
    return geode::utils::string::pathToString(wavFile);
}

bool writeWavFile(const std::string& wavPath,
                  const uint8_t* pcm, size_t pcmBytes,
                  uint16_t numChannels, uint32_t sampleRate,
                  uint16_t bitsPerSample) {
    if (!pcm || pcmBytes == 0 || numChannels == 0 || sampleRate == 0 || bitsPerSample == 0) {
        return false;
    }

    FILE* fp = nullptr;
#ifdef _MSC_VER
    fopen_s(&fp, wavPath.c_str(), "wb");
#else
    fp = fopen(wavPath.c_str(), "wb");
#endif
    if (!fp) {
        geode::log::warn("[AudioExtract] failed to open WAV for writing: {}", wavPath);
        return false;
    }

    WavHeader hdr;
    hdr.numChannels   = numChannels;
    hdr.sampleRate    = sampleRate;
    hdr.bitsPerSample = bitsPerSample;
    hdr.blockAlign    = static_cast<uint16_t>(numChannels * bitsPerSample / 8);
    hdr.byteRate      = sampleRate * hdr.blockAlign;
    hdr.dataSize      = static_cast<uint32_t>(pcmBytes);
    hdr.fileSize      = static_cast<uint32_t>(sizeof(WavHeader) - 8 + pcmBytes);

    bool ok = fwrite(&hdr, sizeof(hdr), 1, fp) == 1
           && fwrite(pcm, 1, pcmBytes, fp) == pcmBytes;
    fclose(fp);

    if (!ok) {
        std::error_code ec;
        std::filesystem::remove(wavPath, ec);
        geode::log::warn("[AudioExtract] WAV write failed: {}", wavPath);
        return false;
    }
    return true;
}

} // namespace detail

std::string getCachedWavPath(const std::string& videoPath) {
    auto wavPath = detail::makeWavPath(videoPath);
    std::error_code ec;
    if (std::filesystem::exists(wavPath, ec) && !ec) return wavPath;
    return {};
}

void cleanupAudioCache(const std::string& videoPath) {
    auto wavPath = detail::makeWavPath(videoPath);
    if (!wavPath.empty()) {
        std::error_code ec;
        std::filesystem::remove(wavPath, ec);
    }
}

// Fallback extractor for platforms with no native audio backend (e.g. Linux).
// Windows/Android/Apple provide their own extractAudioToWav in dedicated TUs.
#if !defined(USE_MEDIA_FOUNDATION) && !defined(USE_MEDIA_NDK) && !defined(USE_AV_FOUNDATION)
std::string extractAudioToWav(const std::string&) { return {}; }
#endif

} // namespace paimon::video
