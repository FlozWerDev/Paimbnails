#include "DiscordIpcClient.hpp"

#include <Geode/loader/Log.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <string>
#include <vector>

#ifdef GEODE_IS_WINDOWS
#include <windows.h>
#else
#include <cstdlib>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#endif

namespace paimon::discord {

namespace {

constexpr uint32_t kOpHandshake = 0;
constexpr uint32_t kOpFrame = 1;
constexpr uint32_t kOpClose = 2;

// reconnect throttle: don't hammer the ipc endpoint when discord is closed.
constexpr std::chrono::seconds kReconnectCooldown(15);

// posix send: bound eagain spinning with poll() in short slices.
constexpr int kSendSliceMs = 100;
constexpr int kSendBudgetMs = 500;
// windows overlapped i/o timeouts: writes must never hang the main thread.
constexpr int kPipeWriteTimeoutMs = 500;
constexpr int kPipeReadTimeoutMs = 100;
// cap drained replies so a chatty peer can't spin us forever.
constexpr size_t kMaxDrainBytes = 64 * 1024;
constexpr size_t kLogSnippetLen = 200;

// replies are async; reconnect on error/close if seen.
constexpr char const* kEvtErrorMarker = "\"evt\":\"ERROR\"";
constexpr char const* kCloseMarker = "\"CLOSE\"";

#ifndef GEODE_IS_WINDOWS
#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL; // survive discord closing the socket (no sigpipe)
#else
constexpr int kSendFlags = 0;
#endif
#endif

// explicit little-endian opcode+length header (never rely on host endianness).
std::string buildHeader(uint32_t opcode, uint32_t length) {
    std::string header;
    header.resize(8);
    header[0] = static_cast<char>(opcode & 0xFF);
    header[1] = static_cast<char>((opcode >> 8) & 0xFF);
    header[2] = static_cast<char>((opcode >> 16) & 0xFF);
    header[3] = static_cast<char>((opcode >> 24) & 0xFF);
    header[4] = static_cast<char>(length & 0xFF);
    header[5] = static_cast<char>((length >> 8) & 0xFF);
    header[6] = static_cast<char>((length >> 16) & 0xFF);
    header[7] = static_cast<char>((length >> 24) & 0xFF);
    return header;
}

std::string jsonEscape(std::string const& in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (unsigned char c : in) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

void appendField(std::string& obj, char const* key, std::string const& value, bool& first) {
    if (value.empty()) return;
    if (!first) obj += ',';
    first = false;
    obj += '"';
    obj += key;
    obj += "\":\"";
    obj += jsonEscape(value);
    obj += '"';
}

int currentPid() {
#ifdef GEODE_IS_WINDOWS
    return static_cast<int>(GetCurrentProcessId());
#else
    return static_cast<int>(getpid());
#endif
}

std::string buildActivityJson(DiscordActivity const& a) {
    std::string assets;
    {
        bool first = true;
        assets = "{";
        appendField(assets, "large_image", a.largeImage, first);
        appendField(assets, "large_text", a.largeText, first);
        appendField(assets, "small_image", a.smallImage, first);
        appendField(assets, "small_text", a.smallText, first);
        assets += "}";
        if (first) assets.clear();
    }

    std::string buttons;
    {
        std::string arr = "[";
        bool any = false;
        if (!a.button1Label.empty() && !a.button1Url.empty()) {
            arr += "{\"label\":\"" + jsonEscape(a.button1Label) +
                   "\",\"url\":\"" + jsonEscape(a.button1Url) + "\"}";
            any = true;
        }
        if (!a.button2Label.empty() && !a.button2Url.empty()) {
            if (any) arr += ',';
            arr += "{\"label\":\"" + jsonEscape(a.button2Label) +
                   "\",\"url\":\"" + jsonEscape(a.button2Url) + "\"}";
            any = true;
        }
        arr += "]";
        if (any) buttons = arr;
    }

    std::string activity = "{";
    bool first = true;
    activity += "\"type\":" + std::to_string(static_cast<int>(a.type));
    first = false;
    appendField(activity, "state", a.state, first);
    appendField(activity, "details", a.details, first);
    if (a.startTimestamp > 0) {
        activity += ",\"timestamps\":{\"start\":" + std::to_string(a.startTimestamp) + "}";
    }
    if (!assets.empty()) {
        activity += ",\"assets\":" + assets;
    }
    if (!buttons.empty()) {
        activity += ",\"buttons\":" + buttons;
    }
    activity += "}";
    return activity;
}

} // namespace

DiscordIpcClient& DiscordIpcClient::get() {
    static auto* instance = new DiscordIpcClient();
    return *instance;
}

DiscordIpcClient::~DiscordIpcClient() {
    close();
}

bool DiscordIpcClient::tryConnect() {
#ifdef GEODE_IS_WINDOWS
    for (int i = 0; i < 10; ++i) {
        std::string name = "\\\\?\\pipe\\discord-ipc-" + std::to_string(i);
        HANDLE h = CreateFileA(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                               OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            m_pipe = h;
            return true;
        }
        DWORD err = GetLastError();
        if (err == ERROR_PIPE_BUSY) {
            // pipe busy: skip it, the 15s reconnect cooldown retries.
            if (WaitNamedPipeA(name.c_str(), NMPWAIT_NOWAIT)) {
                h = CreateFileA(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
                if (h != INVALID_HANDLE_VALUE) {
                    m_pipe = h;
                    return true;
                }
            }
            continue;
        }
        if (err != ERROR_FILE_NOT_FOUND) {
            break;
        }
    }
    return false;
#else
    std::vector<std::string> bases;
    auto addBase = [&](char const* dir) {
        if (!dir || !*dir) return;
        if (std::find(bases.begin(), bases.end(), dir) != bases.end()) return;
        bases.emplace_back(dir);
    };
    if (char const* v = std::getenv("XDG_RUNTIME_DIR")) addBase(v);
    if (char const* v = std::getenv("TMPDIR")) addBase(v);
    if (char const* v = std::getenv("TMP")) addBase(v);
    if (char const* v = std::getenv("TEMP")) addBase(v);
    {
        char runUser[64];
        std::snprintf(runUser, sizeof(runUser), "/run/user/%d", static_cast<int>(getuid()));
        addBase(runUser);
    }
    addBase("/tmp");

    char const* suffixes[] = {
        "",
        "/app/com.discordapp.Discord", // flatpak
        "/snap.discord", // snap
    };

    for (auto const& base : bases) {
        for (char const* suffix : suffixes) {
            for (int i = 0; i < 10; ++i) {
                std::string path = base + suffix + "/discord-ipc-" + std::to_string(i);
                int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
                if (fd < 0) return false;

                sockaddr_un addr{};
                addr.sun_family = AF_UNIX;
                if (path.size() >= sizeof(addr.sun_path)) {
                    ::close(fd);
                    continue;
                }
                std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
                if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
                    int flags = fcntl(fd, F_GETFL, 0);
                    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#ifdef __APPLE__
                    int noSigPipe = 1; // so_nosignal equivalent: survive discord restarts
                    (void)::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &noSigPipe, sizeof(noSigPipe));
#endif
                    m_socket = fd;
                    return true;
                }
                ::close(fd);
            }
        }
    }
    return false;
#endif
}

bool DiscordIpcClient::writeFrame(uint32_t opcode, std::string const& payload) {
    std::string header = buildHeader(opcode, static_cast<uint32_t>(payload.size()));

#ifdef GEODE_IS_WINDOWS
    if (!m_pipe) return false;
    HANDLE pipe = static_cast<HANDLE>(m_pipe);
    auto writeAll = [this, pipe](void const* data, size_t size) -> bool {
        char const* p = static_cast<char const*>(data);
        size_t left = size;
        while (left > 0) {
            DWORD chunk = left > 65536 ? 65536 : static_cast<DWORD>(left);
            // heap alloc: kernel may use overlapped after timeout.
            OVERLAPPED* ov = new OVERLAPPED{};
            ov->hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
            if (!ov->hEvent) {
                delete ov;
                return false;
            }
            BOOL ok = WriteFile(pipe, p, chunk, nullptr, ov);
            if (!ok && GetLastError() != ERROR_IO_PENDING) {
                CloseHandle(ov->hEvent);
                delete ov;
                return false;
            }
            if (WaitForSingleObject(ov->hEvent, static_cast<DWORD>(kPipeWriteTimeoutMs)) != WAIT_OBJECT_0) {
                // timeout: cancel, tear down, leak ov to avoid use-after-free.
                CancelIoEx(pipe, ov);
                CloseHandle(ov->hEvent);
                geode::log::warn("[DiscordIPC] pipe write timed out, leaking OVERLAPPED on purpose");
                CloseHandle(pipe);
                m_pipe = nullptr;
                if (m_connected) {
                    m_connected = false;
                    ++m_connectionGeneration;
                }
                return false;
            }
            DWORD written = 0;
            bool done = GetOverlappedResult(pipe, ov, &written, FALSE) != FALSE;
            CloseHandle(ov->hEvent);
            delete ov;
            if (!done || written == 0) return false; // dead pipe (zero-byte completion)
            p += written;
            left -= written;
        }
        return true;
    };
    if (!writeAll(header.data(), header.size())) return false;
    if (!payload.empty() && !writeAll(payload.data(), payload.size())) return false;
    return true;
#else
    if (m_socket < 0) return false;
    auto writeAll = [this](void const* data, size_t size) -> bool {
        char const* p = static_cast<char const*>(data);
        size_t left = size;
        size_t totalSent = 0;
        int waitedMs = 0;
        while (left > 0) {
            ssize_t n = ::send(m_socket, p, left, kSendFlags);
            if (n > 0) {
                p += n;
                left -= static_cast<size_t>(n);
                totalSent += static_cast<size_t>(n);
                continue;
            }
            if (n < 0) {
                if (errno == EINTR) continue;
                if (errno == EPIPE || errno == ECONNRESET) return false;
                if (errno != EAGAIN && errno != EWOULDBLOCK) return false;
            }
            // eagain: wait writable in short slices, bounded budget.
            pollfd pfd{};
            pfd.fd = m_socket;
            pfd.events = POLLOUT;
            int rc = ::poll(&pfd, 1, kSendSliceMs);
            waitedMs += kSendSliceMs;
            if (rc <= 0) return false;
            if (waitedMs >= kSendBudgetMs) return false;
            if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) return false;
        }
        if (size > 0 && totalSent == 0) return false;
        return true;
    };
    if (!writeAll(header.data(), header.size())) return false;
    if (!payload.empty() && !writeAll(payload.data(), payload.size())) return false;
    return true;
#endif
}

