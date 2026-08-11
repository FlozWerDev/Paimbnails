#pragma once

// Downloads and caches a platform ffmpeg build for yt-dlp conversion because
// GD's FMOD path does not reliably decode AAC, Opus, or WebM.

#include <Geode/Geode.hpp>
#include <filesystem>
#include <functional>
#include <string>

namespace paimon::menumusic {

struct FfmpegBootstrapProgress {
    std::string stage;   // "resolving", "downloading", "extracting", "installing", "done", "error"
    float percent = 0.f; // 0..1
    std::string message;
};

using FfmpegBootstrapCompleteCallback =
    std::function<void(bool success, std::string pathOrError)>;
using FfmpegBootstrapProgressCallback =
    std::function<void(FfmpegBootstrapProgress)>;

class FfmpegBootstrap {
public:
    static FfmpegBootstrap& get();

    // Cached binary path.
    std::filesystem::path bundledPath() const;

    // Whether the cached binary exists.
    bool exists() const;

    // Download if needed; callbacks run on the main thread.
    void ensureInstalled(
        FfmpegBootstrapProgressCallback onProgress,
        FfmpegBootstrapCompleteCallback onComplete
    );

    // Remove the cached binary.
    void uninstall();

    bool isDownloading() const { return m_downloading; }

private:
    FfmpegBootstrap() = default;
    static std::string releaseUrl();

    // Whether releaseUrl points to an archive.
    static bool isArchive();

    // Archive member to extract.
    static std::string archiveEntry();

    std::atomic<bool> m_downloading{false};
};

}
