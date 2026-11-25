#include "../utils/FilesystemPath.hpp"
#include "AudioWavCommon.hpp"
#include "AudioExtractor.hpp"

#include <Geode/loader/Log.hpp>
#include <Geode/utils/string.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <mutex>
#include <vector>

namespace paimon::video {

namespace detail {

static std::filesystem::path nativeWavPath(std::string const& utf8Path) {
    return std::filesystem::path(std::u8string(utf8Path.begin(), utf8Path.end()));
}

std::recursive_mutex& audioExtractorMutex() {
    static std::recursive_mutex mtx;
    return mtx;
}

std::filesystem::path getAudioCacheDir() {
    auto dir = std::filesystem::temp_directory_path() / "paimbnails_audio_cache";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::string makeWavPath(const std::string& videoPath) {
    std::size_t h = std::hash<std::string>{}(videoPath);
    auto wavFile = getAudioCacheDir() / ("vid_" + std::to_string(h) + ".wav");
    return paimon::paths::toUtf8(wavFile);
}

bool writeWavFile(const std::string& wavPath,
                  const uint8_t* pcm, size_t pcmBytes,
                  uint16_t numChannels, uint32_t sampleRate,
                  uint16_t bitsPerSample) {
    if (!pcm || pcmBytes == 0 || numChannels == 0 || sampleRate == 0 ||
        bitsPerSample == 0 || bitsPerSample % 8 != 0) {
        return false;
    }

    auto const blockAlign = static_cast<uint32_t>(numChannels) * (bitsPerSample / 8);
    if (blockAlign > std::numeric_limits<uint16_t>::max() || pcmBytes % blockAlign != 0 ||
        sampleRate > std::numeric_limits<uint32_t>::max() / blockAlign ||
        pcmBytes > std::numeric_limits<uint32_t>::max() - (sizeof(WavHeader) - 8) ||
        pcmBytes > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max())) {
        return false;
    }

    auto const path = nativeWavPath(wavPath);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        geode::log::warn("[AudioExtract] failed to open WAV for writing: {}", wavPath);
        return false;
    }

    WavHeader hdr;
    hdr.numChannels   = numChannels;
    hdr.sampleRate    = sampleRate;
    hdr.bitsPerSample = bitsPerSample;
    hdr.blockAlign    = static_cast<uint16_t>(blockAlign);
    hdr.byteRate      = sampleRate * hdr.blockAlign;
    hdr.dataSize      = static_cast<uint32_t>(pcmBytes);
    hdr.fileSize      = static_cast<uint32_t>(sizeof(WavHeader) - 8 + pcmBytes);

    output.write(reinterpret_cast<char const*>(&hdr), sizeof(hdr));
    output.write(reinterpret_cast<char const*>(pcm), static_cast<std::streamsize>(pcmBytes));
    output.close();

    if (!output) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        geode::log::warn("[AudioExtract] WAV write failed: {}", wavPath);
        return false;
    }
    return true;
}

namespace {

constexpr std::size_t kMaxAudioCacheFiles = 20;
constexpr uint64_t kMaxAudioCacheBytes = 512ULL * 1024ULL * 1024ULL;

bool isManagedWav(const std::filesystem::path& path) {
    const auto name = path.filename().string();
    return name.starts_with("vid_") && name.ends_with(".wav");
}

} // namespace

