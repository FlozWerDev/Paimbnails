#pragma once

#include "../GifImportTypes.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace paimon::gifimport {

bool isVideoFile(std::filesystem::path const& path);

// Reparte capturas por toda la duracion del video y las devuelve en RGBA como si
// vinieran de un GIF. Bloquea mientras decodifica, asi que va en el hilo de carga.
std::shared_ptr<SourceAnimation> decodeVideo(
    std::filesystem::path const& path,
    int maxFrames,
    std::string& error,
    double maxDurationSeconds = 0.0
);

} // namespace paimon::gifimport
