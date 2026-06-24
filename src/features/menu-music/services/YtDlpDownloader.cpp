#include "YtDlpDownloader.hpp"
#include "YtDlpBootstrap.hpp"
#include "FfmpegBootstrap.hpp"
#include "MenuMusicLibrary.hpp"

#include <Geode/loader/Loader.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/utils/file.hpp>
#include <matjson.hpp>
#include <fmt/format.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <regex>
#include <thread>
#include "../../../utils/ThreadTracker.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

#ifdef GEODE_IS_WINDOWS
    // Preferimos las APIs Win32 directas sobre system() porque necesitamos
    // poder leer stdout del proceso en tiempo real (progreso) sin abrir
    // una ventana de consola.
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    // POSIX: fork+execvp para evitar shell (anti-injection) + signal/poll
    // para timeout y SIGKILL si el proceso se cuelga.
    #include <unistd.h>
    #include <fcntl.h>
    #include <signal.h>
    #include <sys/wait.h>
#endif

using namespace geode::prelude;

namespace paimon::menumusic {

YtDlpDownloader& YtDlpDownloader::get() {
    static YtDlpDownloader instance;
    return instance;
}

// Busqueda del binario
//
// Buscamos primero el binario bundleado (descargado por YtDlpBootstrap),
// luego rutas tipicas del sistema, y como ultimo recurso el PATH.

namespace {

#ifdef GEODE_IS_WINDOWS
    constexpr const char* kYtDlpExeName = "yt-dlp.exe";
#else
    constexpr const char* kYtDlpExeName = "yt-dlp";
#endif

static bool fileExists(const std::filesystem::path& p) {
    std::error_code ec;
    return std::filesystem::is_regular_file(p, ec);
}

static std::string joinArgWindows(const std::string& s) {
    // Escapado basico de args para CreateProcess: rodeamos con comillas
    // si hay espacios y escapamos backslashes / comillas internas.
    if (s.find_first_of(" \t\"") == std::string::npos) return s;
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else out += c;
    }
    out += '"';
    return out;
}

// Para shells POSIX: escapamos con single quotes.
static std::string joinArgPosix(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += '\'';
    return out;
}

static std::string joinArg(const std::string& s) {
#ifdef GEODE_IS_WINDOWS
    return joinArgWindows(s);
#else
    return joinArgPosix(s);
#endif
}

// Ejecuta un comando y captura stdout+stderr linea a linea, invocando
// onLine por cada linea leida. Devuelve el exit code (-1 si no se pudo
// lanzar). Bloquea el thread que lo llama — llamar desde worker.
//
// Existen dos variantes:
//   - runAndCaptureArgv(argv, onLine, timeoutMs): toma un vector de strings
//     que se pasan DIRECTAMENTE al proceso sin involucrar shell. Esto evita
//     command injection si algun argumento contiene metachars del shell
//     (', `, $, ;, &, |, etc.). USAR para datos de usuario (URLs, nombres).
//
//   - runAndCapture(cmdLine, onLine): mantenida para casos triviales
//     (`where yt-dlp.exe`) donde el cmd es constante hardcoded. NO usar
//     con datos de usuario.
//
// Ambas variantes incluyen timeout opcional para evitar que un yt-dlp
// colgado bloquee el worker thread indefinidamente y termine colgando
// ThreadTracker en atexit.
#ifdef GEODE_IS_WINDOWS
// Combina argv en una linea de comandos Windows con escape correcto.
// Ver: https://learn.microsoft.com/en-us/cpp/c-runtime-library/parsing-cpp-command-line-arguments
static std::string winQuoteArg(const std::string& arg) {
    if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string::npos) {
        return arg;
    }
    std::string out;
    out.push_back('"');
    for (auto it = arg.begin(); ; ++it) {
        unsigned backslashes = 0;
        while (it != arg.end() && *it == '\\') {
            ++backslashes;
            ++it;
        }
        if (it == arg.end()) {
            out.append(backslashes * 2, '\\');
            break;
        } else if (*it == '"') {
            out.append(backslashes * 2 + 1, '\\');
            out.push_back(*it);
        } else {
            out.append(backslashes, '\\');
            out.push_back(*it);
        }
    }
    out.push_back('"');
    return out;
}

static std::string buildWindowsCmdLine(const std::vector<std::string>& argv) {
    std::string cmd;
    for (size_t i = 0; i < argv.size(); ++i) {
        if (i) cmd.push_back(' ');
        cmd += winQuoteArg(argv[i]);
    }
    return cmd;
}

