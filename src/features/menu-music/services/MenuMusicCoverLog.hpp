#pragma once
// cover diagnostics: always goes to cover-debug.log, forces Geode console.

#include <Geode/Geode.hpp>
#include <filesystem>
#include <mutex>
#include <string>

namespace paimon::menumusic::coverlog {

bool isEnabled();

std::filesystem::path logFilePath();

void info(std::string const& message);
void warn(std::string const& message);

template <typename... Args>
void info(fmt::format_string<Args...> fmt, Args&&... args) {
    info(fmt::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void warn(fmt::format_string<Args...> fmt, Args&&... args) {
    warn(fmt::format(fmt, std::forward<Args>(args)...));
}

} // namespace paimon::menumusic::coverlog