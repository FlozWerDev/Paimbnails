#pragma once

// downloads and caches a platform ffmpeg build for yt-dlp conversion because
// gd's fmod path does not reliably decode aac, opus, or webm.

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

    // cached binary path.
    std::filesystem::path bundledPath() const;

    // whether the cached binary exists.
    bool exists() const;

    // download if needed; callbacks run on the main thread.
    void ensureInstalled(
        FfmpegBootstrapProgressCallback onProgress,
        FfmpegBootstrapCompleteCallback onComplete
    );

    // remove the cached binary.
    void uninstall();

    bool isDownloading() const { return m_downloading; }

private:
    FfmpegBootstrap() = default;
    static std::string releaseUrl();

    // archive member to extract.
    static std::string archiveEntry();

    std::atomic<bool> m_downloading{false};
};

}
