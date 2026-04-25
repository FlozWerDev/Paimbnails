#include "AudioExtractor.hpp"

#if defined(USE_MEDIA_FOUNDATION)

#include <Geode/loader/Log.hpp>
#include <Geode/utils/string.hpp>
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <codecapi.h>
#include <objbase.h>

namespace paimon::video {

// ─────────────────────────────────────────────────────────────
// Cache directory for extracted WAV files
// ─────────────────────────────────────────────────────────────
static std::filesystem::path getAudioCacheDir() {
    auto dir = std::filesystem::temp_directory_path() / "paimbnails_audio_cache";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

static std::string makeWavPath(const std::string& videoPath) {
    // Use hash of the full video path as the WAV filename
    std::size_t h = std::hash<std::string>{}(videoPath);
    auto wavFile = getAudioCacheDir() / ("vid_" + std::to_string(h) + ".wav");
    return geode::utils::string::pathToString(wavFile);
}

std::string getCachedWavPath(const std::string& videoPath) {
    auto wavPath = makeWavPath(videoPath);
    std::error_code ec;
    if (std::filesystem::exists(wavPath, ec) && !ec) return wavPath;
    return {};
}

void cleanupAudioCache(const std::string& videoPath) {
    auto wavPath = makeWavPath(videoPath);
    if (!wavPath.empty()) {
        std::error_code ec;
        std::filesystem::remove(wavPath, ec);
    }
}

// ─────────────────────────────────────────────────────────────
// Simple WAV writer
// ─────────────────────────────────────────────────────────────
struct WavHeader {
    char     riff[4]     = {'R','I','F','F'};
    uint32_t fileSize    = 0;          // filled at end
    char     wave[4]     = {'W','A','V','E'};
    char     fmt[4]      = {'f','m','t',' '};
    uint32_t fmtSize     = 16;
    uint16_t audioFormat = 1;          // PCM
    uint16_t numChannels = 0;
    uint32_t sampleRate  = 0;
    uint32_t byteRate    = 0;
    uint16_t blockAlign  = 0;
    uint16_t bitsPerSample = 0;
    char     data[4]     = {'d','a','t','a'};
    uint32_t dataSize    = 0;          // filled at end
};

// ─────────────────────────────────────────────────────────────
// Extract audio using MF Source Reader → transcode to PCM WAV
// ─────────────────────────────────────────────────────────────
std::string extractAudioToWav(const std::string& videoPath) {
    if (videoPath.empty()) return {};

    auto wavPath = makeWavPath(videoPath);
    std::error_code ec;
    if (std::filesystem::exists(wavPath, ec) && !ec) {
        geode::log::info("[AudioExtract] cached WAV exists: {}", wavPath);
        return wavPath;
    }

    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
    if (FAILED(hr)) {
        geode::log::warn("[AudioExtract] MFStartup failed (hr={})", hr);
        return {};
    }

    // Normalize path for MF
    std::string normPath = videoPath;
    std::replace(normPath.begin(), normPath.end(), '/', '\\');

    int wLen = MultiByteToWideChar(CP_UTF8, 0, normPath.c_str(), -1, nullptr, 0);
    if (wLen <= 0) { MFShutdown(); return {}; }
    auto* wPath = new (std::nothrow) wchar_t[wLen];
    if (!wPath) { MFShutdown(); return {}; }
    MultiByteToWideChar(CP_UTF8, 0, normPath.c_str(), -1, wPath, wLen);

    IMFSourceReader* reader = nullptr;
    hr = MFCreateSourceReaderFromURL(wPath, nullptr, &reader);
    delete[] wPath;

    if (FAILED(hr) || !reader) {
        geode::log::warn("[AudioExtract] MFCreateSourceReaderFromURL failed (hr={})", hr);
        MFShutdown();
        return {};
    }

    // Set the audio output format to PCM
    IMFMediaType* pcmType = nullptr;
    hr = MFCreateMediaType(&pcmType);
    if (FAILED(hr)) {
        reader->Release();
        MFShutdown();
        return {};
    }

    pcmType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    pcmType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);

    hr = reader->SetCurrentMediaType(
        static_cast<UINT32>(MF_SOURCE_READER_FIRST_AUDIO_STREAM),
        nullptr, pcmType);
    pcmType->Release();

    if (FAILED(hr)) {
        geode::log::warn("[AudioExtract] failed to set PCM audio output (hr={})", hr);
        reader->Release();
        MFShutdown();
        return {};
    }

    // Get the actual media type for WAV header info
    IMFMediaType* actualType = nullptr;
    hr = reader->GetCurrentMediaType(
        static_cast<UINT32>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), &actualType);
    if (FAILED(hr)) {
        reader->Release();
        MFShutdown();
        return {};
    }