bool DiscordIpcClient::drainReads() {
    // drain replies so os buffer doesn't fill; false means peer dead.
    std::string drained;
    char buf[2048];
#ifdef GEODE_IS_WINDOWS
    if (!m_pipe) return true;
    HANDLE pipe = static_cast<HANDLE>(m_pipe);
    while (drained.size() < kMaxDrainBytes) {
        DWORD avail = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &avail, nullptr)) {
            return GetLastError() != ERROR_BROKEN_PIPE;
        }
        if (avail == 0) break;
        DWORD toRead = avail < static_cast<DWORD>(sizeof(buf)) ? avail : static_cast<DWORD>(sizeof(buf));
        // heap alloc: same leak-on-timeout as writeframe.
        OVERLAPPED* ov = new OVERLAPPED{};
        ov->hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
        if (!ov->hEvent) {
            delete ov;
            break;
        }
        BOOL ok = ReadFile(pipe, buf, toRead, nullptr, ov);
        if (!ok && GetLastError() != ERROR_IO_PENDING) {
            DWORD err = GetLastError();
            CloseHandle(ov->hEvent);
            delete ov;
            if (err == ERROR_BROKEN_PIPE) return false;
            break;
        }
        if (WaitForSingleObject(ov->hEvent, static_cast<DWORD>(kPipeReadTimeoutMs)) != WAIT_OBJECT_0) {
            CancelIoEx(pipe, ov);
            CloseHandle(ov->hEvent);
            // leak ov on purpose (see writeframe).
            geode::log::warn("[DiscordIPC] pipe read timed out, leaking OVERLAPPED on purpose");
            return false;
        }
        DWORD read = 0;
        bool done = GetOverlappedResult(pipe, ov, &read, FALSE) != FALSE;
        CloseHandle(ov->hEvent);
        delete ov;
        if (!done || read == 0) return false;
        drained.append(buf, read);
    }
