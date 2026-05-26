#pragma once

// FfmpegBootstrap — instala ffmpeg automaticamente la primera vez que
// hace falta.
//
// Contexto: FMOD Core (la engine de audio que usa Geometry Dash) NO
// decodifica AAC/M4A ni Opus/WebM en Windows/macOS/Linux por cuestion
// de licencias. YouTube sirve su mejor audio en Opus o AAC, por lo que
// yt-dlp nos entrega archivos que GD no puede reproducir (silencio).
//
// La unica forma realista de arreglarlo es recodificar el audio a MP3
// con ffmpeg despues de que yt-dlp lo descarga. yt-dlp acepta la flag
// `--ffmpeg-location`, asi que solo necesitamos tener el binario cerca.
//
// Igual que YtDlpBootstrap, descargamos el binario bajo demanda, lo
// cacheamos en saveDir, y borramos con uninstall().
//
// Builds usados (single-file, sin dependencias runtime):
//   Windows x64  →  https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip
//                   (dentro del zip: bin/ffmpeg.exe)
//   macOS        →  https://evermeet.cx/ffmpeg/getrelease/zip  (ffmpeg)
//   Linux x64    →  https://johnvansickle.com/ffmpeg/releases/ffmpeg-release-amd64-static.tar.xz
//                   (pero como no queremos acoplar libtar, preferimos una
//                   build .zip comunitaria para linux; si no esta
//                   disponible, informamos al usuario).

#include <Geode/Geode.hpp>
#include <filesystem>
#include <functional>
#include <string>

namespace paimon::menumusic {

struct FfmpegBootstrapProgress {
    std::string stage;   // "resolving", "downloading", "extracting", "installing", "done", "error"
    float percent = 0.f; // 0..1
    std::string message;
};

using FfmpegBootstrapCompleteCallback =
    std::function<void(bool success, std::string pathOrError)>;
using FfmpegBootstrapProgressCallback =
    std::function<void(FfmpegBootstrapProgress)>;

class FfmpegBootstrap {
public:
    static FfmpegBootstrap& get();

    // Path esperado del binario bundleado.
    std::filesystem::path bundledPath() const;

    // true si el binario ya esta instalado.
    bool exists() const;

    // Descarga ffmpeg desde el mirror oficial si no existe. Si ya existe,
    // llama onComplete inmediatamente.
    // Los callbacks corren en main thread.
    void ensureInstalled(
        FfmpegBootstrapProgressCallback onProgress,
        FfmpegBootstrapCompleteCallback onComplete
    );

    // Borra el binario (para re-download / update).
    void uninstall();

    bool isDownloading() const { return m_downloading; }

private:
    FfmpegBootstrap() = default;
    static std::string releaseUrl();

    // Devuelve true si la URL apunta a un .zip que hay que extraer, false
    // si es el binario tal cual.
    static bool isArchive();

    // Devuelve el nombre de la entrada dentro del zip que debemos
    // extraer. "bin/ffmpeg.exe" en Windows, etc.
    static std::string archiveEntry();

    std::atomic<bool> m_downloading{false};
};

} // namespace paimon::menumusic
