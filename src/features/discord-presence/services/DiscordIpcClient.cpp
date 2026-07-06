#include "DiscordIpcClient.hpp"

#include <ctime>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <cstdlib>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#endif

namespace paimon::discord {

namespace {

constexpr uint32_t kOpHandshake = 0;
constexpr uint32_t kOpFrame = 1;
constexpr uint32_t kOpClose = 2;

// Reconnect throttle: don't hammer the IPC endpoint when Discord is closed.
constexpr int64_t kReconnectCooldownSeconds = 15;

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
#ifdef _WIN32
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
        if (first) assets.clear(); // no asset fields
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
#ifdef _WIN32
    for (int i = 0; i < 10; ++i) {
        std::string name = "\\\\?\\pipe\\discord-ipc-" + std::to_string(i);
        HANDLE h = CreateFileA(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                               OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            m_pipe = h;
            return true;
        }
        if (GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PIPE_BUSY) {
            // Other error; stop scanning.
            break;
        }
    }
    return false;
#else
    char const* base = nullptr;
    for (char const* var : {"XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"}) {
        if (auto* v = std::getenv(var)) { base = v; break; }
    }
    if (!base) base = "/tmp";

    for (int i = 0; i < 10; ++i) {
        std::string path = std::string(base) + "/discord-ipc-" + std::to_string(i);
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
            m_socket = fd;
            return true;
        }
        ::close(fd);
    }
    return false;
#endif
}

bool DiscordIpcClient::writeFrame(uint32_t opcode, std::string const& payload) {
    uint32_t header[2] = { opcode, static_cast<uint32_t>(payload.size()) };

#ifdef _WIN32
    if (!m_pipe) return false;
    auto writeAll = [this](void const* data, size_t size) -> bool {
        char const* p = static_cast<char const*>(data);
        size_t left = size;
        while (left > 0) {
            DWORD written = 0;
            if (!WriteFile(static_cast<HANDLE>(m_pipe), p, static_cast<DWORD>(left), &written, nullptr)) {
                return false;
            }
            p += written;
            left -= written;
        }
        return true;
    };
    if (!writeAll(header, sizeof(header))) return false;
    if (!payload.empty() && !writeAll(payload.data(), payload.size())) return false;
    return true;
#else
    if (m_socket < 0) return false;
    auto writeAll = [this](void const* data, size_t size) -> bool {
        char const* p = static_cast<char const*>(data);
        size_t left = size;
        while (left > 0) {
            ssize_t n = ::send(m_socket, p, left, 0);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
                return false;
            }
            p += n;
            left -= static_cast<size_t>(n);
        }
        return true;
    };
    if (!writeAll(header, sizeof(header))) return false;
    if (!payload.empty() && !writeAll(payload.data(), payload.size())) return false;
    return true;
#endif
}

void DiscordIpcClient::drainReads() {
    // Discord replies to every frame; drain so the OS buffer doesn't fill up.
    char buf[2048];
#ifdef _WIN32
    if (!m_pipe) return;
    DWORD avail = 0;
    while (PeekNamedPipe(static_cast<HANDLE>(m_pipe), nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
        DWORD read = 0;
        DWORD toRead = avail < sizeof(buf) ? avail : sizeof(buf);
        if (!ReadFile(static_cast<HANDLE>(m_pipe), buf, toRead, &read, nullptr) || read == 0) break;
    }
#else
    if (m_socket < 0) return;
    while (true) {
        ssize_t n = ::recv(m_socket, buf, sizeof(buf), 0);
        if (n <= 0) break; // nonblocking: EAGAIN => nothing left
    }
#endif
}

void DiscordIpcClient::handleDisconnect() {
    close();
}

bool DiscordIpcClient::ensureConnected() {
    if (m_connected) return true;
    if (m_clientID.empty()) return false;

    int64_t now = static_cast<int64_t>(std::time(nullptr));
    if (now - m_lastConnectAttempt < kReconnectCooldownSeconds) return false;
    m_lastConnectAttempt = now;

    if (!tryConnect()) return false;

    std::string handshake = "{\"v\":1,\"client_id\":\"" + jsonEscape(m_clientID) + "\"}";
    if (!writeFrame(kOpHandshake, handshake)) {
        close();
        return false;
    }
    m_connected = true;
    drainReads();
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
    drainReads();
}

void DiscordIpcClient::clear() {
    if (!m_connected) return;

    std::string payload = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" +
        std::to_string(currentPid()) + "},\"nonce\":\"" + std::to_string(++m_nonce) + "\"}";

    if (!writeFrame(kOpFrame, payload)) {
        handleDisconnect();
        return;
    }
    drainReads();
}

void DiscordIpcClient::close() {
#ifdef _WIN32
    if (m_pipe) {
        if (m_connected) writeFrame(kOpClose, "{}");
        CloseHandle(static_cast<HANDLE>(m_pipe));
        m_pipe = nullptr;
    }
#else
    if (m_socket >= 0) {
        if (m_connected) writeFrame(kOpClose, "{}");
        ::close(m_socket);
        m_socket = -1;
    }
#endif
    m_connected = false;
}

} // namespace paimon::discord
