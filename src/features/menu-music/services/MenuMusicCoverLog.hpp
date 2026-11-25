#pragma once
// cover diagnostics are enabled explicitly and batched to a worker.

#include <Geode/Geode.hpp>
#include <filesystem>
#include <mutex>
#include <string>

namespace paimon::menumusic::coverlog {

bool isEnabled();

std::filesystem::path logFilePath();

void info(std::string const& message);
void warn(std::string const& message);

namespace detail {
// callers have already checked the setting before formatting the message.
void infoEnabled(std::string const& message);
void warnEnabled(std::string const& message);
}

inline void info(char const* message) {
    if (!isEnabled()) return;
    detail::infoEnabled(message);
}

inline void warn(char const* message) {
    if (!isEnabled()) return;
    detail::warnEnabled(message);
}

template <typename... Args>
void info(fmt::format_string<Args...> fmt, Args&&... args) {
    if (!isEnabled()) return;
    detail::infoEnabled(fmt::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void warn(fmt::format_string<Args...> fmt, Args&&... args) {
    if (!isEnabled()) return;
    detail::warnEnabled(fmt::format(fmt, std::forward<Args>(args)...));
}

} // namespace paimon::menumusic::coverlog
