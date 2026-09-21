#pragma once

#include "../GifImportTypes.hpp"

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>

namespace paimon::gifimport {

bool isVideoFile(std::filesystem::path const& path);

// 768 conserva el borde fino; mas resolucion solo gasta memoria.
inline constexpr int kMaxVideoSide = 768;

// Buzon de progreso de decodeVideo; el hilo de carga escribe, la UI lee.
struct VideoProgress {
    std::atomic<bool> cancelled = false;
    std::atomic<int> framesSeen = 0;
    std::atomic<int> framesKept = 0;
};

// Reparte capturas por toda la duracion del video y las devuelve en RGBA como si
// vinieran de un GIF. Bloquea mientras decodifica, asi que va en el hilo de carga.
// Si partialOut no es nulo, marca si hubo corte por stall/deadline.
std::shared_ptr<SourceAnimation> decodeVideo(
    std::filesystem::path const& path,
    int maxFrames,
    std::string& error,
    double maxDurationSeconds = 0.0,
    bool* partialOut = nullptr,
    VideoProgress* progress = nullptr
);

} // namespace paimon::gifimport
