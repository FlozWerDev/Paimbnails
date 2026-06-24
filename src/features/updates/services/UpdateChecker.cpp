#include "UpdateChecker.hpp"
#include "../../../utils/WebHelper.hpp"
#include "../../../core/Settings.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <matjson.hpp>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <system_error>

#ifdef GEODE_IS_WINDOWS
#include <windows.h>
#endif

using namespace geode::prelude;

namespace paimon::updates {

namespace {

constexpr auto kReleasesApiUrl =
    "https://api.github.com/repos/FlozWerDev/Paimbnails/releases/latest";
constexpr auto kAssetName = "flozwer.paimbnails2.geode";

std::filesystem::path getStagedUpdatePath() {
    auto dir = Mod::get()->getSaveDir() / "updates";
    auto filename = Mod::get()->getPackagePath().filename();
    if (filename.empty()) {
        filename = kAssetName;
    }
    return dir / filename;
}

std::string escapePowerShellLiteral(std::string value) {
    size_t pos = 0;
    while ((pos = value.find('\'', pos)) != std::string::npos) {
        value.replace(pos, 1, "''");
        pos += 2;
    }
    return value;
}

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
    m_pendingUpdatePath.clear();

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

            // Never overwrite the live .geode in-place; stage it and swap on exit.
            std::filesystem::path stagedPath = getStagedUpdatePath();
            std::filesystem::path tempPath = stagedPath;
            tempPath += ".download";
            std::error_code ec;

            std::filesystem::create_directories(stagedPath.parent_path(), ec);
            if (ec) {
                fail(fmt::format("cannot create update dir: {}", ec.message()));
                return;
            }

            std::filesystem::remove(tempPath, ec);

            std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
            if (!out) {
                fail("cannot open staged file");
                return;
            }

            out.write(reinterpret_cast<char const*>(bytes.data()), bytes.size());
            out.close();

            if (!out) {
                std::filesystem::remove(tempPath, ec);
                fail("cannot write staged file");
                return;
            }

            if (m_downloadCancelled.load()) {
                std::filesystem::remove(tempPath, ec);
                fail("cancelled");
                return;
            }

            std::filesystem::remove(stagedPath, ec);
            ec.clear();
            std::filesystem::rename(tempPath, stagedPath, ec);
            if (ec) {
                std::filesystem::remove(tempPath, ec);
                fail(fmt::format("cannot finalize staged update: {}", ec.message()));
                return;
            }

            m_pendingUpdatePath = stagedPath;
            log::info("[UpdateChecker] Update staged at {}", m_pendingUpdatePath);

            if (doneShared && *doneShared) {
                (*doneShared)(true, geode::utils::string::pathToString(stagedPath));
            }
        }
    );
}

bool UpdateChecker::hasPendingInstall() const {
    if (m_pendingUpdatePath.empty()) return false;
    std::error_code ec;
    return std::filesystem::exists(m_pendingUpdatePath, ec) && !ec;
}

