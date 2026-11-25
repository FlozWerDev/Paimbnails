#include "DiscordIpcClient.hpp"
#include "../model/PresenceCustomization.hpp"
#include <matjson.hpp>

#include <Geode/loader/Log.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <string>
#include <vector>
#include <memory>

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
constexpr uint32_t kOpPing = 3;
constexpr uint32_t kOpPong = 4;

// reconnect throttle: don't hammer the ipc endpoint when discord is closed.
constexpr std::chrono::seconds kReconnectCooldown(15);

// windows overlapped i/o timeouts: writes must never hang the main thread.
constexpr int kPipeWriteTimeoutMs = 500;
constexpr int kPipeReadTimeoutMs = 100;
// cap drained replies so a chatty peer can't spin us forever.
constexpr size_t kMaxDrainBytes = 64 * 1024;

#ifdef GEODE_IS_WINDOWS
struct PipeOperation {
    OVERLAPPED overlapped{};
    std::vector<char> buffer;
    explicit PipeOperation(std::size_t size) : buffer(size) {
        overlapped.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    }
    ~PipeOperation() { if (overlapped.hEvent) CloseHandle(overlapped.hEvent); }
};
// only the IPC worker accesses this pool. Own both OVERLAPPED and its buffer
// until cancellation completes; closing an event alone does not stop kernel I/O.
auto& cancelledOperations() {
    static auto* operations = [] {
        auto operations = std::make_unique<std::vector<std::unique_ptr<PipeOperation>>>();
        operations->reserve(8);
        return operations.release();
    }();
    return *operations;
}
void collectCancelledOperations() {
    std::erase_if(cancelledOperations(), [](auto const& operation) {
        return WaitForSingleObject(operation->overlapped.hEvent, 0) == WAIT_OBJECT_0;
    });
}
bool finishPipeOperation(HANDLE pipe, std::unique_ptr<PipeOperation>& operation, DWORD timeout, DWORD& transferred) {
    if (WaitForSingleObject(operation->overlapped.hEvent, timeout) != WAIT_OBJECT_0) {
        CancelIoEx(pipe, &operation->overlapped);
        cancelledOperations().push_back(std::move(operation));
        return false;
    }
    return GetOverlappedResult(pipe, &operation->overlapped, &transferred, FALSE) && transferred > 0;
}
#endif

