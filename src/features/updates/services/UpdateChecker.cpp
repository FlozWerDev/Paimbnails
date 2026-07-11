#include "UpdateChecker.hpp"
#include "../../../utils/WebHelper.hpp"
#include "../../../core/Settings.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/file.hpp>
#include <matjson.hpp>
#include <filesystem>
#include <algorithm>
#include <cctype>

using namespace geode::prelude;

namespace paimon::updates {

namespace {

constexpr auto kReleasesApiUrl =
    "https://api.github.com/repos/FlozWerDev/Paimbnails/releases/latest";
constexpr auto kAssetName = "flozwer.paimbnails2.geode";

// Strip 'v'/'V' prefix and surrounding whitespace from a version string.
std::string sanitizeVersion(std::string v) {
    while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());
    while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.pop_back();
    if (!v.empty() && (v.front() == 'v' || v.front() == 'V')) v.erase(v.begin());
    return v;
}

// Hierarchical semver comparison.
// Returns >0 if remote > local, 0 if equal, <0 if remote < local.
// Uses Geode VersionInfo if parseable; falls back to numeric component comparison.
int compareVersions(std::string const& localStr, std::string const& remoteStr) {
    auto local  = sanitizeVersion(localStr);
    auto remote = sanitizeVersion(remoteStr);

    auto localRes  = VersionInfo::parse("v" + local);
    auto remoteRes = VersionInfo::parse("v" + remote);

    if (localRes.isOk() && remoteRes.isOk()) {
        auto const& l = localRes.unwrap();
        auto const& r = remoteRes.unwrap();
        if (r > l) return 1;
        if (r < l) return -1;
        return 0;
    }

    // Fallback: numeric component comparison.
    auto split = [](std::string const& s) {
        std::vector<int> out;
        std::string cur;
        for (char c : s) {
            if (std::isdigit((unsigned char)c)) {
                cur.push_back(c);
            } else if (c == '.' || c == '-' || c == '+') {
                if (!cur.empty()) { out.push_back(std::atoi(cur.c_str())); cur.clear(); }
                if (c != '.') break;
            } else {
                break;
            }
        }
        if (!cur.empty()) out.push_back(std::atoi(cur.c_str()));
        return out;
    };

    auto la = split(local);
    auto ra = split(remote);
    size_t n = std::max(la.size(), ra.size());
    la.resize(n, 0);
    ra.resize(n, 0);
    for (size_t i = 0; i < n; i++) {
        if (ra[i] > la[i]) return 1;
        if (ra[i] < la[i]) return -1;
    }
    return 0;
}

} // namespace

UpdateChecker& UpdateChecker::get() {
    static UpdateChecker s;
    return s;
}

void UpdateChecker::checkAsync() {
    if (m_checkLaunched) return;
    m_checkLaunched = true;
    m_state.store(State::Checking);

    m_localVersion = Mod::get()->getVersion().toVString(false);

    auto req = web::WebRequest()
        .timeout(std::chrono::seconds(15))
        .userAgent("Paimbnails-UpdateChecker/1.0")
        .header("Accept", "application/vnd.github+json");

    WebHelper::dispatchOwned(
        m_checkTask,
        std::move(req),
        "GET",
        kReleasesApiUrl,
        [this](web::WebResponse res) {
            if (paimon::isRuntimeShuttingDown()) return;
            this->onCheckResponse(res);
        }
    );
}

void UpdateChecker::onCheckResponse(web::WebResponse& res) {
    if (paimon::isRuntimeShuttingDown()) return;
    if (!res.ok()) {
        m_lastError = fmt::format("HTTP {}", res.code());
        log::warn("[UpdateChecker] check failed: {}", m_lastError);
        m_state.store(State::Failed);
        return;
    }

    auto body = res.string().unwrapOr("");
    if (body.empty()) {
        m_lastError = "empty body";
        m_state.store(State::Failed);
        return;
    }

    auto parsed = matjson::parse(body);
    if (!parsed.isOk()) {
        m_lastError = "invalid json";
        m_state.store(State::Failed);
        return;
    }
    auto json = parsed.unwrap();

    std::string tag;
    if (json["tag_name"].isString()) {
        tag = json["tag_name"].asString().unwrapOr("");
    }
    if (tag.empty()) {
        m_lastError = "no tag_name";
        m_state.store(State::Failed);
        return;
    }
    m_remoteTag = tag;
    m_remoteVersion = sanitizeVersion(tag);

    // Build download URL: prefer the expected asset name, fall back to the known release URL pattern.
    m_downloadUrl.clear();
    if (json["assets"].isArray()) {
        for (auto const& asset : json["assets"]) {
            std::string name = asset["name"].isString()
                ? asset["name"].asString().unwrapOr("") : "";
            std::string url  = asset["browser_download_url"].isString()
                ? asset["browser_download_url"].asString().unwrapOr("") : "";
            if (name == kAssetName && !url.empty()) {
                m_downloadUrl = url;
                break;
            }
        }
    }
    if (m_downloadUrl.empty()) {
        m_downloadUrl = fmt::format(
            "https://github.com/FlozWerDev/Paimbnails/releases/download/{}/{}",
            tag, kAssetName
        );
    }

    int cmp = compareVersions(m_localVersion, m_remoteVersion);
    log::info("[UpdateChecker] local={} remote={} cmp={}",
        m_localVersion, m_remoteVersion, cmp);

    if (cmp > 0) {
        m_state.store(State::UpdateAvailable);
        // If auto-update is on, start the silent download now.
        if (paimon::settings::general::autoUpdate()) {
            Loader::get()->queueInMainThread([]() {
                UpdateChecker::get().autoDownloadIfNeeded();
            });
        }
    } else {
        m_state.store(State::UpToDate);
    }
}