namespace {

#ifdef GEODE_IS_WINDOWS
// Spawns a PowerShell helper that waits for the game PID, atomically swaps the
// staged .geode, and optionally relaunches.
// relaunch=true: restart GD (user-triggered); relaunch=false: silent (auto-update on exit).
bool spawnUpdaterHelper(std::filesystem::path const& pendingUpdatePath, bool relaunch) {
    wchar_t exePath[MAX_PATH] = {};
    auto exeLen = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (!(exeLen > 0 && exeLen < MAX_PATH)) {
        log::warn("[UpdateChecker] GetModuleFileNameW failed while staging swap");
        return false;
    }

    std::filesystem::path currentPackage = Mod::get()->getPackagePath();
    std::filesystem::path backupPackage = currentPackage;
    backupPackage += ".old";
    std::filesystem::path scriptPath =
        Mod::get()->getSaveDir() / "updates" /
        (relaunch ? "apply-pending-update.ps1" : "apply-pending-update-silent.ps1");
    std::error_code ec;
    std::filesystem::create_directories(scriptPath.parent_path(), ec);
    if (ec) {
        log::warn("[UpdateChecker] Failed to create updater script dir: {}", ec.message());
        return false;
    }

    auto exeString = geode::utils::string::pathToString(std::filesystem::path(exePath));
    auto workingDirString = geode::utils::string::pathToString(std::filesystem::path(exePath).parent_path());
    auto sourceString = geode::utils::string::pathToString(pendingUpdatePath);
    auto destString = geode::utils::string::pathToString(currentPackage);
    auto backupString = geode::utils::string::pathToString(backupPackage);

    // Optional relaunch segment: restart GD on user-triggered update, silent on auto-update.
    std::string relaunchSegment = relaunch
        ? "Start-Process -FilePath $exe -WorkingDirectory $workingDir\n"
        : "";
    std::string relaunchOnFailure = relaunch
        ? "Start-Process -FilePath $exe -WorkingDirectory $workingDir\n"
        : "";

    auto scriptBody = fmt::format(
        R"ps($pidToWait = {0}
$source = '{1}'
$dest = '{2}'
$backup = '{3}'
$exe = '{4}'
$workingDir = '{5}'

for ($i = 0; $i -lt 200; $i++) {{
    if (-not (Get-Process -Id $pidToWait -ErrorAction SilentlyContinue)) {{ break }}
    Start-Sleep -Milliseconds 250
}}

for ($i = 0; $i -lt 40; $i++) {{
    try {{
        if (Test-Path -LiteralPath $backup) {{ Remove-Item -LiteralPath $backup -Force -ErrorAction SilentlyContinue }}
        if (Test-Path -LiteralPath $dest) {{ Move-Item -LiteralPath $dest -Destination $backup -Force }}
        Move-Item -LiteralPath $source -Destination $dest -Force
        if (Test-Path -LiteralPath $backup) {{ Remove-Item -LiteralPath $backup -Force -ErrorAction SilentlyContinue }}
        {6}exit 0
    }} catch {{
        Start-Sleep -Milliseconds 250
    }}
}}

{7}exit 1
)ps",
        GetCurrentProcessId(),
        escapePowerShellLiteral(sourceString),
        escapePowerShellLiteral(destString),
        escapePowerShellLiteral(backupString),
        escapePowerShellLiteral(exeString),
        escapePowerShellLiteral(workingDirString),
        relaunchSegment,
        relaunchOnFailure
    );

    std::ofstream scriptFile(scriptPath, std::ios::binary | std::ios::trunc);
    if (!scriptFile) {
        log::warn("[UpdateChecker] Failed to open updater script file");
        return false;
    }
    scriptFile << scriptBody;
    scriptFile.close();
    if (!scriptFile) {
        log::warn("[UpdateChecker] Failed to write updater script file");
        return false;
    }

    std::wstring commandLine =
        L"powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" +
        scriptPath.wstring() +
        L"\"";
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        log::warn("[UpdateChecker] Failed to launch updater helper: {}", static_cast<unsigned long>(GetLastError()));
        return false;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    log::info("[UpdateChecker] Spawned updater helper {} (relaunch={})",
        geode::utils::string::pathToString(scriptPath), relaunch);
    return true;
}
#endif

} // namespace

bool UpdateChecker::restartToApplyPendingUpdate() const {
    if (!this->hasPendingInstall()) {
        return false;
    }

#ifdef GEODE_IS_WINDOWS
    return spawnUpdaterHelper(m_pendingUpdatePath, /*relaunch=*/true);
#else
    return false;
#endif
}

bool UpdateChecker::applyPendingUpdateInPlace() const {
    if (!this->hasPendingInstall()) {
        return false;
    }

#ifdef GEODE_IS_WINDOWS
    return spawnUpdaterHelper(m_pendingUpdatePath, /*relaunch=*/false);
#else
    return false;
#endif
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
                log::info("[UpdateChecker] Auto-update downloaded and staged. Will apply on exit.");
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