static int runWindowsProcess(const std::wstring& wideCmdLine,
                             const std::function<void(const std::string&)>& onLine,
                             int timeoutMs) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr, writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) return -1;
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};

    // CreateProcessW puede modificar wideCmdLine — necesita buffer mutable.
    std::wstring mutableCmd = wideCmdLine;
    BOOL ok = CreateProcessW(
        nullptr,
        mutableCmd.data(),
        nullptr, nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr, nullptr,
        &si, &pi
    );

    CloseHandle(writePipe);
    if (!ok) {
        CloseHandle(readPipe);
        return -1;
    }

    // Lectura del pipe con check periodico de timeout. PeekNamedPipe permite
    // verificar disponibilidad sin bloquear. Si no hay data y excedimos el
    // timeout absoluto, terminamos el proceso para evitar cuelgues.
    auto startTime = std::chrono::steady_clock::now();
    std::string buffer;
    char chunk[1024];
    DWORD readN = 0;
    bool timedOut = false;

    while (true) {
        DWORD avail = 0;
        if (!PeekNamedPipe(readPipe, nullptr, 0, nullptr, &avail, nullptr)) {
            break; // pipe cerrado por el child
        }

        if (avail > 0) {
            DWORD toRead = std::min<DWORD>(avail, sizeof(chunk));
            if (!ReadFile(readPipe, chunk, toRead, &readN, nullptr) || readN == 0) {
                break;
            }
            buffer.append(chunk, readN);
            std::size_t pos = 0;
            while (true) {
                auto n = buffer.find_first_of("\r\n", pos);
                if (n == std::string::npos) break;
                onLine(buffer.substr(pos, n - pos));
                pos = n + 1;
                if (pos < buffer.size() && buffer[pos - 1] == '\r' && buffer[pos] == '\n') {
                    pos++;
                }
            }
            buffer.erase(0, pos);
        } else {
            // Sin data ahora: esperamos un poco antes de re-poll.
            DWORD waitRes = WaitForSingleObject(pi.hProcess, 100);
            if (waitRes == WAIT_OBJECT_0) {
                // Proceso termino — leer ultimos bytes del pipe y salir.
                DWORD finalAvail = 0;
                if (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &finalAvail, nullptr) && finalAvail > 0) {
                    continue;
                }
                break;
            }
        }

        if (timeoutMs > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startTime).count();
            if (elapsed > timeoutMs) {
                geode::log::warn("[YtDlpDownloader] subprocess timeout after {}ms — killing", timeoutMs);
                TerminateProcess(pi.hProcess, 1);
                timedOut = true;
                break;
            }
        }

        // Tambien chequeamos shutdown del runtime
        if (paimon::isRuntimeShuttingDown()) {
            TerminateProcess(pi.hProcess, 1);
            break;
        }
    }

    if (!buffer.empty()) onLine(buffer);

    CloseHandle(readPipe);
    WaitForSingleObject(pi.hProcess, 5000);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return timedOut ? -2 : static_cast<int>(exitCode);
}

static int runAndCaptureArgv(const std::vector<std::string>& argv,
                             const std::function<void(const std::string&)>& onLine,
                             int timeoutMs = 0) {
    if (argv.empty()) return -1;
    std::string cmdLine = buildWindowsCmdLine(argv);

    int wideLen = MultiByteToWideChar(CP_UTF8, 0, cmdLine.c_str(), -1, nullptr, 0);
    std::wstring wide(wideLen, 0);
    MultiByteToWideChar(CP_UTF8, 0, cmdLine.c_str(), -1, wide.data(), wideLen);

    return runWindowsProcess(wide, onLine, timeoutMs);
}