#else
    if (m_socket < 0) return true;
    while (drained.size() < kMaxDrainBytes) {
        ssize_t n = ::recv(m_socket, buf, sizeof(buf), 0);
        if (n == 0) return false;
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == ECONNRESET || errno == EPIPE) return false;
            break; // eagain means drained.
        }
        drained.append(buf, static_cast<size_t>(n));
    }
#endif
    // force reconnect on error/close reply.
    if (drained.find(kEvtErrorMarker) != std::string::npos) {
        geode::log::warn("[DiscordIPC] Discord replied ERROR, forcing reconnect: {}",
                         drained.substr(0, kLogSnippetLen));
        return false;
    }
    if (drained.find(kCloseMarker) != std::string::npos) {
        return false;
    }
    return true;
}

void DiscordIpcClient::handleDisconnect() {
    close();
}

bool DiscordIpcClient::ensureConnected() {
    if (m_connected) return true;
    if (m_clientID.empty()) return false;

    // steady_clock: wall-clock jumps (ntp/sleep) must not change the throttle.
    auto now = std::chrono::steady_clock::now();
    if (m_lastConnectAttempt != std::chrono::steady_clock::time_point{} &&
        now - m_lastConnectAttempt < kReconnectCooldown) {
        return false;
    }
    m_lastConnectAttempt = now;

    if (!tryConnect()) return false;

    std::string handshake = "{\"v\":1,\"client_id\":\"" + jsonEscape(m_clientID) + "\"}";
    if (!writeFrame(kOpHandshake, handshake)) {
        close();
        return false;
    }
    m_connected = true;
    ++m_connectionGeneration;
    if (!drainReads()) {
        handleDisconnect();
        return false;
    }
    return true;
}

