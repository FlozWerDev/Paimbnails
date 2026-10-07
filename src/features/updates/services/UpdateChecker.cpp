#include "UpdateChecker.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/loader/ModMetadata.hpp>

#include "../../../utils/WebHelper.hpp"
#include "../../../core/Settings.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

#include <matjson.hpp>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <limits>

using namespace geode::prelude;

namespace paimon::updates {

namespace {

constexpr auto kReleasesApiUrl =
    "https://api.github.com/repos/FlozWerDev/Paimbnails/releases/latest";
constexpr auto kReleaseListUrl =
    "https://api.github.com/repos/FlozWerDev/Paimbnails/releases?per_page=60";
constexpr auto kAssetName = "flozwer.paimbnails2.geode";

std::string sanitizeVersion(std::string v) {
    while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());
    while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.pop_back();
    if (!v.empty() && (v.front() == 'v' || v.front() == 'V')) v.erase(v.begin());
    return v;
}

std::string jsonString(matjson::Value const& obj, char const* key) {
    if (!obj[key].isString()) return "";
    return obj[key].asString().unwrapOr("");
}

// older releases shipped under other asset names, so fall back to any
// .geode instead of dropping them from the history.
std::string pickGeodeAsset(matjson::Value const& release, uint64_t& outSize) {
    if (!release["assets"].isArray()) return "";

    std::string fallbackUrl;
    uint64_t fallbackSize = 0;
    for (auto const& asset : release["assets"]) {
        auto name = jsonString(asset, "name");
        auto url  = jsonString(asset, "browser_download_url");
        if (url.empty()) continue;

        uint64_t size = asset["size"].isNumber()
            ? static_cast<uint64_t>(asset["size"].asUInt().unwrapOr(0)) : 0;

        if (name == kAssetName) {
            outSize = size;
            return url;
        }
        if (fallbackUrl.empty() && name.size() > 6 &&
            name.compare(name.size() - 6, 6, ".geode") == 0) {
            fallbackUrl = url;
            fallbackSize = size;
        }
    }
    outSize = fallbackSize;
    return fallbackUrl;
}

} // namespace

UpdateChecker::UpdateChecker()
    : m_localVersion(Mod::get()->getVersion().toVString(false)) {}

UpdateChecker& UpdateChecker::get() {
    static UpdateChecker s;
    return s;
}

int UpdateChecker::compareVersions(std::string const& baseStr, std::string const& otherStr) {
    auto base  = sanitizeVersion(baseStr);
    auto other = sanitizeVersion(otherStr);

    auto baseRes  = VersionInfo::parse("v" + base);
    auto otherRes = VersionInfo::parse("v" + other);

    if (baseRes.isOk() && otherRes.isOk()) {
        auto const& l = baseRes.unwrap();
        auto const& r = otherRes.unwrap();
        if (r > l) return 1;
        if (r < l) return -1;
        return 0;
    }

    auto split = [](std::string const& s) {
        std::vector<int> out;
        std::string cur;
        for (char c : s) {
            if (std::isdigit((unsigned char)c)) {
                cur.push_back(c);
            } else if (c == '.' || c == '-' || c == '+') {
                if (!cur.empty()) {
                    out.push_back(utils::numFromString<int>(cur).unwrapOr(std::numeric_limits<int>::max()));
                    cur.clear();
                }
                if (c != '.') break;
            } else {
                break;
            }
        }
        if (!cur.empty()) {
            out.push_back(utils::numFromString<int>(cur).unwrapOr(std::numeric_limits<int>::max()));
        }
        return out;
    };

    auto la = split(base);
    auto ra = split(other);
    size_t n = std::max(la.size(), ra.size());
    la.resize(n, 0);
    ra.resize(n, 0);
    for (size_t i = 0; i < n; i++) {
        if (ra[i] > la[i]) return 1;
        if (ra[i] < la[i]) return -1;
    }
    return 0;
}

