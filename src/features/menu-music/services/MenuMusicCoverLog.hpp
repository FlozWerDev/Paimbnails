#pragma once
// MenuMusicCoverLog — diagnostico de portadas del menu music.
// Los logs normales del mod estan desactivados por defecto (enable-debug-logs),
// asi que este helper escribe SIEMPRE a cover-debug.log y fuerza la consola
// de Geode mientras menu-music-cover-debug este activo.

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