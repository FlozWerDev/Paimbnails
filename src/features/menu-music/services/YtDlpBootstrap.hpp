#pragma once

#include <Geode/Geode.hpp>
#include <filesystem>
#include <functional>
#include <string>

namespace paimon::menumusic {

struct BootstrapProgress {
    std::string stage;
    float percent = 0.f;
    std::string message;
};

using BootstrapCompleteCallback = std::function<void(bool success, std::string pathOrError)>;
using BootstrapProgressCallback = std::function<void(BootstrapProgress)>;

class YtDlpBootstrap {
public:
    static YtDlpBootstrap& get();

    std::filesystem::path bundledPath() const;

    bool exists() const;

    // Los callbacks corren en main thread.
    void ensureInstalled(
        BootstrapProgressCallback onProgress,
        BootstrapCompleteCallback onComplete
    );

    void uninstall();

    bool isDownloading() const { return m_downloading; }

private:
    YtDlpBootstrap() = default;

    static std::string releaseUrl();

    std::atomic<bool> m_downloading{false};
};

} // namespace paimon::menumusic
