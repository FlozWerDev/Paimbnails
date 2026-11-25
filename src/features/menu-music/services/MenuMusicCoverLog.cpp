#include "../../../utils/FilesystemPath.hpp"
#include "MenuMusicCoverLog.hpp"

#include <Geode/loader/Log.hpp>
#include <Geode/loader/Mod.hpp>

#include <chrono>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <memory>
#include <sstream>
#include <vector>
#include "../../../utils/ThreadTracker.hpp"

using namespace geode::prelude;

namespace paimon::menumusic::coverlog {

namespace {

struct PendingLog {
    std::mutex mutex;
    std::vector<std::string> lines;
    bool writing = false;
};

std::shared_ptr<PendingLog> pendingLog() {
    static auto state = std::make_shared<PendingLog>();
    return state;
}

void drainLog(std::filesystem::path const& path, std::shared_ptr<PendingLog> const& state) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::app);
    for (;;) {
        std::vector<std::string> batch;
        {
            std::lock_guard lock(state->mutex);
            if (state->lines.empty()) {
                state->writing = false;
                return;
            }
            batch.swap(state->lines);
        }
        // never hold the producer lock across filesystem operations.
        if (out) {
            for (auto const& line : batch) out << line;
            out.flush();
        }
    }
}

void appendFile(char const* level, std::string const& message) {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream line;
    line << std::put_time(&tm, "%H:%M:%S") << ' ' << level << ' ' << message << '\n';
    auto state = pendingLog();
    {
        std::lock_guard lock(state->mutex);
        state->lines.push_back(line.str());
        if (state->writing) return;
        state->writing = true;
    }
    auto path = logFilePath();
    if (!paimon::ThreadTracker::get().spawn([state, path = std::move(path)] { drainLog(path, state); })) {
        std::lock_guard lock(state->mutex);
        state->writing = false;
        state->lines.clear();
    }
}

void emitConsole(bool warning, std::string const& message) {
    bool const prevEnabled = Mod::get()->isLoggingEnabled();
    Mod::get()->setLoggingEnabled(true);
    if (warning) {
        log::warn("{}", message);
    } else {
        log::info("{}", message);
    }
    Mod::get()->setLoggingEnabled(prevEnabled);
}

} // namespace

bool isEnabled() {
    return Mod::get()->getSettingValue<bool>("menu-music-cover-debug");
}

std::filesystem::path logFilePath() {
    return paimon::paths::toStdPath(Mod::get()->getSaveDir()) / "menu-music" / "cover-debug.log";
}

void detail::infoEnabled(std::string const& message) {
    appendFile("INFO", message);
    emitConsole(false, message);
}

void detail::warnEnabled(std::string const& message) {
    appendFile("WARN", message);
    emitConsole(true, message);
}

void info(std::string const& message) {
    if (isEnabled()) detail::infoEnabled(message);
}

void warn(std::string const& message) {
    if (isEnabled()) detail::warnEnabled(message);
}

} // namespace paimon::menumusic::coverlog
