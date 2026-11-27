#include "../../../utils/FilesystemPath.hpp"
#include "OpenVectorRunner.hpp"
#include "OpenVectorBootstrap.hpp"

#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/ImageConverter.hpp"

#include <Geode/loader/Log.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/file.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fmt/format.h>
#include <fstream>
#include <limits>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>

#ifdef GEODE_IS_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace geode::prelude;

namespace paimon::gifimport {

namespace {

constexpr int kVectorizeTimeoutMs = 90'000;

#ifdef GEODE_IS_WINDOWS
std::string winQuoteArg(std::string const& arg) {
    if (arg.empty()) return "\"\"";
    bool needsQuotes = false;
    for (char c : arg) {
        if (c == ' ' || c == '\t' || c == '"' || c == '\n' || c == '\v') {
            needsQuotes = true;
            break;
        }
    }
    if (!needsQuotes) return arg;
    std::string out = "\"";
    std::size_t backslashes = 0;
    for (char c : arg) {
        if (c == '\\') {
            ++backslashes;
        } else if (c == '"') {
            out.append(backslashes * 2 + 1, '\\');
            out.push_back('"');
            backslashes = 0;
        } else {
            out.append(backslashes, '\\');
            out.push_back(c);
            backslashes = 0;
        }
    }
    out.append(backslashes * 2, '\\');
    out.push_back('"');
    return out;
}

int runSubprocess(
    std::vector<std::string> const& argv,
    std::string& stderrOut,
    int timeoutMs,
    std::atomic<bool> const* cancelled
) {
    if (argv.empty()) return -1;
    std::string cmdLine;
    for (std::size_t i = 0; i < argv.size(); ++i) {
        if (i) cmdLine.push_back(' ');
        cmdLine += winQuoteArg(argv[i]);
    }
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, cmdLine.c_str(), -1, nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(wideLen), 0);
    MultiByteToWideChar(CP_UTF8, 0, cmdLine.c_str(), -1, wide.data(), wideLen);

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
    BOOL ok = CreateProcessW(
        nullptr, wide.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(writePipe);
    if (!ok) {
        CloseHandle(readPipe);
        return -1;
    }

    auto startTime = std::chrono::steady_clock::now();
    char chunk[1024];
    DWORD readN = 0;
    bool timedOut = false;
    bool aborted = false;

    while (true) {
        DWORD avail = 0;
        if (!PeekNamedPipe(readPipe, nullptr, 0, nullptr, &avail, nullptr)) break;
        if (avail > 0) {
            DWORD toRead = std::min<DWORD>(avail, sizeof(chunk));
            if (!ReadFile(readPipe, chunk, toRead, &readN, nullptr) || readN == 0) break;
            if (stderrOut.size() < 4096) stderrOut.append(chunk, readN);
        } else {
            DWORD waitRes = WaitForSingleObject(pi.hProcess, 25);
            if (waitRes == WAIT_OBJECT_0) {
                DWORD finalAvail = 0;
                if (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &finalAvail, nullptr) && finalAvail > 0) {
                    continue;
                }
                break;
            }
        }
        if ((cancelled && cancelled->load(std::memory_order_relaxed)) ||
            paimon::isRuntimeShuttingDown()) {
            TerminateProcess(pi.hProcess, 1);
            aborted = true;
            break;
        }
        if (timeoutMs > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startTime).count();
            if (elapsed > timeoutMs) {
                TerminateProcess(pi.hProcess, 1);
                timedOut = true;
                break;
            }
        }
    }