void UpdateChecker::checkAsync(bool force, std::function<void(bool, std::string)> onDone) {
    if (onDone) m_checkWaiters.push_back(std::move(onDone));
    if (m_state.load() == State::Checking) return;
    if (m_checkLaunched && !force) {
        this->finishCheck(m_state.load() != State::Failed, m_lastError);
        return;
    }
    m_checkLaunched = true;
    m_state.store(State::Checking);

    m_localVersion = Mod::get()->getVersion().toVString(false);

    auto req = web::WebRequest()
        .timeout(std::chrono::seconds(15))
        .userAgent("Paimbnails-UpdateChecker/1.0")
        .header("Accept", "application/vnd.github+json");
    if (force) {
        req.header("Cache-Control", "no-cache")
            .param("_paimbnails", std::chrono::steady_clock::now().time_since_epoch().count());
    }

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
    auto fail = [this](std::string error) {
        m_lastError = std::move(error);
        m_state.store(State::Failed);
        log::warn("[UpdateChecker] check failed: {}", m_lastError);
        this->finishCheck(false, m_lastError);
    };
    if (!res.ok()) {
        fail(fmt::format("HTTP {}", res.code()));
        return;
    }

    auto body = res.string().unwrapOr("");
    if (body.empty()) {
        fail("empty body");
        return;
    }

    auto parsed = matjson::parse(body);
    if (!parsed.isOk()) {
        fail("invalid json");
        return;
    }
    auto json = parsed.unwrap();

    std::string tag = jsonString(json, "tag_name");
    if (tag.empty()) {
        fail("no tag_name");
        return;
    }
    m_remoteTag = tag;
    m_remoteVersion = sanitizeVersion(tag);

    uint64_t assetSize = 0;
    m_downloadUrl = pickGeodeAsset(json, assetSize);
    if (m_downloadUrl.empty()) {
        m_downloadUrl = fmt::format(
            "https://github.com/FlozWerDev/Paimbnails/releases/download/{}/{}",
            tag, kAssetName
        );
    }

    int cmp = compareVersions(m_localVersion, m_remoteVersion);
    m_state.store(cmp > 0 ? State::UpdateAvailable : State::UpToDate);
    m_lastError.clear();
    bool manualCheck = !m_checkWaiters.empty();
    this->finishCheck(true, "");
    if (cmp > 0 && !manualCheck && paimon::settings::general::autoUpdate()) {
        Loader::get()->queueInMainThread([]() {
            UpdateChecker::get().autoDownloadIfNeeded();
        });
    }
}

void UpdateChecker::finishCheck(bool ok, std::string error) {
    auto waiters = std::move(m_checkWaiters);
    m_checkWaiters.clear();
    for (auto const& callback : waiters) {
        if (callback) callback(ok, error);
    }
}

void UpdateChecker::fetchReleasesAsync(std::function<void(bool, std::string)> onDone) {
    if (onDone) m_releaseWaiters.push_back(std::move(onDone));
    if (m_releasesLoading) return;

    m_releasesLoading = true;

    auto req = web::WebRequest()
        .timeout(std::chrono::seconds(20))
        .userAgent("Paimbnails-UpdateChecker/1.0")
        .header("Accept", "application/vnd.github+json");

    WebHelper::dispatchOwned(
        m_releasesTask,
        std::move(req),
        "GET",
        kReleaseListUrl,
        [this](web::WebResponse res) {
            if (paimon::isRuntimeShuttingDown()) return;
            this->onReleasesResponse(res);
        }
    );
}

void UpdateChecker::onReleasesResponse(web::WebResponse& res) {
    if (!res.ok()) {
        this->finishReleasesFetch(false, fmt::format("HTTP {}", res.code()));
        return;
    }

    auto parsed = matjson::parse(res.string().unwrapOr(""));
    if (!parsed.isOk() || !parsed.unwrap().isArray()) {
        this->finishReleasesFetch(false, "invalid json");
        return;
    }

    std::vector<ReleaseInfo> list;
    for (auto const& entry : parsed.unwrap()) {
        if (entry["draft"].isBool() && entry["draft"].asBool().unwrapOr(false)) continue;

        ReleaseInfo info;
        info.tag = jsonString(entry, "tag_name");
        if (info.tag.empty()) continue;

        info.version = sanitizeVersion(info.tag);
        info.name = jsonString(entry, "name");
        if (info.name.empty()) info.name = info.tag;
        info.notes = jsonString(entry, "body");
        info.prerelease = entry["prerelease"].isBool()
            && entry["prerelease"].asBool().unwrapOr(false);

        // published_at is iso-8601; the picker shows the day only.
        auto published = jsonString(entry, "published_at");
        info.date = published.size() >= 10 ? published.substr(0, 10) : published;

        info.downloadUrl = pickGeodeAsset(entry, info.size);
        list.push_back(std::move(info));
    }

    // api order is by creation date, which drifts once an older branch ships
    // a late patch.
    std::stable_sort(list.begin(), list.end(), [](ReleaseInfo const& a, ReleaseInfo const& b) {
        return compareVersions(a.version, b.version) < 0;
    });

    m_releases = std::move(list);
    m_releasesLoaded = true;
    this->finishReleasesFetch(true, "");
}

void UpdateChecker::finishReleasesFetch(bool ok, std::string error) {
    m_releasesLoading = false;
    if (!ok) {
        m_lastError = error;
        log::warn("[UpdateChecker] release list failed: {}", error);
    }

    auto waiters = std::move(m_releaseWaiters);
    m_releaseWaiters.clear();
    for (auto const& cb : waiters) {
        if (cb) cb(ok, error);
    }
}

