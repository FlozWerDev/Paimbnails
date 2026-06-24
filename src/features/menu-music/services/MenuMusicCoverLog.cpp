#include "MenuMusicCoverLog.hpp"

#include <Geode/loader/Log.hpp>
#include <Geode/loader/Mod.hpp>

#include <chrono>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

using namespace geode::prelude;

namespace paimon::menumusic::coverlog {

namespace {

std::mutex& fileMutex() {
    static std::mutex m;
    return m;
}

void appendFile(char const* level, std::string const& message) {
    std::lock_guard lock(fileMutex());
    std::error_code ec;
    auto path = logFilePath();
    std::filesystem::create_directories(path.parent_path(), ec);

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

    std::ofstream out(path, std::ios::app);
    if (out) out << line.str();
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
    return Mod::get()->getSaveDir() / "menu-music" / "cover-debug.log";
}

void info(std::string const& message) {
    appendFile("INFO", message);
    if (isEnabled()) emitConsole(false, message);
}

void warn(std::string const& message) {
    appendFile("WARN", message);
    if (isEnabled()) emitConsole(true, message);
}

} // namespace paimon::menumusic::coverlog