    CloseHandle(readPipe);
    WaitForSingleObject(pi.hProcess, 3000);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (aborted) return -3;
    return timedOut ? -2 : static_cast<int>(exitCode);
}
#else
int runSubprocess(
    std::vector<std::string> const& argv,
    std::string& stderrOut,
    int timeoutMs,
    std::atomic<bool> const* cancelled
) {
    if (argv.empty()) return -1;
    std::vector<char*> cargv;
    cargv.reserve(argv.size() + 1);
    for (auto const& a : argv) cargv.push_back(const_cast<char*>(a.c_str()));
    cargv.push_back(nullptr);

    int pipefd[2];
    if (pipe(pipefd) != 0) return -1;
    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execvp(cargv[0], cargv.data());
        _exit(127);
    }
    close(pipefd[1]);
    fcntl(pipefd[0], F_SETFL, fcntl(pipefd[0], F_GETFL, 0) | O_NONBLOCK);

    auto startTime = std::chrono::steady_clock::now();
    char chunk[1024];
    int status = 0;
    bool finished = false;
    bool timedOut = false;
    bool aborted = false;

    while (!finished) {
        ssize_t n = read(pipefd[0], chunk, sizeof(chunk));
        if (n > 0) {
            if (stderrOut.size() < 4096) stderrOut.append(chunk, static_cast<std::size_t>(n));
            continue;
        }
        pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid) {
            finished = true;
            break;
        }
        if ((cancelled && cancelled->load(std::memory_order_relaxed)) ||
            paimon::isRuntimeShuttingDown()) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            aborted = true;
            break;
        }
        if (timeoutMs > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startTime).count();
            if (elapsed > timeoutMs) {
                kill(pid, SIGKILL);
                waitpid(pid, &status, 0);
                timedOut = true;
                break;
            }
        }
        usleep(50'000);
    }
    while (true) {
        ssize_t n = read(pipefd[0], chunk, sizeof(chunk));
        if (n <= 0) break;
        if (stderrOut.size() < 4096) stderrOut.append(chunk, static_cast<std::size_t>(n));
    }
    close(pipefd[0]);
    if (aborted) return -3;
    if (timedOut) return -2;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
#endif

} // namespace

OpenVectorResult OpenVectorRunner::vectorizeRgbaToSvg(
    std::uint8_t const* rgba,
    int width,
    int height,
    OpenVectorQuality quality,
    std::atomic<bool> const* cancelled
) {
    OpenVectorResult res;
    if (!rgba || width <= 0 || height <= 0) {
        res.error = "Imagen invalida para vectorizar.";
        return res;
    }
    auto binary = OpenVectorBootstrap::get().locateBinary();
    if (binary.empty()) {
        res.error = "El motor complementario OpenVector no esta instalado.";
        return res;
    }

    static std::atomic<std::uint64_t> seq{0};
    auto const stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto const jobDir = paimon::paths::toStdPath(Mod::get()->getSaveDir()) / "openvector" /
        "tmp" / fmt::format("job-{}-{}", stamp, seq.fetch_add(1));

    struct DirGuard {
        std::filesystem::path dir;
        ~DirGuard() {
            std::error_code ec;
            std::filesystem::remove_all(dir, ec);
        }
    } guard{jobDir};

    std::error_code ec;
    std::filesystem::create_directories(jobDir, ec);
    if (ec) {
        res.error = "No se pudo crear carpeta temporal para OpenVector.";
        return res;
    }

    auto const inPng = jobDir / "input.png";
    auto const recentJson = jobDir / "recent.json";
    auto const outSvg = jobDir / "output.svg";

    if (!ImageConverter::saveRGBAToPNG(
            rgba, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), inPng)) {
        res.error = "No se pudo preparar la imagen temporal para OpenVector.";
        return res;
    }

    int const qVal = std::clamp(static_cast<int>(quality), 0, 2);
    // Replay explicit basic settings with category=0 (continuous-tone) so OpenVector skips
    // automatic classification (whose category=1 logo classifier can stall >90s on detailed
    // illustrations) and runs fitting only once instead of twice.
    std::string const recent = fmt::format(
        "{{\"recentBasicSettings\":[{{\"basicColorCount\":0,\"colorCount\":2,"
        "\"paletteColorCount\":0,\"recommendedBackgroundMode\":-1,"
        "\"settings\":{{\"advanced\":false,\"antiAlias\":true,\"artifactRejection\":0,"
        "\"background\":[0.0,0.0,0.0,0.0],\"backgroundMode\":-1,\"category\":0,"
        "\"clusterColors\":true,\"complexity\":5,\"curveComplexity\":6,"
        "\"detectCorners\":true,\"minimumPixels\":0,\"palette\":[],"
        "\"paletteMode\":2,\"quality\":{},\"sensitivity\":1,"
        "\"smoothingAntiAlias\":true,\"smoothness\":6}}}}]}}",
        qVal);
    auto writeRecent = geode::utils::file::writeString(paimon::paths::toSdkPath(recentJson), recent);
    if (!writeRecent) {
        res.error = "No se pudo escribir el perfil de OpenVector.";
        return res;
    }

    std::vector<std::string> argv{
        paimon::paths::toUtf8(binary),
        "vectorize",
        paimon::paths::toUtf8(inPng),
        "--shrink-source",
        "--layout", "stacked",
        "--recent-file", paimon::paths::toUtf8(recentJson),
        "--recent-index", "0",
        "-o", paimon::paths::toUtf8(outSvg),
        "-f"
    };

    std::string procLog;
    int const code = runSubprocess(argv, procLog, kVectorizeTimeoutMs, cancelled);
    if (code == -3) {
        res.error = "Vectorizacion cancelada.";
        return res;
    }
    if (code == -2) {
        res.error = "OpenVector tardo demasiado en vectorizar la imagen.";
        return res;
    }
    if (code != 0) {
        log::warn("[OpenVector] exit {} log: {}", code, procLog);
        res.error = fmt::format("OpenVector fallo al vectorizar (codigo {}).", code);
        return res;
    }

    std::ifstream svgFile(outSvg, std::ios::binary);
    if (!svgFile) {
        res.error = "OpenVector no genero ningun archivo SVG.";
        return res;
    }
    std::ostringstream ss;
    ss << svgFile.rdbuf();
    res.svgContent = ss.str();
    if (res.svgContent.empty()) {
        res.error = "El SVG generado por OpenVector esta vacio.";
    }
    return res;
}

