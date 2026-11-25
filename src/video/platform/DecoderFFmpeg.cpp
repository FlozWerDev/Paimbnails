#include "DecoderFFmpeg.hpp"

#if defined(_WIN32)

#include "../../core/RuntimeLifecycle.hpp"
#include "../../core/Settings.hpp"
#include "../../features/menu-music/services/FfmpegBootstrap.hpp"
#include "../../utils/JoinWithWarning.hpp"

#include <Geode/loader/Loader.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/utils/string.hpp>

#include <Windows.h>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string_view>
#include <vector>

namespace paimon {

namespace {

std::wstring quoteWinArg(std::wstring_view arg) {
    std::wstring out;
    out.push_back(L'"');
    size_t backslashes = 0;
    for (wchar_t ch : arg) {
        if (ch == L'\\') {
            ++backslashes;
        } else if (ch == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'"');
            backslashes = 0;
        } else {
            if (backslashes > 0) {
                out.append(backslashes, L'\\');
                backslashes = 0;
            }
            out.push_back(ch);
        }
    }
    if (backslashes > 0) out.append(backslashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

std::filesystem::path resolveFfmpegExecutable() {
    std::error_code ec;
    auto& boot = paimon::menumusic::FfmpegBootstrap::get();
    auto bundled = boot.bundledPath();
    if (!bundled.empty() && std::filesystem::is_regular_file(bundled, ec)) {
        return bundled;
    }
    wchar_t found[MAX_PATH] = {};
    if (SearchPathW(nullptr, L"ffmpeg.exe", nullptr, MAX_PATH, found, nullptr) > 0) {
        return std::filesystem::path(found);
    }
    return {};
}

bool parseDoubleView(std::string_view sv, double& out) {
    if (sv.empty()) return false;
    auto const* begin = sv.data();
    auto const* end = sv.data() + sv.size();
    auto res = std::from_chars(begin, end, out);
    return res.ec == std::errc{} && std::isfinite(out);
}

bool parseIntView(std::string_view sv, int& out) {
    if (sv.empty()) return false;
    auto const* begin = sv.data();
    auto const* end = sv.data() + sv.size();
    auto res = std::from_chars(begin, end, out);
    return res.ec == std::errc{};
}

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

struct ProbeInfo {
    int width = 0;
    int height = 0;
    double fps = 30.0;
    double duration = 0.0;
    VideoColorMatrix colorMatrix = VideoColorMatrix::Auto;
    bool fullRange = false;
    int rotation = 0;
};

void parseProbeOutput(std::string_view text, ProbeInfo& info) {
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        std::string_view line = text.substr(pos, nl - pos);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        pos = nl + 1;

        if (auto d = line.find("Duration:"); d != std::string_view::npos) {
            std::string_view rest = line.substr(d + 9);
            while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
            // HH:MM:SS.cc
            if (rest.size() >= 8 && rest[2] == ':' && rest[5] == ':') {
                int hh = 0, mm = 0;
                double ss = 0.0;
                size_t comma = rest.find(',');
                std::string_view secPart = rest.substr(6, comma == std::string_view::npos ? std::string_view::npos : comma - 6);
                if (parseIntView(rest.substr(0, 2), hh) &&
                    parseIntView(rest.substr(3, 2), mm) &&
                    parseDoubleView(secPart, ss)) {
                    info.duration = std::max(0.0, hh * 3600.0 + mm * 60.0 + ss);
                }
            }
        }

        if (line.find("Video:") != std::string_view::npos) {
            // Color range / matrix hints inside video stream description.
            if (line.find("bt709") != std::string_view::npos) {
                info.colorMatrix = VideoColorMatrix::BT709;
            } else if (line.find("bt470bg") != std::string_view::npos ||
                       line.find("smpte170m") != std::string_view::npos) {
                info.colorMatrix = VideoColorMatrix::BT601;
            }
            if (line.find("pc,") != std::string_view::npos ||
                line.find("yuvj") != std::string_view::npos) {
                info.fullRange = true;
            }

            // Dimensions: token matching ", WxH" or " WxH"
            for (size_t i = 0; i + 2 < line.size(); ++i) {
                if (!isDigit(line[i])) continue;
                bool const boundaryBefore = (i == 0 || line[i - 1] == ' ' || line[i - 1] == ',');
                if (!boundaryBefore) continue;
                size_t xPos = i;
                while (xPos < line.size() && isDigit(line[xPos])) ++xPos;
                if (xPos == i || xPos >= line.size() || line[xPos] != 'x') continue;
                size_t hEnd = xPos + 1;
                while (hEnd < line.size() && isDigit(line[hEnd])) ++hEnd;
                if (hEnd == xPos + 1) continue;
                bool const boundaryAfter = (hEnd == line.size() || line[hEnd] == ' ' || line[hEnd] == ',' || line[hEnd] == '[');
                if (!boundaryAfter) continue;
                int w = 0, h = 0;
                if (parseIntView(line.substr(i, xPos - i), w) &&
                    parseIntView(line.substr(xPos + 1, hEnd - (xPos + 1)), h) &&
                    w >= 16 && w <= 8192 && h >= 16 && h <= 8192) {
                    info.width = w;
                    info.height = h;
                    break;
                }
            }

            // Frame rate: "<num> fps" or "<num> tbr"
            for (char const* tag : {" fps", " tbr"}) {
                auto fPos = line.find(tag);
                if (fPos != std::string_view::npos && fPos > 0) {
                    size_t start = fPos;
                    while (start > 0 && (isDigit(line[start - 1]) || line[start - 1] == '.')) {
                        --start;
                    }
                    double f = 0.0;
                    if (parseDoubleView(line.substr(start, fPos - start), f) && f >= 1.0 && f <= 240.0) {
                        info.fps = f;
                        break;
                    }
                }
            }
        }

        if (auto r = line.find("rotate"); r != std::string_view::npos) {
            if (auto colon = line.find(':', r); colon != std::string_view::npos) {
                std::string_view val = line.substr(colon + 1);
                while (!val.empty() && val.front() == ' ') val.remove_prefix(1);
                int deg = 0;
                if (parseIntView(val, deg)) {
                    deg = ((deg % 360) + 360) % 360;
                    if (deg == 90 || deg == 180 || deg == 270) info.rotation = deg;
                }
            }
        }
    }
}

bool probeVideoWithFfmpeg(
    std::filesystem::path const& ffmpegPath,
    std::string const& videoPath,
    ProbeInfo& outInfo
) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr, writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 65536)) return false;
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.hStdInput = nullptr;

    std::wstring cmd = quoteWinArg(ffmpegPath.native()) +
        L" -hide_banner -nostdin -i " +
        quoteWinArg(geode::utils::string::utf8ToWide(videoPath));

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(
        nullptr,
        cmd.data(),
        nullptr, nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr, nullptr,
        &si, &pi
    );
    CloseHandle(writePipe);
    if (!ok) {
        CloseHandle(readPipe);
        return false;
    }

    std::string output;
    char buf[4096];
    DWORD readN = 0;
    auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (std::chrono::steady_clock::now() < deadline) {
        DWORD avail = 0;
        if (!PeekNamedPipe(readPipe, nullptr, 0, nullptr, &avail, nullptr)) break;
        if (avail > 0) {
            DWORD want = std::min<DWORD>(avail, sizeof(buf));
            if (!ReadFile(readPipe, buf, want, &readN, nullptr) || readN == 0) break;
            output.append(buf, readN);
            if (output.size() > 65536) break;
        } else {
            if (WaitForSingleObject(pi.hProcess, 15) == WAIT_OBJECT_0) {
                while (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
                    DWORD want = std::min<DWORD>(avail, sizeof(buf));
                    if (!ReadFile(readPipe, buf, want, &readN, nullptr) || readN == 0) break;
                    output.append(buf, readN);
                }
                break;
            }
        }
    }

    if (WaitForSingleObject(pi.hProcess, 0) != WAIT_OBJECT_0) {
        TerminateProcess(pi.hProcess, 0);
        WaitForSingleObject(pi.hProcess, 500);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(readPipe);

    parseProbeOutput(output, outInfo);
    return outInfo.width >= 16 && outInfo.height >= 16;
}

} // namespace