void DiscordIpcClient::update(DiscordActivity const& activity) {
    if (!ensureConnected()) return;

    std::string payload = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" +
        std::to_string(currentPid()) + ",\"activity\":" + buildActivityJson(activity) +
        "},\"nonce\":\"" + std::to_string(++m_nonce) + "\"}";

    if (!writeFrame(kOpFrame, payload)) {
        handleDisconnect();
        return;
    }
    if (!drainReads()) {
        handleDisconnect();
    }
}

void DiscordIpcClient::clear() {
    if (!m_connected) return;

    std::string payload = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" +
        std::to_string(currentPid()) + "},\"nonce\":\"" + std::to_string(++m_nonce) + "\"}";

    if (!writeFrame(kOpFrame, payload)) {
        handleDisconnect();
        return;
    }
    if (!drainReads()) {
        handleDisconnect();
    }
}

void DiscordIpcClient::close() {
#ifdef GEODE_IS_WINDOWS
    if (m_pipe) {
        if (m_connected) writeFrame(kOpClose, "{}");
        // writeframe may already have torn the pipe down on a write timeout.
        if (m_pipe) {
            CloseHandle(static_cast<HANDLE>(m_pipe));
            m_pipe = nullptr;
        }
    }
#else
    if (m_socket >= 0) {
        if (m_connected) writeFrame(kOpClose, "{}");
        ::close(m_socket);
        m_socket = -1;
    }
#endif
    // only tearing down a live connection counts as a generation change.
    if (m_connected) {
        ++m_connectionGeneration;
    }
    m_connected = false;
}

} // namespace paimon::discord