static int runAndCapture(const std::string& cmdLine,
                         const std::function<void(const std::string&)>& onLine) {
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, cmdLine.c_str(), -1, nullptr, 0);
    std::wstring wide(wideLen, 0);
    MultiByteToWideChar(CP_UTF8, 0, cmdLine.c_str(), -1, wide.data(), wideLen);

    // Sin timeout para `where`/`which` simples — son comandos rapidos.
    return runWindowsProcess(wide, onLine, 0);
}
#else
// POSIX: usar fork+execvp con argv (sin shell) — evita command injection.
// Tambien implementa timeout matando el proceso si excede el limite.
static int runAndCaptureArgv(const std::vector<std::string>& argv,
                             const std::function<void(const std::string&)>& onLine,
                             int timeoutMs = 0) {
    if (argv.empty()) return -1;

    int pipefd[2];
    if (pipe(pipefd) != 0) return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        // Child: redirigir stdout y stderr al pipe.
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        // Construir argv-style char* array. NO usamos shell.
        std::vector<char*> cargv;
        cargv.reserve(argv.size() + 1);
        for (auto& s : argv) cargv.push_back(const_cast<char*>(s.c_str()));
        cargv.push_back(nullptr);

        execvp(cargv[0], cargv.data());
        _exit(127); // execvp solo retorna en error
    }

    // Parent: leer pipe con timeout.
    close(pipefd[1]);

    auto startTime = std::chrono::steady_clock::now();
    std::string buffer;
    char chunk[1024];
    bool timedOut = false;

    // Set non-blocking on read end para poder hacer poll-style con timeout.
    int flags = fcntl(pipefd[0], F_GETFL, 0);
    if (flags >= 0) fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

    while (true) {
        ssize_t n = read(pipefd[0], chunk, sizeof(chunk));
        if (n > 0) {
            buffer.append(chunk, n);
            std::size_t pos = 0;
            while (true) {
                auto nl = buffer.find('\n', pos);
                if (nl == std::string::npos) break;
                std::string line = buffer.substr(pos, nl - pos);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                onLine(line);
                pos = nl + 1;
            }
            buffer.erase(0, pos);
        } else if (n == 0) {
            break; // EOF
        } else {
            // EAGAIN/EWOULDBLOCK: no data — chequear estado del proceso.
            int status = 0;
            pid_t w = waitpid(pid, &status, WNOHANG);
            if (w == pid) {
                // child termino — drenar lo que quede.
                while ((n = read(pipefd[0], chunk, sizeof(chunk))) > 0) {
                    buffer.append(chunk, n);
                }
                break;
            }
            if (w < 0) break;

            if (timeoutMs > 0) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - startTime).count();
                if (elapsed > timeoutMs) {
                    geode::log::warn("[YtDlpDownloader] subprocess timeout after {}ms — killing", timeoutMs);
                    kill(pid, SIGKILL);
                    timedOut = true;
                    break;
                }
            }

            if (paimon::isRuntimeShuttingDown()) {
                kill(pid, SIGKILL);
                break;
            }

            // Espera corta antes de re-poll.
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    if (!buffer.empty()) onLine(buffer);
    close(pipefd[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    if (timedOut) return -2;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

static int runAndCapture(const std::string& cmdLine,
                         const std::function<void(const std::string&)>& onLine) {
    // Mantenida para casos hardcoded como `which yt-dlp`. NO usar con datos
    // de usuario — usa shell y abre command injection. runAndCaptureArgv()
    // es el path seguro para descargas reales.
    std::string full = cmdLine + " 2>&1";
    FILE* fp = popen(full.c_str(), "r");
    if (!fp) return -1;

    std::string buffer;
    char chunk[1024];
    while (fgets(chunk, sizeof(chunk), fp)) {
        buffer += chunk;
        std::size_t pos = 0;
        while (true) {
            auto n = buffer.find('\n', pos);
            if (n == std::string::npos) break;
            std::string line = buffer.substr(pos, n - pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            onLine(line);
            pos = n + 1;
        }
        buffer.erase(0, pos);
    }
    if (!buffer.empty()) onLine(buffer);

    int status = pclose(fp);
    if (status == -1) return -1;
#ifdef WEXITSTATUS
    if (WIFEXITED(status)) return WEXITSTATUS(status);
#endif
    return status;
}
#endif

// Detecta el path al binario probando en orden.
static std::filesystem::path resolveScoopPath() {
#ifdef GEODE_IS_WINDOWS
    char* buf = nullptr;
    std::size_t len = 0;
    if (_dupenv_s(&buf, &len, "USERPROFILE") == 0 && buf != nullptr) {
        std::filesystem::path p = std::filesystem::path(buf) / "scoop" / "shims" / kYtDlpExeName;
        free(buf);
        return p;
    }
    if (buf) free(buf);
#endif
    return {};
}

static std::string detectYtDlp() {
    // Maxima prioridad: el binario auto-instalado por YtDlpBootstrap.
    auto bundled = YtDlpBootstrap::get().bundledPath();
    if (fileExists(bundled)) {
        return geode::utils::string::pathToString(bundled);
    }

    std::vector<std::filesystem::path> candidates = {
        // Config dir alternativo (legacy / user-placed).
        Mod::get()->getConfigDir() / "yt-dlp" / kYtDlpExeName,
        Mod::get()->getConfigDir() / kYtDlpExeName,
#ifdef GEODE_IS_WINDOWS
        // Chocolatey
        std::filesystem::path("C:/ProgramData/chocolatey/bin") / kYtDlpExeName,
        // Python Scripts (mainstream install paths)
        std::filesystem::path("C:/Python311/Scripts") / kYtDlpExeName,
        std::filesystem::path("C:/Python312/Scripts") / kYtDlpExeName,
#else
        // Rutas unix comunes
        std::filesystem::path("/usr/local/bin") / kYtDlpExeName,
        std::filesystem::path("/usr/bin") / kYtDlpExeName,
        std::filesystem::path("/opt/homebrew/bin") / kYtDlpExeName,
#endif
    };

    for (const auto& c : candidates) {
        if (fileExists(c)) return geode::utils::string::pathToString(c);
    }

    // Scoop (requiere resolver %USERPROFILE%).
    auto scoopPath = resolveScoopPath();
    if (!scoopPath.empty() && fileExists(scoopPath)) {
        return geode::utils::string::pathToString(scoopPath);
    }

    // Ultima chance: confiar en el PATH. Construimos el path a traves del
    // nombre pelado y dejamos que el shell resuelva. Pero verificamos
    // que exista preguntando al SO.
#ifdef GEODE_IS_WINDOWS
    // Probamos con 'where'
    std::string whereOut;
    runAndCapture("where yt-dlp.exe", [&](const std::string& line) {
        if (whereOut.empty()) whereOut = line;
    });
    if (!whereOut.empty() && fileExists(whereOut)) return whereOut;
#else
    std::string whichOut;
    runAndCapture("which yt-dlp", [&](const std::string& line) {
        if (whichOut.empty()) whichOut = line;
    });
    if (!whichOut.empty() && fileExists(whichOut)) return whichOut;
#endif

    return "";
}

// Parser muy simple del output de yt-dlp para extraer el % cuando lo
// reporta en formato "[download]  42.3% of ...".
static float parseProgressLine(const std::string& line) {
    static const std::regex reProg(R"(\[download\]\s+(\d+(?:\.\d+)?)%)");
    std::smatch m;
    if (std::regex_search(line, m, reProg)) {
        auto result = geode::utils::numFromString<float>(m[1].str());
        if (result.isOk()) {
            return result.unwrap() / 100.f;
        }
    }
    return -1.f;
}

} // namespace

// API

std::string YtDlpDownloader::locateBinary() {
    auto now = std::chrono::steady_clock::now();
    if (!m_cachedBinary.empty() &&
        std::chrono::duration_cast<std::chrono::seconds>(now - m_cachedBinaryAt).count() < 60) {
        return m_cachedBinary;
    }
    m_cachedBinary = detectYtDlp();
    m_cachedBinaryAt = now;
    return m_cachedBinary;
}

bool YtDlpDownloader::isAvailable() {
    return !locateBinary().empty();
}

void YtDlpDownloader::download(
    const std::string& url,
    const std::string& trackId,
    YtDlpProgressCallback onProgress,
    YtDlpCompleteCallback onComplete
) {
    auto binary = locateBinary();
    if (binary.empty()) {
        Loader::get()->queueInMainThread([onComplete, trackId]() {
            YtDlpResult r;
            r.trackId = trackId;
            r.error = "__NEED_YTDLP__";
            if (onComplete) onComplete(std::move(r));
        });
        return;
    }

    // Comprobacion de ffmpeg
    //
    // FMOD en Geometry Dash no decodifica AAC/M4A ni Opus/WebM. Como
    // YouTube sirve casi todo en alguno de esos formatos, la UNICA
    // forma fiable de garantizar que el track sonara es pedir a yt-dlp
    // que recodifique a MP3 tras la descarga. Eso requiere ffmpeg.
    //
    // Aqui NO lanzamos la descarga de ffmpeg automaticamente. Si falta,
    // devolvemos un error con codigo especial `__NEED_FFMPEG__` para
    // que la UI (MenuMusicAddPopup) pregunte al usuario y, si acepta,
    // muestre su propio popup de instalacion con barra de progreso antes
    // de reintentar.
    auto& ffmpeg = FfmpegBootstrap::get();
    if (!ffmpeg.exists()) {
        Loader::get()->queueInMainThread([onComplete, trackId]() {
            YtDlpResult r;
            r.trackId = trackId;
            r.error = "__NEED_FFMPEG__";
            if (onComplete) onComplete(std::move(r));
        });
        return;
    }

    auto ffmpegPath = geode::utils::string::pathToString(ffmpeg.bundledPath());

    auto& lib = MenuMusicLibrary::get();
    auto tracksDir = lib.getTracksDir();
    auto coversDir = lib.getCoversDir();

    // Formato elegido por el usuario
    //
    // La setting `menuMusicDownloadFormat` controla en que codec/
    // contenedor quedara el archivo final:
    //
    //   * "mp3"  → MP3 (.mp3). Requiere RECODIFICAR (con ffmpeg) porque
    //     no hay stream MP3 nativo en la mayoria de fuentes. Es el formato
    //     mas universal y compatible con FMOD en GD.
    //
    //   * "m4a"  → AAC en MP4 (.m4a). YouTube tambien sirve AAC nativo;
    //     remux directo, sin perdida anadida. Buena compatibilidad.
    //
    // NOTA: Opus (.opus) NO esta soportado porque FMOD en GD no lo
    // decodifica correctamente en todos los escenarios.
    //
    // Fallback: si la setting es desconocida, usamos mp3.
    std::string formatChoice = "mp3";
    try {
        formatChoice = Mod::get()->getSavedValue<std::string>("menuMusicDownloadFormat", "mp3");
    } catch (...) {
        // Puede que la setting no exista (mod actualizado sobre save viejo).
    }
    // Opus NO esta soportado por este mod (FMOD en GD no lo decodifica
    // correctamente en todos los casos). Solo permitimos mp3 y m4a.
    if (formatChoice != "mp3" && formatChoice != "m4a") {
        formatChoice = "mp3";
    }

    // Extension de archivo esperada al terminar, para buscarla luego.
    std::string expectedExt;
    if (formatChoice == "mp3") {
        expectedExt = ".mp3";
    } else {
        expectedExt = ".m4a";
    }

    // Comando
    //
    // Dos flows distintos segun el formato:
    //
    // 1. mp3  → `-x --audio-format mp3 --audio-quality 0`
    //           yt-dlp descarga bestaudio y ffmpeg re-codifica a MP3.
    //
    // 2. m4a  → `-f bestaudio[ext=m4a]/bestaudio -x --audio-format m4a`
    //           pide AAC nativo (YouTube/sc) y ffmpeg solo re-empaqueta.
    //
    // En ambos casos dejamos que yt-dlp invoque ffmpeg para la
    // post-procesacion; ffmpeg esta disponible porque lo verificamos
    // arriba.

    std::string formatSelector;
    std::string audioFormatArg;
    if (formatChoice == "mp3") {
        formatSelector = "bestaudio/best";
        audioFormatArg = "mp3";
    } else {
        // m4a
        formatSelector = "bestaudio[ext=m4a]/bestaudio/best";
        audioFormatArg = "m4a";
    }

    std::string templatePath =
        geode::utils::string::pathToString(tracksDir / (trackId + ".%(ext)s"));

    // CRITICAL: usar argv array en lugar de string concatenado para evitar
    // command injection. Una URL con `;`, `&`, `'`, `$(...)`, `` ` `` etc.
    // no se interpreta por shell porque execvp/CreateProcessW no usa shell.
    std::vector<std::string> argv = {
        binary,
        "--no-playlist",
        "-f", formatSelector,
        "-x",
        "--audio-format", audioFormatArg,
        "--audio-quality", "0",
        "--ffmpeg-location", ffmpegPath,
        "--write-thumbnail",
        "--write-info-json",
        "--no-warnings",
        "--newline",
        "--progress",
        "-o", templatePath,
        url,
    };

    log::info("[yt-dlp] starting download. trackId={}, url='{}', format={}",
        trackId, url, formatChoice);

    m_activeJobs.fetch_add(1, std::memory_order_relaxed);

    paimon::ThreadTracker::get().spawn([this, argv, trackId, url, tracksDir, coversDir, expectedExt, formatChoice,
                 onProgress, onComplete]() mutable {
        geode::utils::thread::setName("Paimon YT-DLP Worker");
        if (paimon::isRuntimeShuttingDown()) {
            m_activeJobs.fetch_sub(1, std::memory_order_relaxed);
            return;
        }

        YtDlpResult result;
        result.trackId = trackId;

        // Ultima linea "generica" (no de progreso) para poder reportar
        // errores mas utiles.
        std::string lastMeaningfulLine;
        std::string lastErrorLine;

        auto hasPrefix = [](const std::string& line, std::string_view prefix) {
            return line.size() >= prefix.size() &&
                   line.compare(0, prefix.size(), prefix) == 0;
        };

        // Timeout 5 minutos: yt-dlp puede tardar mucho en archivos grandes
        // o conexiones lentas, pero no debe colgar el shutdown del juego.
        constexpr int kDownloadTimeoutMs = 5 * 60 * 1000;
        int exitCode = runAndCaptureArgv(argv, [&](const std::string& line) {
            // Log todo al debug log para debugging. No saturan mucho porque
            // yt-dlp no es un mod critico de tiempo real.
            log::debug("[yt-dlp] {}", line);

            float pct = parseProgressLine(line);
            if (pct >= 0.f) {
                if (onProgress) {
                    if (paimon::isRuntimeShuttingDown()) return;
                    Loader::get()->queueInMainThread([onProgress, pct, line]() {
                        if (paimon::isRuntimeShuttingDown()) return;
                        if (onProgress) onProgress(YtDlpProgress{"downloading", pct, line});
                    });
                }
                return; // lineas de progreso no cuentan como "meaningful"
            }

            // Lineas de yt-dlp — filtrar las que empiezan con "ERROR" o
            // contienen errores tipicos de Python para usarlas como mensaje.
            // Guardamos la linea mas reciente "significativa" (no vacia, no
            // solo whitespace, no una linea puramente informativa).
            if (!line.empty()) {
                auto trimmed = line;
                while (!trimmed.empty() && (trimmed.back() == '\r' || trimmed.back() == ' '))
                    trimmed.pop_back();
                if (trimmed.empty()) return;

                lastMeaningfulLine = trimmed;
                // Pista de error explicita.
                if (trimmed.find("ERROR") != std::string::npos ||
                    trimmed.find("error:") != std::string::npos ||
                    hasPrefix(trimmed, "usage:")) {
                    lastErrorLine = trimmed;
                }
            }
        }, kDownloadTimeoutMs);

        result.exitCode = exitCode;

        auto& lib = MenuMusicLibrary::get();

        // Buscar el audio producido. Segun la setting de formato, el
        // archivo final sera <trackId>.mp3, <trackId>.m4a o <trackId>.opus.
        // Si por alguna razon ffmpeg fallo y solo quedo el archivo
        // intermedio (.m4a/.webm cuando pedimos mp3), lo detectamos para
        // reportar un error util en vez de registrar un track silencioso.
        //
        // Tambien aprovechamos para localizar el thumbnail (webp / jpg /
        // png) — ese sigue saliendo en formato nativo.
        //
        // `kFinalAudio` es el conjunto de extensiones "validas como
        // resultado" segun la eleccion: siempre incluimos la extension
        // esperada y las que FMOD sabe leer directamente. Cualquier otra
        // cosa (p.ej. .webm contenedor original) se considera intermedia.
        std::filesystem::path foundAudio;        // el archivo "bueno" si existe
        std::filesystem::path foundIntermediate; // cualquier archivo "malo" que sobrevivio
        std::filesystem::path foundCover;
        std::string foundCoverExt;
        std::filesystem::path foundInfoJson;     // <trackId>.info.json producido por yt-dlp
        {
            // Conjunto de extensiones consideradas "finales" (reproducibles).
            // Coincide con MenuMusicLibrary::isAudioExtension — si FMOD la
            // puede decodificar, nos vale, incluso si el usuario pidio
            // otro formato (esto ayuda a flows donde yt-dlp decide no
            // recodificar porque el stream ya coincide con el pedido).
            static const std::array<std::string_view, 7> kFinalAudio = {
                ".mp3", ".m4a", ".opus", ".ogg", ".oga", ".wav", ".flac"
            };
            static const std::array<std::string_view, 4> kImgExts = {
                ".jpg", ".jpeg", ".png", ".webp"
            };
            std::error_code ec;
            log::debug("[yt-dlp] scanning {} for stem '{}' (expected {})",
                geode::utils::string::pathToString(tracksDir), trackId, expectedExt);
            for (auto& e : std::filesystem::directory_iterator(tracksDir, ec)) {
                if (paimon::isRuntimeShuttingDown()) return;
                if (!e.is_regular_file()) continue;
                const auto& entryPath = e.path();
                // OJO: usamos pathToString en vez de path::string() porque
                // este ultimo devuelve la codificacion ANSI del sistema
                // en Windows (pierde caracteres no ASCII).
                auto stem = geode::utils::string::pathToString(entryPath.stem());
                bool stemMatches =
                    (stem == trackId) ||
                    (stem.size() > trackId.size() &&
                     stem.compare(0, trackId.size(), trackId) == 0 &&
                     (stem[trackId.size()] == '.' || stem[trackId.size()] == '_'));
                if (!stemMatches) continue;

                auto ext = geode::utils::string::toLower(
                    geode::utils::string::pathToString(entryPath.extension()));
                log::debug("[yt-dlp] found file: {} (ext={})",
                    geode::utils::string::pathToString(entryPath), ext);

                bool isFinalAudio = std::find(kFinalAudio.begin(), kFinalAudio.end(), ext)
                    != kFinalAudio.end();
                bool isImg = std::find(kImgExts.begin(), kImgExts.end(), ext)
                    != kImgExts.end();

                // yt-dlp produce archivos secundarios como <trackId>.info.json
                // que no tienen "extension" simple: la `ext` sale como
                // ".json" y el stem termina en ".info". Los detectamos
                // comprobando tanto extension como stem.
                const bool stemEndsInfo = stem.size() > 5 &&
                    stem.compare(stem.size() - 5, 5, ".info") == 0;
                const bool isInfoJson = (ext == ".json") &&
                    (stem == (trackId + ".info") || stemEndsInfo);

                if (isInfoJson) {
                    foundInfoJson = entryPath;
                } else if (isFinalAudio) {
                    // Preferimos el formato pedido: si coincide con
                    // `expectedExt` lo tomamos si/si. En otro caso solo
                    // lo usamos si aun no tenemos candidato mejor.
                    if (ext == expectedExt) {
                        foundAudio = entryPath;
                    } else if (foundAudio.empty()) {
                        foundAudio = entryPath;
                    }
                } else if (isImg) {
                    foundCover = entryPath;
                    foundCoverExt = ext;
                } else {
                    // Todo lo demas (webm, aac raw, etc) es intermedio
                    // que ffmpeg deberia haber eliminado.
                    foundIntermediate = entryPath;
                }
            }
        }

        if (paimon::isRuntimeShuttingDown()) return;

        // Parsear el info.json: extraer title/artist/channel/uploader.
        // yt-dlp produce JSON plano con, entre otros:
        //   "title"    : titulo de la cancion / video
        //   "artist"   : si el origen lo sabe (SoundCloud, Bandcamp)
        //   "creator"  : alternativo
        //   "channel"  : canal de YouTube
        //   "uploader" : subidor (fallback generico)
        //   "track"    : titulo limpio en videos musicales con metadata
        std::string metaTitle;
        std::string metaArtist;
        if (!foundInfoJson.empty()) {
            auto raw = geode::utils::file::readString(foundInfoJson).unwrapOr("");
            if (!raw.empty()) {
                auto parsed = matjson::parse(raw);
                if (parsed.isOk()) {
                    auto root = parsed.unwrap();
                    // Helper: lee un campo string, tolera que no exista o
                    // que sea null.
                    auto str = [&](const char* key) -> std::string {
                        if (!root.contains(key)) return "";
                        auto v = root[key];
                        if (!v.isString()) return "";
                        return v.asString().unwrapOr("");
                    };
                    // Titulo: "track" es mas limpio si existe; si no, "title".
                    metaTitle = str("track");
                    if (metaTitle.empty()) metaTitle = str("title");
                    // Artista: probar varios campos en orden.
                    metaArtist = str("artist");
                    if (metaArtist.empty()) metaArtist = str("creator");
                    if (metaArtist.empty()) metaArtist = str("channel");
                    if (metaArtist.empty()) metaArtist = str("uploader");
                }
            }
            // Limpieza: el info.json no es algo que el usuario quiera ver
            // en su carpeta. Lo eliminamos tras parsearlo.
            std::error_code rm;
            std::filesystem::remove(foundInfoJson, rm);
        }

        if (paimon::isRuntimeShuttingDown()) return;

        // Sanitizacion del nombre de archivo. Windows no permite
        //   < > : " / \ | ? *   ni NUL/espacios al final de segmentos.
        // Mac/Linux son mas tolerantes pero es mejor mantenerlo
        // cross-platform. Tambien colapsamos whitespace consecutivo.
        auto sanitizeFsName = [](std::string in) {
            static const std::string banned = "<>:\"/\\|?*";
            for (auto& c : in) {
                unsigned char uc = static_cast<unsigned char>(c);
                if (uc < 32) c = ' ';
                else if (banned.find(c) != std::string::npos) c = '_';
            }
            // Colapsar espacios consecutivos.
            std::string out;
            out.reserve(in.size());
            bool prevSpace = false;
            for (char c : in) {
                if (c == ' ' || c == '\t') {
                     if (!prevSpace) out.push_back(' ');
                     prevSpace = true;
                } else {
                     out.push_back(c);
                     prevSpace = false;
                }
            }
            // Trim.
            while (!out.empty() && (out.front() == ' ' || out.front() == '.')) {
                out.erase(out.begin());
            }
            while (!out.empty() && (out.back() == ' ' || out.back() == '.')) {
                out.pop_back();
            }
            // Cap: 120 chars es suficiente y evita filename-too-long en
            // Windows con rutas profundas.
            if (out.size() > 120) out.resize(120);
            return out;
        };

        // Convierte un std::string UTF-8 a std::filesystem::path preservando
        // correctamente los caracteres no ASCII. En Windows la conversion
        // plain `path(std::string)` usa la codepage local y pierde acentos,
        // asi que ruteamos via wstring. En Unix el path ya es UTF-8 asi
        // que la conversion es directa.
        auto utf8ToPath = [](const std::string& s) -> std::filesystem::path {
#ifdef GEODE_IS_WINDOWS
            return std::filesystem::path(geode::utils::string::utf8ToWide(s));
#else
            return std::filesystem::path(s);
#endif
        };

        // Si tenemos metadata, renombramos el audio a "Title - Artist.ext"
        // para que el archivo quede identificable en disco. La extension
        // del cover se renombra al mismo stem para que emparejen.
        if (!foundAudio.empty() && !metaTitle.empty()) {
            std::string niceStem = sanitizeFsName(metaTitle);
            if (!metaArtist.empty()) {
                std::string niceArtist = sanitizeFsName(metaArtist);
                if (!niceArtist.empty()) {
                    niceStem = fmt::format("{} - {}", niceStem, niceArtist);
                }
            }
            niceStem = sanitizeFsName(niceStem);

            if (!niceStem.empty()) {
                // Construimos el nuevo path via utf8ToPath para preservar
                // caracteres no-ASCII en Windows (ANSI codepage no los
                // soporta y se perderian en el round-trip std::string).
                const auto niceStemFs = utf8ToPath(niceStem);
                const auto extFs = foundAudio.extension();
                auto newAudio = foundAudio.parent_path() / (niceStemFs.native() + extFs.native());

                // Evitar colisiones: si ya existe un archivo con ese nombre
                // (descarga previa duplicada), le agregamos un sufijo corto.
                std::error_code existsEc;
                if (std::filesystem::exists(newAudio, existsEc)) {
                    const std::string altStem = niceStem + "_" + trackId;
                    const auto altStemFs = utf8ToPath(altStem);
                    newAudio = foundAudio.parent_path() / (altStemFs.native() + extFs.native());
                }
                std::error_code mv;
                std::filesystem::rename(foundAudio, newAudio, mv);
                if (!mv) {
                    log::info("[yt-dlp] renamed audio to '{}'",
                        geode::utils::string::pathToString(newAudio.filename()));

                    // Renombrar el cover al mismo stem para emparejar.
                    if (!foundCover.empty()) {
                        const auto coverExtFs = foundCover.extension();
                        auto newCover = foundCover.parent_path() /
                            (niceStemFs.native() + coverExtFs.native());
                        std::error_code mv2;
                        std::filesystem::rename(foundCover, newCover, mv2);
                        if (!mv2) {
                            foundCover = newCover;
                        }
                    }

                    foundAudio = newAudio;
                } else {
                    log::warn("[yt-dlp] rename failed ({}), keeping original name",
                        mv.message());
                }
            }
        }

        if (paimon::isRuntimeShuttingDown()) return;

        // Mover el thumbnail al covers dir con nombre consistente
        // (<niceStem o trackId>.<ext>). Si hubo renombrado, usamos el
        // nuevo stem; si no, caemos al trackId para evitar colisiones
        // entre descargas distintas.
        if (!foundCover.empty()) {
            std::error_code mv;
            std::filesystem::create_directories(coversDir, mv);
            std::string coverStem = geode::utils::string::pathToString(foundAudio.stem());
            if (coverStem.empty()) coverStem = trackId;
            const auto coverStemFs = utf8ToPath(coverStem);
            const auto coverExtFs = utf8ToPath(foundCoverExt);
            auto finalCover = coversDir / (coverStemFs.native() + coverExtFs.native());
            std::error_code existsEc;
            if (std::filesystem::exists(finalCover, existsEc)) {
                const std::string altStem = coverStem + "_" + trackId;
                const auto altStemFs = utf8ToPath(altStem);
                finalCover = coversDir / (altStemFs.native() + coverExtFs.native());
            }
            std::filesystem::rename(foundCover, finalCover, mv);
            if (!mv) {
                foundCover = finalCover;
            }
        }

        // Limpiar archivos intermedios que hayan sobrevivido a ffmpeg.
        // yt-dlp normalmente los borra, pero si -x falla (p.ej. ffmpeg
        // crashea) puede quedar un .m4a o .webm huerfano.
        if (!foundIntermediate.empty()) {
            std::error_code rm;
            std::filesystem::remove(foundIntermediate, rm);
            log::debug("[yt-dlp] cleaned up intermediate file: {}",
                geode::utils::string::pathToString(foundIntermediate));
        }

        // Decision final:
        //   * Si el archivo de audio existe, EXITO (incluso si exitCode != 0;
        //     yt-dlp a veces devuelve codigo no-cero por warnings menores
        //     tras terminar la conversion correctamente).
        //   * Si no hay audio pero yt-dlp termino sin errores, es que
        //     ffmpeg fallo la conversion / el remux. Reportamos eso al usuario.
        std::error_code finalEc;
        const bool audioExists = !foundAudio.empty() &&
            std::filesystem::exists(foundAudio, finalEc);
        if (audioExists) {
            result.success = true;
            // Convertimos a string UTF-8 al final (justo antes de entregar
            // el resultado al caller). Hasta aqui mantuvimos el path como
            // std::filesystem::path para evitar perder caracteres no-ASCII
            // en el round-trip por ANSI codepage en Windows.
            result.audioPath = geode::utils::string::pathToString(foundAudio);
            result.coverPath = foundCover.empty()
                ? std::string{}
                : geode::utils::string::pathToString(foundCover);
            // Preferimos metadata parseada del info.json. Si no hay,
            // caemos al stem del archivo (que ya puede ser "Title - Artist"
            // si el rename tuvo exito arriba).
            result.displayName = !metaTitle.empty()
                ? metaTitle
                : geode::utils::string::pathToString(foundAudio.stem());
            result.artist = metaArtist;
        } else {
            result.success = false;
            // Preferimos una linea que parezca error real sobre la ultima
            // linea cualquiera (que a veces es una meta-linea nuestra).
            std::string msg = lastErrorLine.empty() ? lastMeaningfulLine : lastErrorLine;
            if (msg.empty()) {
                msg = fmt::format(
                    "yt-dlp finished (exit {}) but no {} file was produced. "
                    "The conversion to {} (via ffmpeg) may have failed. "
                    "Check the Geode debug log for full output.",
                    exitCode, expectedExt, formatChoice);
            } else if (exitCode != 0) {
                msg = fmt::format("[exit {}] {}", exitCode, msg);
            }
            result.error = msg;
            log::warn("[yt-dlp] download failed. exit={}, last='{}', err='{}'",
                exitCode, lastMeaningfulLine, lastErrorLine);
        }

        m_activeJobs.fetch_sub(1, std::memory_order_relaxed);

        if (paimon::isRuntimeShuttingDown()) return;

        Loader::get()->queueInMainThread([onComplete, result = std::move(result)]() mutable {
            if (paimon::isRuntimeShuttingDown()) return;
            if (onComplete) onComplete(std::move(result));
        });
    });
}

} // namespace paimon::menumusic
