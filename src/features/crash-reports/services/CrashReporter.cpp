#include "CrashReporter.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/ThreadTracker.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/Dirs.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/utils/file.hpp>
#include <algorithm>
#include <filesystem>
#include <matjson.hpp>
#include <vector>

using namespace geode::prelude;

namespace {

constexpr char const* kModuleId = "paimbnails.crashreports.system";
constexpr char const* kSentKey = "crash-reports-sent";
constexpr size_t kMaxCrashBytes = 128 * 1024;
constexpr size_t kMaxLogBytes = 256 * 1024;
constexpr size_t kMaxPerLaunch = 3;
constexpr size_t kSentHistory = 24;

struct LogFile {
    std::filesystem::path path;
    std::filesystem::file_time_type time;
};

std::vector<LogFile> collectLogs(std::filesystem::path const& dir) {
    std::vector<LogFile> files;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return files;

    for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec) || entry.path().extension() != ".log") continue;
        auto time = entry.last_write_time(ec);
        if (ec) {
            ec.clear();
            continue;
        }
        files.push_back({entry.path(), time});
    }

    std::sort(files.begin(), files.end(), [](LogFile const& a, LogFile const& b) {
        return a.time > b.time;
    });
    return files;
}

// The session log that goes with a crash is the one whose last write lands
// closest to it, skipping the log this launch is currently writing to.
std::filesystem::path sessionLogFor(std::filesystem::file_time_type crashTime) {
    std::error_code ec;
    auto current = std::filesystem::weakly_canonical(log::getCurrentLogPath(), ec);
    if (ec) current.clear();

    std::filesystem::path best;
    std::filesystem::file_time_type::duration bestDistance{};

    for (auto const& file : collectLogs(dirs::getGeodeLogDir())) {
        ec.clear();
        auto canonical = std::filesystem::weakly_canonical(file.path, ec);
        if (!ec && !current.empty() && canonical == current) continue;

        auto distance = file.time > crashTime ? file.time - crashTime : crashTime - file.time;
        if (best.empty() || distance < bestDistance) {
            best = file.path;
            bestDistance = distance;
        }
    }
    return best;
}

std::string readCapped(std::filesystem::path const& path, size_t limit, bool keepTail) {
    auto text = utils::file::readString(path);
    if (!text) return {};

    auto content = std::move(text).unwrap();
    if (content.size() <= limit) return content;
    return keepTail ? content.substr(content.size() - limit) : content.substr(0, limit);
}

// Logs are full of absolute paths; the account folder is the only part of them
// that identifies the player, so it goes out as a placeholder.
void scrubUserPaths(std::string& text) {
    for (std::string_view needle : {"\\Users\\", "/Users/", "/home/"}) {
        size_t at = 0;
        while ((at = text.find(needle, at)) != std::string::npos) {
            size_t start = at + needle.size();
            size_t end = text.find_first_of("\\/\"' \t\r\n", start);
            if (end == std::string::npos) end = text.size();
            if (end == start) {
                at = start;
                continue;
            }
            text.replace(start, end - start, "<user>");
            at = start + 6;
        }
    }
}

// Geode writes the crash timestamp as the first line of the crashlog.
std::string crashTimestamp(std::string const& crashlog) {
    auto end = crashlog.find('\n');
    auto line = crashlog.substr(0, end == std::string::npos ? crashlog.size() : end);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    return line.size() <= 40 ? line : std::string{};
}

std::vector<std::string> readSentList() {
    auto stored = Mod::get()->getSavedValue<matjson::Value>(kSentKey, matjson::Value::array());
    std::vector<std::string> names;
    if (!stored.isArray()) return names;

    for (auto const& item : stored) {
        auto name = item.asString().unwrapOr("");
        if (!name.empty()) names.push_back(name);
    }
    return names;
}

void markSent(std::string const& name) {
    auto names = readSentList();
    names.insert(names.begin(), name);
    if (names.size() > kSentHistory) names.resize(kSentHistory);

    auto stored = matjson::Value::array();
    for (auto const& entry : names) stored.push(entry);
    Mod::get()->setSavedValue(kSentKey, stored);
}

void uploadCrashlog(LogFile const& file, std::string const& geodeVersion, std::string const& gameVersion) {
    auto const& path = file.path;
    auto crashTime = file.time;

    HttpClient::CrashReport report;
    report.crashlogName = path.filename().string();
    report.crashlog = readCapped(path, kMaxCrashBytes, false);
    if (report.crashlog.empty()) {
        log::warn("[CrashReports] Could not read {}", report.crashlogName);
        return;
    }

    report.crashedAt = crashTimestamp(report.crashlog);
    report.geodeVersion = geodeVersion;
    report.gameVersion = gameVersion;

    if (auto session = sessionLogFor(crashTime); !session.empty()) {
        report.geodeLog = readCapped(session, kMaxLogBytes, true);
    }

    scrubUserPaths(report.crashlog);
    scrubUserPaths(report.geodeLog);

    queueInMainThread([report = std::move(report)]() {
        if (paimon::isRuntimeShuttingDown()) return;
        auto name = report.crashlogName;
        HttpClient::get().uploadCrashLog(report, [name](bool success, std::string const& response) {
            if (!success) {
                log::warn("[CrashReports] Upload of {} failed: {}", name, response);
                return;
            }
            log::info("[CrashReports] Sent {}", name);
            markSent(name);
            PaimonNotify::show("Crash log sent to the Paimbnails devs", NotificationIcon::Info);
        });
    });
}

} // namespace

namespace paimon::crash {

void reportPendingCrashes() {
    if (!modules::isEnabled(kModuleId)) return;

    auto crashlogs = collectLogs(dirs::getCrashlogsDir());
    if (crashlogs.empty()) return;
    if (crashlogs.size() > kMaxPerLaunch) crashlogs.resize(kMaxPerLaunch);

    auto sent = readSentList();
    std::vector<LogFile> pending;
    for (auto const& file : crashlogs) {
        auto name = file.path.filename().string();
        if (std::find(sent.begin(), sent.end(), name) == sent.end()) pending.push_back(file);
    }
    if (pending.empty()) return;

    log::info("[CrashReports] {} crashlog(s) pending upload", pending.size());

    // Read on the main thread: Loader fills the game version lazily.
    auto geodeVersion = Loader::get()->getVersion().toVString();
    auto gameVersion = Loader::get()->getGameVersion();

    ThreadTracker::get().spawn([pending = std::move(pending), geodeVersion, gameVersion]() {
        geode::utils::thread::setName("PaimonCrashReports");
        for (auto const& file : pending) {
            if (isRuntimeShuttingDown()) return;
            uploadCrashlog(file, geodeVersion, gameVersion);
        }
    });
}

} // namespace paimon::crash
