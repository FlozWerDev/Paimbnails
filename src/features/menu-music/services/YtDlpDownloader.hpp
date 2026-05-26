#pragma once

// YtDlpDownloader — wrapper async sobre el binario yt-dlp.
//
// Filosofia:
//   * yt-dlp hace TODO el trabajo pesado (streaming, extraccion, metadatos).
//     Nosotros solo lanzamos el subproceso con los flags correctos y
//     esperamos al final para registrar el track en la libreria.
//   * El subproceso se lanza en un thread worker para NO bloquear el main
//     thread del juego. Reportamos progreso y resultado via callback que
//     se despacha en main thread con Loader::queueInMainThread.
//   * Busqueda del binario (en orden):
//        1. [modDir]/yt-dlp/yt-dlp.exe (Windows) ó [modDir]/yt-dlp/yt-dlp
//        2. variables de entorno comunes (PATH, Scoop, Chocolatey)
//        3. ultima chance: nombre pelado 'yt-dlp'
//   * La miniatura se pide en --convert-thumbnails jpg para tener un
//     formato que cocos pueda leer sin dependencias.

#include <Geode/Geode.hpp>
#include <functional>
#include <string>
#include <filesystem>

namespace paimon::menumusic {

struct YtDlpResult {
    bool success = false;
    std::string trackId;          // id asignado al track (para enlazar UI)
    std::string audioPath;        // path al audio final (.mp3/.m4a/.opus) si success
    std::string coverPath;        // path al thumbnail (.jpg) si success
    std::string displayName;      // titulo extraido
    std::string artist;           // artista extraido del info.json (canal o uploader)
    std::string error;            // mensaje si !success
    int exitCode = 0;
};

struct YtDlpProgress {
    std::string stage;   // "detecting", "downloading", "converting", "done", "error"
    float percent = 0.f; // 0..1 (aproximado — yt-dlp lo reporta por linea)
    std::string message; // ultima linea significativa del log
};

using YtDlpCompleteCallback = std::function<void(YtDlpResult)>;
using YtDlpProgressCallback = std::function<void(YtDlpProgress)>;

class YtDlpDownloader {
public:
    static YtDlpDownloader& get();

    // Devuelve el path absoluto al binario de yt-dlp, o cadena vacia si
    // no lo encontramos en ningun sitio. El resultado se cachea 60s
    // para no llamar a exec en cada apertura del popup.
    std::string locateBinary();

    // true si locateBinary() devuelve algo no vacio.
    bool isAvailable();

    // Lanza una descarga. `url` puede ser cualquier thing que yt-dlp
    // sepa resolver (YouTube, TikTok, SoundCloud, etc). `trackId` es el
    // id que se usara para nombrar los archivos.
    //
    // Los callbacks corren siempre en main thread.
    void download(
        const std::string& url,
        const std::string& trackId,
        YtDlpProgressCallback onProgress,
        YtDlpCompleteCallback onComplete
    );

    bool isBusy() const { return m_activeJobs > 0; }

private:
    YtDlpDownloader() = default;
    ~YtDlpDownloader() = default;
    YtDlpDownloader(const YtDlpDownloader&) = delete;
    YtDlpDownloader& operator=(const YtDlpDownloader&) = delete;

    std::atomic<int> m_activeJobs{0};

    std::string m_cachedBinary;
    std::chrono::steady_clock::time_point m_cachedBinaryAt;
};

} // namespace paimon::menumusic