    UINT32 nChannels = 0, sampleRate = 0, bitsPerSample = 0;
    actualType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &nChannels);
    actualType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sampleRate);
    actualType->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bitsPerSample);
    actualType->Release();

    if (nChannels == 0 || sampleRate == 0 || bitsPerSample == 0) {
        geode::log::warn("[AudioExtract] invalid audio params: ch={} rate={} bps={}",
                         nChannels, sampleRate, bitsPerSample);
        reader->Release();
        MFShutdown();
        return {};
    }

    // Write WAV file
    FILE* fp = nullptr;
    #ifdef _MSC_VER
    fopen_s(&fp, wavPath.c_str(), "wb");
    #else
    fp = fopen(wavPath.c_str(), "wb");
    #endif
    if (!fp) {
        geode::log::warn("[AudioExtract] failed to open WAV for writing: {}", wavPath);
        reader->Release();
        MFShutdown();
        return {};
    }

    // Write placeholder header
    WavHeader hdr;
    hdr.numChannels   = static_cast<uint16_t>(nChannels);
    hdr.sampleRate    = sampleRate;
    hdr.bitsPerSample = static_cast<uint16_t>(bitsPerSample);
    hdr.blockAlign    = static_cast<uint16_t>(nChannels * bitsPerSample / 8);
    hdr.byteRate      = sampleRate * hdr.blockAlign;
    fwrite(&hdr, sizeof(hdr), 1, fp);

    uint32_t totalDataBytes = 0;

    // Read all audio samples
    while (true) {
        DWORD streamIdx = 0, flags = 0;
        IMFSample* sample = nullptr;
        hr = reader->ReadSample(
            static_cast<UINT32>(MF_SOURCE_READER_FIRST_AUDIO_STREAM),
            0, &streamIdx, &flags, nullptr, &sample);

        if (FAILED(hr)) {
            if (sample) sample->Release();
            break;
        }

        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            if (sample) sample->Release();
            break;
        }

        if (!sample) continue;

        IMFMediaBuffer* buf = nullptr;
        hr = sample->GetBufferByIndex(0, &buf);
        if (FAILED(hr) || !buf) {
            sample->Release();
            continue;
        }

        BYTE* data = nullptr;
        DWORD bufLen = 0;
        hr = buf->Lock(&data, nullptr, &bufLen);
        if (SUCCEEDED(hr) && data && bufLen > 0) {
            fwrite(data, 1, bufLen, fp);
            totalDataBytes += bufLen;
            buf->Unlock();
        }
        buf->Release();
        sample->Release();
    }

    // Finalize WAV header
    hdr.dataSize = totalDataBytes;
    hdr.fileSize = static_cast<uint32_t>(sizeof(WavHeader) - 8 + totalDataBytes);
    fseek(fp, 0, SEEK_SET);
    fwrite(&hdr, sizeof(hdr), 1, fp);
    fclose(fp);

    reader->Release();
    MFShutdown();

    if (totalDataBytes == 0) {
        geode::log::warn("[AudioExtract] no audio data extracted from {}", videoPath);
        std::error_code ec;
        std::filesystem::remove(wavPath, ec);
        return {};
    }

    geode::log::info("[AudioExtract] extracted {} bytes of PCM audio to {}",
                     totalDataBytes, wavPath);
    return wavPath;
}

} // namespace paimon::video

#else // !USE_MEDIA_FOUNDATION

namespace paimon::video {

std::string extractAudioToWav(const std::string&) { return {}; }
std::string getCachedWavPath(const std::string&) { return {}; }
void cleanupAudioCache(const std::string&) {}

} // namespace paimon::video

#endif
