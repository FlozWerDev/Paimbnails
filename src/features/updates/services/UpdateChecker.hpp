#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/async.hpp>
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <filesystem>

namespace paimon::updates {

// One published release of the mod, as listed by the GitHub Releases API.
struct ReleaseInfo {
    std::string tag;
    std::string version;
    std::string name;
    std::string notes;
    std::string date;
    std::string downloadUrl;
    uint64_t size = 0;
    bool prerelease = false;
};

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

    // Start the GitHub check once; force re-runs it from the update center.
    void checkAsync(bool force = false);

    // True when a successful check found a newer version.
    bool hasUpdate() const { return m_state.load() == State::UpdateAvailable; }
    State state() const { return m_state.load(); }

    std::string const& localVersion() const { return m_localVersion; }
    std::string const& remoteVersion() const { return m_remoteVersion; }
    std::string const& remoteTag() const { return m_remoteTag; }
    std::string const& downloadUrl() const { return m_downloadUrl; }
    std::string const& lastError() const { return m_lastError; }

    // Full release history, newest first. Empty until fetchReleasesAsync runs.
    void fetchReleasesAsync(std::function<void(bool, std::string)> onDone);
    std::vector<ReleaseInfo> const& releases() const { return m_releases; }
    bool releasesLoaded() const { return m_releasesLoaded; }
    bool releasesLoading() const { return m_releasesLoading; }

    // Download with main-thread progress callbacks; onDone fires once.
    void downloadUpdate(
        std::function<void(uint64_t, uint64_t)> onProgress,
        std::function<void(bool, std::string)> onDone
    );

    // Same, for any release of the history (older ones included).
    void downloadRelease(
        std::string url,
        std::string version,
        std::function<void(uint64_t, uint64_t)> onProgress,
        std::function<void(bool, std::string)> onDone
    );

    // True when an update is installed and only restart remains.
    bool hasPendingInstall() const;

    // Version written to disk by the last successful download.
    std::string const& pendingVersion() const { return m_pendingVersion; }

    // Restart to load the installed update.
    bool restartToApplyPendingUpdate() const;

    // The update is written in place when the download finishes.
    bool applyPendingUpdateInPlace() const;

    // Start a silent download when auto-update is enabled and no install is pending.
    void autoDownloadIfNeeded();

    // Cancel the active download, if any.
    void cancelDownload();

    // Cancel every async handle and release callbacks before Geode teardown.
    void shutdown();

    // >0 when other is newer than base, 0 when equal, <0 when older.
    static int compareVersions(std::string const& base, std::string const& other);

private:
    UpdateChecker();

    void onCheckResponse(geode::utils::web::WebResponse& res);
    void onReleasesResponse(geode::utils::web::WebResponse& res);
    void finishReleasesFetch(bool ok, std::string error);

    std::atomic<State> m_state{State::Idle};
    bool m_checkLaunched = false;

    std::string m_localVersion;
    std::string m_remoteVersion;
    std::string m_remoteTag;
    std::string m_downloadUrl;
    std::string m_lastError;
    std::string m_pendingVersion;
    std::atomic<bool> m_installedPendingRestart{false};

    std::vector<ReleaseInfo> m_releases;
    bool m_releasesLoaded = false;
    bool m_releasesLoading = false;
    std::vector<std::function<void(bool, std::string)>> m_releaseWaiters;

    geode::async::TaskHolder<geode::utils::web::WebResponse> m_checkTask;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_releasesTask;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_downloadTask;
    std::atomic<bool> m_downloadCancelled{false};
    std::atomic<bool> m_autoDownloadStarted{false};
};

}