struct DecoderFFmpeg::State {
    std::filesystem::path ffmpegExe;
    std::string videoPath;
    int width = 0;
    int height = 0;
    int outWidth = 0;
    int outHeight = 0;
    double fps = 30.0;
    double duration = 0.0;
    double seekSeconds = 0.0;
    VideoColorMatrix colorMatrix = VideoColorMatrix::Auto;
    bool fullRange = false;
    int rotation = 0;

    VideoRingBuffer ring;
    std::atomic<bool> decoding{false};
    std::atomic<bool> finished{false};
    std::atomic<bool> threadDone{true};

    std::mutex procMutex;
    HANDLE pipeRead = nullptr;
    HANDLE procHandle = nullptr;

    ~State() {
        terminateProcess();
    }

    void terminateProcess() {
        std::lock_guard lk(procMutex);
        if (procHandle) {
            TerminateProcess(procHandle, 0);
            WaitForSingleObject(procHandle, 300);
            CloseHandle(procHandle);
            procHandle = nullptr;
        }
        if (pipeRead) {
            CloseHandle(pipeRead);
            pipeRead = nullptr;
        }
    }

    bool spawnDecodeProcess(HANDLE& outRead, HANDLE& outProc) {
        outRead = nullptr;
        outProc = nullptr;

        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;

        HANDLE rPipe = nullptr, wPipe = nullptr;
        if (!CreatePipe(&rPipe, &wPipe, &sa, 512 * 1024)) return false;
        SetHandleInformation(rPipe, HANDLE_FLAG_INHERIT, 0);

        HANDLE nulErr = CreateFileW(
            L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
            &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        si.hStdOutput = wPipe;
        si.hStdError = nulErr ? nulErr : wPipe;
        si.hStdInput = nullptr;

        std::wstring cmd = quoteWinArg(ffmpegExe.native()) +
            L" -hide_banner -loglevel quiet -nostdin";
        if (seekSeconds > 0.001) {
            char ssBuf[32] = {};
            std::snprintf(ssBuf, sizeof(ssBuf), "%.3f", seekSeconds);
            cmd += L" -ss ";
            cmd += geode::utils::string::utf8ToWide(ssBuf);
        }
        cmd += L" -i " + quoteWinArg(geode::utils::string::utf8ToWide(videoPath));
        cmd += L" -an -sn -dn";
        if (outWidth != width || outHeight != height) {
            char scaleBuf[64] = {};
            std::snprintf(scaleBuf, sizeof(scaleBuf), " -vf scale=%d:%d:flags=bilinear", outWidth, outHeight);
            cmd += geode::utils::string::utf8ToWide(scaleBuf);
        }
        cmd += L" -f rawvideo -pix_fmt yuv420p -";

        PROCESS_INFORMATION pi{};
        BOOL ok = CreateProcessW(
            nullptr,
            cmd.data(),
            nullptr, nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            nullptr, nullptr,
            &si, &pi
        );
        CloseHandle(wPipe);
        if (nulErr) CloseHandle(nulErr);
        if (!ok) {
            CloseHandle(rPipe);
            return false;
        }
        CloseHandle(pi.hThread);
        outRead = rPipe;
        outProc = pi.hProcess;
        return true;
    }

    void decodeLoop() {
        struct DoneGuard {
            std::atomic<bool>& flag;
            ~DoneGuard() { flag.store(true, std::memory_order_release); }
        } doneGuard{threadDone};

        HANDLE rPipe = nullptr;
        HANDLE hProc = nullptr;
        if (!spawnDecodeProcess(rPipe, hProc)) {
            finished.store(true, std::memory_order_release);
            return;
        }
        {
            std::lock_guard lk(procMutex);
            pipeRead = rPipe;
            procHandle = hProc;
        }

        int const uvW = (outWidth + 1) / 2;
        int const uvH = (outHeight + 1) / 2;
        size_t const yBytes = static_cast<size_t>(outWidth) * static_cast<size_t>(outHeight);
        size_t const uvBytes = static_cast<size_t>(uvW) * static_cast<size_t>(uvH);
        size_t const frameBytes = yBytes + 2 * uvBytes;

        std::vector<uint8_t> frameBuf(frameBytes);
        uint64_t frameIndex = 0;

        while (decoding.load(std::memory_order_relaxed) && !paimon::isRuntimeShuttingDown()) {
            // Read exact frameBytes from stdout pipe cleanly.
            size_t totalRead = 0;
            bool eof = false;
            while (totalRead < frameBytes) {
                if (!decoding.load(std::memory_order_relaxed) || paimon::isRuntimeShuttingDown()) {
                    eof = true;
                    break;
                }
                DWORD want = static_cast<DWORD>(std::min<size_t>(frameBytes - totalRead, 65536));
                DWORD got = 0;
                if (!ReadFile(rPipe, frameBuf.data() + totalRead, want, &got, nullptr) || got == 0) {
                    eof = true;
                    break;
                }
                totalRead += static_cast<size_t>(got);
            }
            if (eof || totalRead < frameBytes) {
                if (decoding.load(std::memory_order_relaxed)) {
                    finished.store(true, std::memory_order_release);
                }
                break;
            }

            while (ring.isFull()) {
                if (!decoding.load(std::memory_order_relaxed) || paimon::isRuntimeShuttingDown()) {
                    terminateProcess();
                    return;
                }
                ring.waitForWritable(40, &decoding);
            }

            VideoFrame* slot = ring.nextWrite();
            if (!slot) continue;

            uint8_t const* srcY = frameBuf.data();
            uint8_t const* srcCb = srcY + yBytes;
            uint8_t const* srcCr = srcCb + uvBytes;

            copyStridedRows(slot->planeY,  slot->strideY,  srcY,  outWidth, outWidth, outHeight);
            copyStridedRows(slot->planeCb, slot->strideCb, srcCb, uvW,      uvW,      uvH);
            copyStridedRows(slot->planeCr, slot->strideCr, srcCr, uvW,      uvW,      uvH);

            slot->pts = seekSeconds + static_cast<double>(frameIndex) / fps;
            ring.commitWrite();

            if (frameIndex == 0) {
                geode::log::info("DecoderFFmpeg: first frame decoded ({}x{} -> {}x{}, fps={:.2f})",
                    width, height, outWidth, outHeight, fps);
            }
            ++frameIndex;
        }

        terminateProcess();
    }
};

DecoderFFmpeg::~DecoderFFmpeg() {
    closeInternal();
}

bool DecoderFFmpeg::isWineEnvironment() {
    static bool const underWine = []() {
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        return ntdll && GetProcAddress(ntdll, "wine_get_version") != nullptr;
    }();
    return underWine;
}

bool DecoderFFmpeg::isFfmpegAvailable() {
    return !resolveFfmpegExecutable().empty();
}

void DecoderFFmpeg::ensureFfmpegAsyncIfNeeded() {
    if (isFfmpegAvailable()) return;
    auto& boot = paimon::menumusic::FfmpegBootstrap::get();
    if (boot.isDownloading()) return;
    static bool requested = false;
    if (requested) return;
    requested = true;
    geode::Loader::get()->queueInMainThread([] {
        auto& b = paimon::menumusic::FfmpegBootstrap::get();
        if (b.exists() || b.isDownloading()) return;
        geode::log::info("DecoderFFmpeg: Wine detected without ffmpeg, downloading bundled ffmpeg...");
        b.ensureInstalled(nullptr, [](bool ok, std::string const& msg) {
            if (ok) geode::log::info("DecoderFFmpeg: bundled ffmpeg ready: {}", msg);
            else geode::log::warn("DecoderFFmpeg: failed to download bundled ffmpeg: {}", msg);
        });
    });
}

bool DecoderFFmpeg::open(const std::string& path) {
    closeInternal();
    auto exe = resolveFfmpegExecutable();
    if (exe.empty()) return false;

    ProbeInfo info;
    if (!probeVideoWithFfmpeg(exe, path, info)) {
        geode::log::warn("DecoderFFmpeg: probe failed for {}", path);
        return false;
    }

    auto st = std::make_shared<State>();
    st->ffmpegExe = std::move(exe);
    st->videoPath = path;
    st->width = info.width & ~1;
    st->height = info.height & ~1;
    if (st->width < 2 || st->height < 2) return false;
    st->fps = info.fps;
    st->duration = info.duration;
    st->colorMatrix = info.colorMatrix;
    st->fullRange = info.fullRange;
    st->rotation = info.rotation;

    int outW = st->width;
    int outH = st->height;
    int cap = paimon::settings::video::videoMaxDecodeDimension();
    if (cap <= 0) cap = 1280;
    int maxDim = std::max(outW, outH);
    if (maxDim > cap) {
        double scale = static_cast<double>(cap) / static_cast<double>(maxDim);
        outW = std::max(2, (static_cast<int>(std::round(outW * scale))) & ~1);
        outH = std::max(2, (static_cast<int>(std::round(outH * scale))) & ~1);
    }
    st->outWidth = outW;
    st->outHeight = outH;

    if (!st->ring.init(outW, outH)) return false;

    geode::log::info("DecoderFFmpeg: opened {} ({}x{} -> {}x{}, fps={:.2f}, dur={:.2f}s)",
        path, st->width, st->height, st->outWidth, st->outHeight, st->fps, st->duration);
    m_state = std::move(st);
    return true;
}

void DecoderFFmpeg::startDecoding() {
    auto st = m_state;
    if (!st) return;
    if (st->decoding.load(std::memory_order_relaxed)) return;
    if (!st->threadDone.load(std::memory_order_acquire)) return;
    st->decoding.store(true, std::memory_order_relaxed);
    st->finished.store(false, std::memory_order_relaxed);
    st->threadDone.store(false, std::memory_order_release);
    m_thread = std::thread([st] { st->decodeLoop(); });
}

void DecoderFFmpeg::stopDecoding() {
    auto st = m_state;
    if (!st) {
        if (m_thread.joinable()) m_thread.join();
        return;
    }
    st->decoding.store(false, std::memory_order_relaxed);
    st->ring.wakeAll();
    st->terminateProcess();
    if (m_thread.joinable()) {
        paimon::joinOrDetachWithWarning(m_thread, std::chrono::milliseconds(25), "DecoderFFmpeg", [&] {
            return st->threadDone.load(std::memory_order_acquire);
        });
    }
}

void DecoderFFmpeg::seekTo(double seconds) {
    auto st = m_state;
    if (!st) return;
    bool const wasDecoding = st->decoding.load(std::memory_order_relaxed);
    stopDecoding();
    if (!st->threadDone.load(std::memory_order_acquire)) return;
    while (st->ring.nextRead()) st->ring.commitRead();
    st->seekSeconds = std::max(0.0, seconds);
    st->finished.store(false, std::memory_order_relaxed);
    if (wasDecoding) startDecoding();
}

double DecoderFFmpeg::getDuration() const { return m_state ? m_state->duration : 0.0; }
int DecoderFFmpeg::getWidth() const { return m_state ? m_state->outWidth : 0; }
int DecoderFFmpeg::getHeight() const { return m_state ? m_state->outHeight : 0; }
int DecoderFFmpeg::getNativeWidth() const { return m_state ? m_state->width : 0; }
int DecoderFFmpeg::getNativeHeight() const { return m_state ? m_state->height : 0; }
VideoColorMatrix DecoderFFmpeg::getColorMatrix() const {
    return m_state ? m_state->colorMatrix : VideoColorMatrix::Auto;
}
bool DecoderFFmpeg::isFullRange() const { return m_state ? m_state->fullRange : false; }
int DecoderFFmpeg::getRotationDegrees() const { return m_state ? m_state->rotation : 0; }
bool DecoderFFmpeg::isFinished() const {
    return m_state ? m_state->finished.load(std::memory_order_acquire) : false;
}
bool DecoderFFmpeg::skipFrame() { return m_state ? m_state->ring.skipRead() : false; }
double DecoderFFmpeg::peekNextPTS() const { return m_state ? m_state->ring.peekNextPTS() : DBL_MAX; }
double DecoderFFmpeg::peekSecondPTS() const { return m_state ? m_state->ring.peekSecondPTS() : DBL_MAX; }
const VideoFrame* DecoderFFmpeg::peekFrame() { return m_state ? m_state->ring.peekRead() : nullptr; }
void DecoderFFmpeg::releaseFrame() {
    if (m_state && m_state->ring.peekRead()) m_state->ring.commitRead();
}

void DecoderFFmpeg::closeInternal() {
    stopDecoding();
    m_state.reset();
}

} // namespace paimon

#endif