// runs without audioExtractorMutex held; callers in the write path already
// hold it, and the lazy startup pass must stay lock-free (call_once).
void pruneAudioCache() {
    std::error_code ec;
    auto dir = getAudioCacheDir();
    if (!std::filesystem::exists(dir, ec) || ec) return;

    struct Entry {
        std::filesystem::path path;
        uint64_t size = 0;
        std::filesystem::file_time_type mtime = std::filesystem::file_time_type::min();
    };
    std::vector<Entry> entries;
    for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec) || ec) continue;
        const auto& path = entry.path();
        if (!isManagedWav(path)) continue;
        Entry e;
        e.path = path;
        e.size = entry.file_size(ec);
        if (ec) {
            e.size = 0;
            ec.clear();
        }
        e.mtime = std::filesystem::last_write_time(path, ec);
        if (ec) {
            e.mtime = std::filesystem::file_time_type::min();
            ec.clear();
        }
        entries.push_back(std::move(e));
    }
    if (ec || entries.size() <= kMaxAudioCacheFiles) {
        // still enforce the byte cap below when over the file count; a small
        // cache needs no work. Fall through only when pruning may be needed.
        bool overBytes = false;
        uint64_t total = 0;
        for (auto const& e : entries) {
            total += e.size;
            if (total > kMaxAudioCacheBytes) {
                overBytes = true;
                break;
            }
        }
        if (!overBytes) return;
    }

    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        return a.mtime > b.mtime;
    });

    std::size_t kept = 0;
    uint64_t keptBytes = 0;
    int removed = 0;
    for (auto const& e : entries) {
        // always keep the single newest entry so a fresh write is never
        // deleted by the same pass that just created it.
        const bool keep = kept == 0 ||
            (kept < kMaxAudioCacheFiles && keptBytes + e.size <= kMaxAudioCacheBytes);
        if (keep) {
            ++kept;
            keptBytes += e.size;
            continue;
        }
        std::error_code removeEc;
        std::filesystem::remove(e.path, removeEc);
        if (!removeEc) {
            ++removed;
        } else {
            geode::log::warn("[AudioExtract] WAV cache prune failed for {}: {}",
                             paimon::paths::toUtf8(e.path), removeEc.message());
        }
    }
    if (removed > 0) {
        geode::log::info("[AudioExtract] WAV cache prune: removed {} file(s), kept {} ({} bytes)",
                         removed, kept, keptBytes);
    }
}

} // namespace detail

namespace {

std::once_flag s_audioCachePruneOnce;

void ensureAudioCachePrunedOnce() {
    std::call_once(s_audioCachePruneOnce, [] {
        detail::pruneAudioCache();
    });
}

} // namespace

std::string getCachedWavPath(const std::string& videoPath) {
    ensureAudioCachePrunedOnce();
    auto wavPath = detail::makeWavPath(videoPath);
    std::error_code ec;
    if (std::filesystem::exists(detail::nativeWavPath(wavPath), ec) && !ec) return wavPath;
    return {};
}

void cleanupAudioCache(const std::string& videoPath) {
    auto wavPath = detail::makeWavPath(videoPath);
    if (!wavPath.empty()) {
        std::error_code ec;
        std::filesystem::remove(detail::nativeWavPath(wavPath), ec);
    }
}

// fallback for platforms with no native audio backend (e.g. linux).
#if !defined(USE_MEDIA_FOUNDATION) && !defined(USE_MEDIA_NDK) && !defined(USE_AV_FOUNDATION)
AudioPcm extractAudioToPcm(const std::string&) { return {}; }
#endif

std::string extractAudioToWav(const std::string& videoPath) {
    std::lock_guard lock(detail::audioExtractorMutex());
    ensureAudioCachePrunedOnce();
    if (videoPath.empty()) return {};

    auto wavPath = detail::makeWavPath(videoPath);
    std::error_code ec;
    if (std::filesystem::exists(detail::nativeWavPath(wavPath), ec) && !ec) return wavPath;

    auto pcm = extractAudioToPcm(videoPath);
    if (!pcm.valid()) return {};

    if (!detail::writeWavFile(wavPath, pcm.data.data(), pcm.data.size(),
                              static_cast<uint16_t>(pcm.channels),
                              static_cast<uint32_t>(pcm.sampleRate),
                              static_cast<uint16_t>(pcm.bitsPerSample))) {
        return {};
    }

    geode::log::info("[AudioExtract] cached {} bytes of PCM to {}", pcm.data.size(), wavPath);
    // mark the fresh file newest so the quota pass below never evicts it.
    {
        std::error_code touchEc;
        std::filesystem::last_write_time(
            detail::nativeWavPath(wavPath),
            std::filesystem::file_time_type::clock::now(), touchEc);
    }
    detail::pruneAudioCache();
    return wavPath;
}

} // namespace paimon::video
