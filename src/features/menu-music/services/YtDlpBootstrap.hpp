#pragma once

// YtDlpBootstrap — instala yt-dlp automaticamente la primera vez.
//
// yt-dlp pesa ~17MB y se actualiza casi semanal. En vez de bundlear el
// binario en el .geode (que quedaria obsoleto rapido), lo descargamos
// desde GitHub Releases la primera vez que el usuario abre el popup
// de descarga. El binario queda cacheado en:
//
//     [saveDir]/yt-dlp/<platform-binary>
//
// URLs oficiales (release "latest"):
//   Windows x64:  https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe
//   macOS:        https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_macos
//   Linux:        https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_linux
//
// Usamos modo "single binary" (sin ffmpeg) y bajamos audio con
// `-f bestaudio` para evitar recodificar (FMOD reproduce m4a/opus/mp3
// nativamente sin ffmpeg).

#include <Geode/Geode.hpp>
#include <filesystem>
#include <functional>
#include <string>

namespace paimon::menumusic {

struct BootstrapProgress {
    std::string stage;   // "resolving", "downloading", "verifying", "installing", "done", "error"
    float percent = 0.f; // 0..1
    std::string message;
};

using BootstrapCompleteCallback = std::function<void(bool success, std::string pathOrError)>;
using BootstrapProgressCallback = std::function<void(BootstrapProgress)>;

class YtDlpBootstrap {
public:
    static YtDlpBootstrap& get();

    // Path esperado del binario bundleado en el modDir. No verifica
    // existencia — llamar exists() para eso.
    std::filesystem::path bundledPath() const;

    // true si el binario ya esta instalado en bundledPath().
    bool exists() const;

    // Descarga yt-dlp desde GitHub Releases si no existe. Si ya existe,
    // llama onComplete inmediatamente con success=true.
    // Los callbacks corren en main thread.
    void ensureInstalled(
        BootstrapProgressCallback onProgress,
        BootstrapCompleteCallback onComplete
    );

    // Borra el binario descargado (para forzar re-download/update).
    void uninstall();

    bool isDownloading() const { return m_downloading; }

private:
    YtDlpBootstrap() = default;

    // URL del binario para la plataforma actual.
    static std::string releaseUrl();

    std::atomic<bool> m_downloading{false};
};

} // namespace paimon::menumusic