void UpdateChecker::downloadUpdate(
    std::function<void(uint64_t, uint64_t)> onProgress,
    std::function<void(bool, std::string)> onDone
) {
    this->downloadRelease(m_downloadUrl, m_remoteVersion, std::move(onProgress), std::move(onDone));
}

void UpdateChecker::downloadRelease(
    std::string url,
    std::string version,
    std::function<void(uint64_t, uint64_t)> onProgress,
    std::function<void(bool, std::string)> onDone
) {
    if (this->isDownloading()) {
        if (onDone) onDone(false, "a download is already in progress");
        return;
    }
    if (url.empty()) {
        if (onDone) onDone(false, "no download url");
        return;
    }

    m_downloadCancelled.store(false);
    m_installedPendingRestart.store(false);
    m_pendingVersion.clear();

    // progress hops to the main thread before touching ui.
    auto progressShared = std::make_shared<std::function<void(uint64_t, uint64_t)>>(std::move(onProgress));
    auto doneShared     = std::make_shared<std::function<void(bool, std::string)>>(std::move(onDone));

    auto req = web::WebRequest()
        .timeout(std::chrono::minutes(5))
        .userAgent("Paimbnails-UpdateChecker/1.0")
        // Release assets can be replaced without changing their tag or filename.
        .header("Cache-Control", "no-cache")
        .param("_paimbnails", std::chrono::steady_clock::now().time_since_epoch().count());

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
        url,
        [this, doneShared, version](web::WebResponse res) {
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
            if (bytes.empty() || bytes.size() > 256ULL * 1024 * 1024) {
                fail("invalid payload size");
                return;
            }

            if (m_downloadCancelled.load()) {
                fail("cancelled");
                return;
            }

            auto packagePath = Mod::get()->getPackagePath();
            if (packagePath.empty()) {
                fail("no package path");
                return;
            }

            // stage beside the package so a failed write leaves the installed version intact.
            auto stagedPath = packagePath;
            stagedPath += ".update.tmp";
            auto failPackage = [&](std::string error) {
                std::error_code cleanupError;
                std::filesystem::remove(stagedPath, cleanupError);
                fail(std::move(error));
            };
            auto writeRes = geode::utils::file::writeBinary(stagedPath, bytes);
            if (!writeRes) {
                failPackage(fmt::format("cannot write update: {}", writeRes.unwrapErr()));
                return;
            }

            auto metadata = ModMetadata::createFromGeodeFile(stagedPath);
            auto expected = VersionInfo::parse("v" + sanitizeVersion(version));
            if (metadata.hasErrors() || metadata.getID() != Mod::get()->getID() ||
                !expected || metadata.getVersion() != expected.unwrap()) {
                failPackage("downloaded package does not match the requested mod and version");
                return;
            }
            auto compatible = metadata.checkTargetVersions();
            if (!compatible) {
                failPackage(compatible.unwrapErr());
                return;
            }
            bool hasBinary = false;
            {
                auto archive = geode::utils::file::Unzip::create(stagedPath);
                hasBinary = archive && archive.unwrap().hasEntry(metadata.getBinaryName().data());
            }
            if (!hasBinary || m_downloadCancelled.load()) {
                failPackage(hasBinary ? "cancelled" : "package has no binary for this platform");
                return;
            }
            std::error_code renameError;
            std::filesystem::rename(stagedPath, packagePath, renameError);
            if (renameError) {
                failPackage("cannot replace package: " + renameError.message());
                return;
            }

            m_pendingVersion = version;
            m_installedPendingRestart.store(true);
            log::info("[UpdateChecker] Version {} written in place at {}", version,
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
    if (!this->hasPendingInstall()) return false;
    geode::utils::game::restart(true);
    return true;
}

bool UpdateChecker::applyPendingUpdateInPlace() const {
    return this->hasPendingInstall();
}

void UpdateChecker::autoDownloadIfNeeded() {
    if (m_state.load() != State::UpdateAvailable) return;
    if (m_downloadUrl.empty()) return;
    if (this->hasPendingInstall()) return;
    if (this->isDownloading()) return;

    bool expected = false;
    if (!m_autoDownloadStarted.compare_exchange_strong(expected, true)) return;

    log::info("[UpdateChecker] Auto-update triggered: downloading {} silently", m_remoteVersion);

    this->downloadUpdate(
        // log at 25% steps to avoid spam.
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
    m_downloadTask.cancel();
}

void UpdateChecker::shutdown() {
    m_downloadCancelled.store(true, std::memory_order_release);
    m_checkTask.cancel();
    m_releasesTask.cancel();
    m_downloadTask.cancel();
    m_checkWaiters.clear();
    m_releaseWaiters.clear();
    m_releasesLoading = false;
}

} // namespace paimon::updates
