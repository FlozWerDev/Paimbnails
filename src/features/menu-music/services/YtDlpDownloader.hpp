#pragma once

#include <Geode/Geode.hpp>
#include <functional>
#include <string>
#include <filesystem>

namespace paimon::menumusic {

struct YtDlpResult {
    bool success = false;
    std::string trackId;
    std::string audioPath;
    std::string coverPath;
    std::string displayName;
    std::string artist;
    std::string error;
    int exitCode = 0;
};

struct YtDlpProgress {
    std::string stage;
    float percent = 0.f;
    std::string message;
};

using YtDlpCompleteCallback = std::function<void(YtDlpResult)>;
using YtDlpProgressCallback = std::function<void(YtDlpProgress)>;

class YtDlpDownloader {
public:
    static YtDlpDownloader& get();

    // El resultado se cachea 60s para no llamar a exec en cada apertura del popup.
    std::string locateBinary();

    bool isAvailable();

    // Los callbacks corren siempre en main thread.
    void download(
        const std::string& url,
        const std::string& trackId,
        YtDlpProgressCallback onProgress,
        YtDlpCompleteCallback onComplete
    );

    bool isBusy() const { return m_activeJobs > 0; }

private:
    YtDlpDownloader() = default;
    ~YtDlpDownloader() = default;
    YtDlpDownloader(const YtDlpDownloader&) = delete;
    YtDlpDownloader& operator=(const YtDlpDownloader&) = delete;

    std::atomic<int> m_activeJobs{0};

    std::string m_cachedBinary;
    std::chrono::steady_clock::time_point m_cachedBinaryAt;
};

} // namespace paimon::menumusic