void UpdateChecker::downloadUpdate(
    std::function<void(uint64_t, uint64_t)> onProgress,
    std::function<void(bool, std::string)> onDone
) {
    if (m_downloadUrl.empty()) {
        if (onDone) onDone(false, "no download url");
        return;
    }

    m_downloadCancelled.store(false);
    m_installedPendingRestart.store(false);

    // Progress callback dispatches to the main thread before touching UI.
    auto progressShared = std::make_shared<std::function<void(uint64_t, uint64_t)>>(std::move(onProgress));
    auto doneShared     = std::make_shared<std::function<void(bool, std::string)>>(std::move(onDone));

    auto req = web::WebRequest()
        .timeout(std::chrono::minutes(5))
        .userAgent("Paimbnails-UpdateChecker/1.0");

    req.onProgress([progressShared, this](web::WebProgress const& p) {
        if (!progressShared || !*progressShared) return;
        uint64_t cur = static_cast<uint64_t>(p.downloaded());
        uint64_t tot = static_cast<uint64_t>(p.downloadTotal());
        Loader::get()->queueInMainThread([progressShared, cur, tot]() {
            if (progressShared && *progressShared) (*progressShared)(cur, tot);
        });
    });

    WebHelper::dispatchOwned(
        m_downloadTask,
        std::move(req),
        "GET",
        m_downloadUrl,
        [this, doneShared](web::WebResponse res) {
            auto fail = [doneShared](std::string err) {
                if (doneShared && *doneShared) (*doneShared)(false, std::move(err));
            };

            if (m_downloadCancelled.load()) {
                fail("cancelled");
                return;
            }
            if (!res.ok()) {
                fail(fmt::format("HTTP {}", res.code()));
                return;
            }

            auto bytes = std::move(res).data();
            if (bytes.empty()) {
                fail("empty payload");
                return;
            }

            if (m_downloadCancelled.load()) {
                fail("cancelled");
                return;
            }

            // Same technique Geode's own updater uses: overwrite the installed
            // .geode in place while the game is running. The file isn't locked
            // (the binary is loaded from the unzipped runtime dir), so the new
            // version simply loads on the next restart.
            auto packagePath = Mod::get()->getPackagePath();
            if (packagePath.empty()) {
                fail("no package path");
                return;
            }

            auto writeRes = geode::utils::file::writeBinary(packagePath, bytes);
            if (!writeRes) {
                fail(fmt::format("cannot write update: {}", writeRes.unwrapErr()));
                return;
            }

            m_installedPendingRestart.store(true);
            log::info("[UpdateChecker] Update written in place at {}",
                geode::utils::string::pathToString(packagePath));

            if (doneShared && *doneShared) {
                (*doneShared)(true, geode::utils::string::pathToString(packagePath));
            }
        }
    );
}

bool UpdateChecker::hasPendingInstall() const {
    return m_installedPendingRestart.load();
}

bool UpdateChecker::restartToApplyPendingUpdate() const {
    if (!this->hasPendingInstall()) {
        return false;
    }
    // The new .geode is already on disk; restarting loads it on all platforms.
    geode::utils::game::restart(true);
    return true;
}

bool UpdateChecker::applyPendingUpdateInPlace() const {
    // The update is written in place as soon as it's downloaded, so there's
    // nothing to do on exit: the new version loads on the next launch.
    return this->hasPendingInstall();
}

void UpdateChecker::autoDownloadIfNeeded() {
    if (m_state.load() != State::UpdateAvailable) return;
    if (m_downloadUrl.empty()) return;
    if (this->hasPendingInstall()) return;

    bool expected = false;
    if (!m_autoDownloadStarted.compare_exchange_strong(expected, true)) {
        return;
    }

    log::info("[UpdateChecker] Auto-update triggered: downloading {} silently", m_remoteVersion);

    this->downloadUpdate(
        // Log at 25% intervals to avoid spamming the log.
        [](uint64_t received, uint64_t total) {
            if (total == 0) return;
            static std::atomic<int> lastBucket{-1};
            int bucket = static_cast<int>((received * 4) / total);
            int expectedBucket = lastBucket.load();
            while (bucket > expectedBucket) {
                if (lastBucket.compare_exchange_strong(expectedBucket, bucket)) {
                    log::info("[UpdateChecker] Auto-update progress: {}%",
                              (bucket * 25));
                    break;
                }
            }
        },
        [](bool ok, std::string detail) {
            if (ok) {
                log::info("[UpdateChecker] Auto-update installed in place. Loads on next restart.");
            } else {
                log::warn("[UpdateChecker] Auto-update failed: {}", detail);
            }
        }
    );
}

void UpdateChecker::cancelDownload() {
    m_downloadCancelled.store(true);
}

} // namespace paimon::updates
