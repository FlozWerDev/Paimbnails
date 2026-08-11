#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/async.hpp>
#include <string>
#include <functional>
#include <atomic>
#include <filesystem>

namespace paimon::updates {

// Checks GitHub Releases against mod.json and downloads a selected .geode with
// main-thread progress callbacks.

class UpdateChecker {
public:
    enum class State {
        Idle,
        Checking,
        UpToDate,
        UpdateAvailable,
        Failed,
    };

    static UpdateChecker& get();

    // Start the GitHub check once.
    void checkAsync();

    // True when a successful check found a newer version.
    bool hasUpdate() const { return m_state.load() == State::UpdateAvailable; }
    State state() const { return m_state.load(); }

    std::string const& localVersion() const { return m_localVersion; }
    std::string const& remoteVersion() const { return m_remoteVersion; }
    std::string const& remoteTag() const { return m_remoteTag; }
    std::string const& downloadUrl() const { return m_downloadUrl; }
    std::string const& lastError() const { return m_lastError; }

    // Download with main-thread progress callbacks; onDone fires once.
    void downloadUpdate(
        std::function<void(uint64_t, uint64_t)> onProgress,
        std::function<void(bool, std::string)> onDone
    );

    // True when an update is installed and only restart remains.
    bool hasPendingInstall() const;

    // Restart to load the installed update.
    bool restartToApplyPendingUpdate() const;

    // The update is written in place when the download finishes.
    bool applyPendingUpdateInPlace() const;

    // Start a silent download when auto-update is enabled and no install is pending.
    void autoDownloadIfNeeded();

    // Cancel the active download, if any.
    void cancelDownload();

private:
    UpdateChecker() = default;

    void onCheckResponse(geode::utils::web::WebResponse& res);

    std::atomic<State> m_state{State::Idle};
    bool m_checkLaunched = false;

    std::string m_localVersion;
    std::string m_remoteVersion;
    std::string m_remoteTag;
    std::string m_downloadUrl;
    std::string m_lastError;
    std::atomic<bool> m_installedPendingRestart{false};

    geode::async::TaskHolder<geode::utils::web::WebResponse> m_checkTask;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_downloadTask;
    std::atomic<bool> m_downloadCancelled{false};
    std::atomic<bool> m_autoDownloadStarted{false};
};

}
