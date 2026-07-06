#include "AudioExtractor.hpp"

#if defined(USE_MEDIA_FOUNDATION)

#include "AudioWavCommon.hpp"
#include <Geode/loader/Log.hpp>
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <codecapi.h>
#include <objbase.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace paimon::video {

std::string extractAudioToWav(const std::string& videoPath) {
    std::lock_guard<std::mutex> lock(detail::audioExtractorMutex());

    if (videoPath.empty()) return {};

    auto wavPath = detail::makeWavPath(videoPath);
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

    std::vector<uint8_t> pcm;
    pcm.reserve(1u << 20);  // 1 MB initial — grows as needed

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
            pcm.insert(pcm.end(), data, data + bufLen);
            buf->Unlock();
        }
        buf->Release();
        sample->Release();
    }

    reader->Release();
    MFShutdown();

    if (pcm.empty()) {
        geode::log::warn("[AudioExtract] no audio data extracted from {}", videoPath);
        return {};
    }

    if (!detail::writeWavFile(wavPath, pcm.data(), pcm.size(),
                              static_cast<uint16_t>(nChannels), sampleRate,
                              static_cast<uint16_t>(bitsPerSample))) {
        return {};
    }

    geode::log::info("[AudioExtract] extracted {} bytes of PCM audio to {}",
                     pcm.size(), wavPath);
    return wavPath;
}

} // namespace paimon::video

#endif // USE_MEDIA_FOUNDATION