#ifndef GEODE_IS_WINDOWS
constexpr int kSendSliceMs = 100;
constexpr int kSendBudgetMs = 500;
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
    if (a.partySize > 0 && a.partyMax >= a.partySize) {
        activity += ",\"party\":{\"size\":[" + std::to_string(a.partySize) + "," + std::to_string(a.partyMax) + "]}";
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
        std::string name = "\\\\.\\pipe\\discord-ipc-" + std::to_string(i);
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
                int flags = fcntl(fd, F_GETFL, 0);
                if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) { ::close(fd); continue; }
                bool connected = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
                if (!connected && errno == EINPROGRESS) {
                    pollfd pending{fd, POLLOUT, 0};
                    int error = 0;
                    socklen_t length = sizeof(error);
                    connected = ::poll(&pending, 1, kSendSliceMs) > 0 &&
                        ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) == 0 && error == 0;
                }
                if (connected) {
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
    auto writeAll = [pipe](void const* data, size_t size) -> bool {
        auto* bytes = static_cast<char const*>(data);
        while (size > 0) {
            collectCancelledOperations();
            if (cancelledOperations().size() >= 8) return false;
            DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(size, 65536));
            auto operation = std::make_unique<PipeOperation>(chunk);
            if (!operation->overlapped.hEvent) return false;
            std::copy_n(bytes, chunk, operation->buffer.data());
            BOOL ok = WriteFile(pipe, operation->buffer.data(), chunk, nullptr, &operation->overlapped);
            if (!ok && GetLastError() != ERROR_IO_PENDING) return false;
            DWORD written = 0;
            if (!finishPipeOperation(pipe, operation, kPipeWriteTimeoutMs, written)) return false;
            bytes += written;
            size -= written;
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

void DiscordIpcClient::setError(std::string message) {
    std::lock_guard lock(m_errorMutex);
    m_lastError = cleanText(message, 160);
}

std::string DiscordIpcClient::lastError() const {
    std::lock_guard lock(m_errorMutex);
    return m_lastError;
}

void DiscordIpcClient::setClientID(std::string id) {
    if (id == m_clientID) return;
    close();
    m_clientID = std::move(id);
    m_lastConnectAttempt = {};
    setError({});
}

bool DiscordIpcClient::processFrames() {
    auto readLE = [](char const* bytes) {
        uint32_t value = 0;
        for (int i = 0; i < 4; ++i) value |= static_cast<uint32_t>(static_cast<unsigned char>(bytes[i])) << (8 * i);
        return value;
    };
    while (m_receiveBuffer.size() >= 8) {
        auto opcode = readLE(m_receiveBuffer.data());
        auto size = readLE(m_receiveBuffer.data() + 4);
        if (size > kMaxDrainBytes) { setError("RPC frame exceeds size limit"); return false; }
        if (m_receiveBuffer.size() < 8 + size) return true;
        auto payload = m_receiveBuffer.substr(8, size);
        m_receiveBuffer.erase(0, 8 + size);
        if (opcode == kOpClose) { setError("Discord closed the connection"); return false; }
        if (opcode == kOpPing) {
            if (!writeFrame(kOpPong, payload)) return false;
            continue;
        }
        if (opcode == kOpPong) continue;
        if (opcode != kOpFrame) { setError("Unexpected RPC opcode"); return false; }
        auto parsed = matjson::parse(payload);
        if (!parsed || !parsed.unwrap().isObject()) { setError("Invalid JSON in Discord reply"); return false; }
        auto event = parsed.unwrap()["evt"].asString().unwrapOr("");
        if (event == "ERROR") {
            auto const& data = parsed.unwrap()["data"];
            auto message = data.isObject() ? data["message"].asString().unwrapOr("Activity was rejected") : "Activity was rejected";
            setError(message);
            return false;
        }
        if (event == "READY") { m_ready.store(true, std::memory_order_release); setError({}); }
    }
    return true;
}

bool DiscordIpcClient::drainReads() {
    std::size_t drained = 0;
#ifdef GEODE_IS_WINDOWS
    if (!m_pipe) return false;
    auto pipe = static_cast<HANDLE>(m_pipe);
    while (drained < kMaxDrainBytes) {
        DWORD available = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) return false;
        if (!available) break;
        collectCancelledOperations();
        if (cancelledOperations().size() >= 8) return false;
        DWORD amount = std::min<DWORD>(available, 2048);
        auto operation = std::make_unique<PipeOperation>(amount);
        if (!operation->overlapped.hEvent) return false;
        BOOL ok = ReadFile(pipe, operation->buffer.data(), amount, nullptr, &operation->overlapped);
        if (!ok && GetLastError() != ERROR_IO_PENDING) return false;
        DWORD received = 0;
        if (!finishPipeOperation(pipe, operation, kPipeReadTimeoutMs, received)) return false;
        m_receiveBuffer.append(operation->buffer.data(), received);
        drained += received;
        if (!processFrames()) return false;
    }
#else
    if (m_socket < 0) return false;
    char buffer[2048];
    while (drained < kMaxDrainBytes) {
        auto received = ::recv(m_socket, buffer, sizeof(buffer), 0);
        if (received == 0) return false;
        if (received < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            return false;
        }
        m_receiveBuffer.append(buffer, static_cast<std::size_t>(received));
        drained += static_cast<std::size_t>(received);
        if (!processFrames()) return false;
    }
#endif
    return true;
}

void DiscordIpcClient::handleDisconnect() {
    // do not send CLOSE over a transport that has already failed.
    if (m_connected.exchange(false, std::memory_order_acq_rel)) ++m_connectionGeneration;
    close();
}

void DiscordIpcClient::poll() {
#ifdef GEODE_IS_WINDOWS
    collectCancelledOperations();
#endif
    if (isConnected() && !drainReads()) { handleDisconnect(); return; }
    if (isConnected() && !isReady() && std::chrono::steady_clock::now() - m_handshakeStarted > std::chrono::seconds(5)) {
        setError("Discord handshake timed out");
        handleDisconnect();
    }
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

#ifdef GEODE_IS_WINDOWS
    collectCancelledOperations();
    if (cancelledOperations().size() >= 8) { setError("Waiting for pending pipe cancellation"); return false; }
#endif
    if (!tryConnect()) return false;

    std::string handshake = "{\"v\":1,\"client_id\":\"" + jsonEscape(m_clientID) + "\"}";
    if (!writeFrame(kOpHandshake, handshake)) {
        close();
        return false;
    }
    m_handshakeStarted = std::chrono::steady_clock::now();
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
        std::to_string(currentPid()) + ",\"activity\":null},\"nonce\":\"" + std::to_string(++m_nonce) + "\"}";

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
        // cancelled operations retain their buffers until the kernel signals completion.
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
    m_ready = false;
    m_handshakeStarted = {};
    m_receiveBuffer.clear();
}

} // namespace paimon::discord