OpenVectorFramesResult OpenVectorRunner::vectorizeFramesToSvg(
    SourceAnimation const& source,
    int maxFrames,
    OpenVectorQuality quality,
    OpenVectorProgressCallback progress,
    std::atomic<bool> const* cancelled
) {
    OpenVectorFramesResult out;
    if (source.width <= 0 || source.height <= 0 || source.frames.empty()) {
        out.error = "No hay frames validos para vectorizar.";
        return out;
    }
    auto binary = OpenVectorBootstrap::get().locateBinary();
    if (binary.empty()) {
        out.error = "El motor complementario OpenVector no esta instalado.";
        return out;
    }

    int const totalSource = static_cast<int>(source.frames.size());
    out.sourceFrames = totalSource;
    int const limit = std::clamp(maxFrames, 1, totalSource);

    struct Selected {
        SourceFrame const* frame = nullptr;
        int delayMs = 100;
    };
    std::vector<Selected> selected;
    selected.reserve(static_cast<std::size_t>(limit));
    if (totalSource <= limit) {
        for (auto const& frame : source.frames) {
            selected.push_back({&frame, std::max(frame.delayMs, 10)});
        }
    } else {
        for (int i = 0; i < limit; ++i) {
            int const begin = i * totalSource / limit;
            int const end = (i + 1) * totalSource / limit;
            int const chosen = begin + (end - begin - 1) / 2;
            long long delay = 0;
            for (int j = begin; j < end; ++j) {
                delay += std::max(source.frames[static_cast<std::size_t>(j)].delayMs, 10);
            }
            selected.push_back({
                &source.frames[static_cast<std::size_t>(chosen)],
                static_cast<int>(std::min<long long>(delay, std::numeric_limits<int>::max()))
            });
        }
    }

    std::size_t const expectedBytes =
        static_cast<std::size_t>(source.width) * static_cast<std::size_t>(source.height) * 4;
    for (auto const& item : selected) {
        if (!item.frame || item.frame->rgba.size() < expectedBytes) {
            out.error = "Uno de los frames esta incompleto.";
            return out;
        }
    }

    // Fast path for a single still image.
    if (selected.size() == 1) {
        if (progress) progress(0, 1);
        auto single = vectorizeRgbaToSvg(
            selected.front().frame->rgba.data(),
            source.width,
            source.height,
            quality,
            cancelled);
        if (!single) {
            out.error = std::move(single.error);
            return out;
        }
        out.frames.push_back({selected.front().delayMs, std::move(single.svgContent)});
        if (progress) progress(1, 1);
        return out;
    }

    // Deduplicate identical frames before spawning OpenVector processes.
    auto hashRgba = [expectedBytes](std::vector<std::uint8_t> const& bytes) -> std::uint64_t {
        std::uint64_t h = 14695981039346656037ULL;
        std::size_t const step = expectedBytes > 65536 ? (expectedBytes / 16384) : 1;
        for (std::size_t i = 0; i < expectedBytes; i += step) {
            h ^= bytes[i];
            h *= 1099511628211ULL;
        }
        return h;
    };

    std::vector<SourceFrame const*> uniqueFrames;
    std::vector<int> uniqueRefCount;
    std::vector<std::size_t> selectedToUnique(selected.size(), 0);
    std::unordered_map<std::uint64_t, std::vector<std::size_t>> hashBuckets;

    for (std::size_t i = 0; i < selected.size(); ++i) {
        auto const* frame = selected[i].frame;
        std::uint64_t const h = hashRgba(frame->rgba);
        bool found = false;
        auto& bucket = hashBuckets[h];
        for (std::size_t u : bucket) {
            if (std::memcmp(uniqueFrames[u]->rgba.data(), frame->rgba.data(), expectedBytes) == 0) {
                selectedToUnique[i] = u;
                ++uniqueRefCount[u];
                found = true;
                break;
            }
        }
        if (!found) {
            std::size_t const u = uniqueFrames.size();
            uniqueFrames.push_back(frame);
            uniqueRefCount.push_back(1);
            bucket.push_back(u);
            selectedToUnique[i] = u;
        }
    }

    int const totalSelected = static_cast<int>(selected.size());
    if (progress) progress(0, totalSelected);

    // Distribute unique frames across a bounded pool of parallel OpenVector workers.
    unsigned int const hw = std::max(1u, std::thread::hardware_concurrency());
    unsigned int const maxWorkers = std::clamp(hw / 2, 2u, 6u);
    unsigned int const workers = static_cast<unsigned int>(
        std::min<std::size_t>(uniqueFrames.size(), maxWorkers));

    std::vector<std::string> uniqueSvgs(uniqueFrames.size());
    std::atomic<std::size_t> nextUnique{0};
    std::atomic<int> doneSelected{0};
    std::atomic<bool> failed{false};
    std::mutex errorMutex;
    std::mutex progressMutex;
    std::string firstError;

    auto workerFn = [&] {
        while (!failed.load(std::memory_order_relaxed)) {
            if ((cancelled && cancelled->load(std::memory_order_relaxed)) ||
                paimon::isRuntimeShuttingDown()) {
                std::lock_guard lock(errorMutex);
                if (firstError.empty()) firstError = "Vectorizacion cancelada.";
                failed.store(true, std::memory_order_relaxed);
                break;
            }
            std::size_t const u = nextUnique.fetch_add(1, std::memory_order_relaxed);
            if (u >= uniqueFrames.size()) break;

            auto res = vectorizeRgbaToSvg(
                uniqueFrames[u]->rgba.data(),
                source.width,
                source.height,
                quality,
                cancelled);
            if (!res) {
                std::lock_guard lock(errorMutex);
                if (firstError.empty()) firstError = std::move(res.error);
                failed.store(true, std::memory_order_relaxed);
                break;
            }
            uniqueSvgs[u] = std::move(res.svgContent);
            int const completed = doneSelected.fetch_add(
                uniqueRefCount[u], std::memory_order_relaxed) + uniqueRefCount[u];
            if (progress) {
                std::lock_guard lock(progressMutex);
                progress(std::min(completed, totalSelected), totalSelected);
            }
        }
    };

    if (workers <= 1) {
        workerFn();
    } else {
        std::vector<std::thread> pool;
        pool.reserve(workers - 1);
        for (unsigned int w = 1; w < workers; ++w) {
            pool.emplace_back(workerFn);
        }
        workerFn();
        for (auto& t : pool) t.join();
    }

    if (failed.load(std::memory_order_relaxed)) {
        out.error = firstError.empty() ? "Error al vectorizar frames con OpenVector." : std::move(firstError);
        return out;
    }

    out.frames.reserve(selected.size());
    for (std::size_t i = 0; i < selected.size(); ++i) {
        out.frames.push_back({selected[i].delayMs, uniqueSvgs[selectedToUnique[i]]});
    }
    return out;
}

} // namespace paimon::gifimport
