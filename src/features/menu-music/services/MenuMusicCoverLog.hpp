#pragma once
// Diagnostico de portadas: escribe SIEMPRE a cover-debug.log (los logs
// normales estan desactivados por defecto) y fuerza la consola de Geode.

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