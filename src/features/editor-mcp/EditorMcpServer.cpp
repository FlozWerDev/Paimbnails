#ifdef GEODE_IS_WINDOWS
#include <winsock.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "EditorMcpServer.hpp"

#include "EditorMcpBridge.hpp"
#include "EditorMcpProtocol.hpp"
#include "../../core/modules/ModuleRegistry.hpp"
#include "../../utils/JoinWithWarning.hpp"

#include <Geode/Geode.hpp>
#include <fmt/format.h>
#include <matjson.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

using namespace geode::prelude;

namespace paimon::editormcp {

namespace {

constexpr char const* kModuleID = "paimbnails.mcp.editor";
constexpr char const* kPortSetting = "editor-mcp-port";
constexpr uint16_t kDefaultPort = 21690;
constexpr int kListenBacklog = 16;
constexpr size_t kMaxRequestBodyBytes = 4 * 1024 * 1024;

#ifdef GEODE_IS_WINDOWS
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
void closeSocket(SocketHandle s) {
    if (s != kInvalidSocket) closesocket(s);
}
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
void closeSocket(SocketHandle s) {
    if (s >= 0) ::close(s);
}
#endif

void setSocketTimeouts(SocketHandle s, int recvMs, int sendMs) {
#ifdef GEODE_IS_WINDOWS
    DWORD r = static_cast<DWORD>(recvMs);
    DWORD w = static_cast<DWORD>(sendMs);
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char const*>(&r), sizeof(r));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char const*>(&w), sizeof(w));
#else
    timeval r{recvMs / 1000, (recvMs % 1000) * 1000};
    timeval w{sendMs / 1000, (sendMs % 1000) * 1000};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &r, sizeof(r));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &w, sizeof(w));
#endif
}

bool sendAll(SocketHandle client, std::string_view data) {
    size_t sent = 0;
    while (sent < data.size()) {
        int chunk = ::send(
            client,
            data.data() + sent,
            static_cast<int>(std::min<size_t>(data.size() - sent, 65536)),
            0
        );
        if (chunk <= 0) return false;
        sent += static_cast<size_t>(chunk);
    }
    return true;
}

std::string toLowerCopy(std::string_view sv) {
    std::string out;
    out.reserve(sv.size());
    for (unsigned char c : sv) out.push_back(static_cast<char>(std::tolower(c)));
    return out;
}

std::string trimAscii(std::string_view sv) {
    size_t a = 0;
    while (a < sv.size() && (sv[a] == ' ' || sv[a] == '\t' || sv[a] == '\r' || sv[a] == '\n')) ++a;
    size_t b = sv.size();
    while (b > a && (sv[b - 1] == ' ' || sv[b - 1] == '\t' || sv[b - 1] == '\r' || sv[b - 1] == '\n')) --b;
    return std::string(sv.substr(a, b - a));
}

bool isValidLocalHost(std::string const& hostHeader, uint16_t port) {
    if (hostHeader.empty()) return true; // non-browser local clients may omit Host
    std::string const h = toLowerCopy(trimAscii(hostHeader));
    std::string const portStr = std::to_string(port);
    return h == "localhost" || h == "127.0.0.1" || h == "[::1]" ||
           h == ("localhost:" + portStr) ||
           h == ("127.0.0.1:" + portStr) ||
           h == ("[::1]:" + portStr);
}

std::string buildHttpResponse(
    int code,
    char const* reason,
    std::string_view contentType,
    std::string_view body,
    std::string_view extraHeaders = ""
) {
    std::string hdr = fmt::format(
        "HTTP/1.1 {} {}\r\n"
        "Content-Type: {}\r\n"
        "Content-Length: {}\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-store\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, DELETE, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type, Accept, Mcp-Session-Id, Last-Event-ID\r\n"
        "Access-Control-Expose-Headers: Mcp-Session-Id\r\n"
        "{}\r\n",
        code, reason, contentType, body.size(), extraHeaders
    );
    hdr.append(body);
    return hdr;
}

bool decodeChunkedBody(std::string_view raw, std::string& outBody) {
    outBody.clear();
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t lineEnd = raw.find("\r\n", pos);
        if (lineEnd == std::string_view::npos) return false;
        std::string_view sizeLine = raw.substr(pos, lineEnd - pos);
        size_t semi = sizeLine.find(';');
        if (semi != std::string_view::npos) sizeLine = sizeLine.substr(0, semi);
        size_t chunkSize = 0;
        for (char c : sizeLine) {
            if (c == ' ' || c == '\t') continue;
            int digit = -1;
            if (c >= '0' && c <= '9') digit = c - '0';
            else if (c >= 'a' && c <= 'f') digit = 10 + (c - 'a');
            else if (c >= 'A' && c <= 'F') digit = 10 + (c - 'A');
            else return false;
            chunkSize = (chunkSize << 4) | static_cast<size_t>(digit);
        }
        pos = lineEnd + 2;
        if (chunkSize == 0) return true;
        if (outBody.size() + chunkSize > kMaxRequestBodyBytes) return false;
        if (pos + chunkSize + 2 > raw.size()) return false;
        outBody.append(raw.substr(pos, chunkSize));
        pos += chunkSize + 2;
    }
    return false;
}

std::unordered_map<std::string, std::string> parseQueryParams(std::string_view target) {
    std::unordered_map<std::string, std::string> out;
    size_t q = target.find('?');
    if (q == std::string_view::npos) return out;
    std::string_view qs = target.substr(q + 1);
    size_t pos = 0;
    while (pos < qs.size()) {
        size_t amp = qs.find('&', pos);
        std::string_view pair = amp == std::string_view::npos ? qs.substr(pos) : qs.substr(pos, amp - pos);
        size_t eq = pair.find('=');
        if (eq != std::string_view::npos) {
            out[std::string(pair.substr(0, eq))] = std::string(pair.substr(eq + 1));
        }
        if (amp == std::string_view::npos) break;
        pos = amp + 1;
    }
    return out;
}

constexpr char const* kBridgeScriptPy = R"PY(#!/usr/bin/env python3
"""Stdio-to-Streamable-HTTP bridge for Geometry Dash Editor MCP."""
import json
import os
import sys
import urllib.error
import urllib.request

MCP_URL = os.environ.get("GD_MCP_URL", "http://127.0.0.1:21690/mcp")
SESSION_ID = None

def forward(line: str) -> None:
    global SESSION_ID
    headers = {
        "Content-Type": "application/json",
        "Accept": "application/json, text/event-stream",
    }
    if SESSION_ID:
        headers["Mcp-Session-Id"] = SESSION_ID
    req = urllib.request.Request(MCP_URL, data=line.encode("utf-8"), headers=headers, method="POST")
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            sid = resp.headers.get("Mcp-Session-Id")
            if sid:
                SESSION_ID = sid
            body = resp.read().decode("utf-8").strip()
            if body:
                sys.stdout.write(body + "\n")
                sys.stdout.flush()
    except urllib.error.HTTPError as e:
        err_body = e.read().decode("utf-8", errors="replace").strip()
        if err_body:
            sys.stdout.write(err_body + "\n")
            sys.stdout.flush()
    except Exception as ex:
        try:
            msg = json.loads(line)
            rid = msg.get("id")
        except Exception:
            rid = None
        if rid is not None:
            out = {"jsonrpc": "2.0", "id": rid, "error": {"code": -32000, "message": f"GD MCP unreachable at {MCP_URL}: {ex}"}}
            sys.stdout.write(json.dumps(out) + "\n")
            sys.stdout.flush()

def main() -> None:
    for raw in sys.stdin:
        line = raw.strip()
        if line:
            forward(line)

if __name__ == "__main__":
    main()
)PY";

uint16_t configuredPort() {
    auto* mod = Mod::get();
    if (!mod || !mod->hasSetting(kPortSetting)) return kDefaultPort;
    int64_t val = mod->getSettingValue<int64_t>(kPortSetting);
    if (val < 1024 || val > 65535) return kDefaultPort;
    return static_cast<uint16_t>(val);
}

} // namespace

struct EditorMcpServer::Impl {
    std::atomic_bool stopping{false};
    std::atomic_bool running{false};
    std::atomic_bool finished{true};
    std::atomic<uint16_t> activePort{kDefaultPort};
    std::atomic<uint64_t> totalRequests{0};
    std::atomic<uint64_t> toolCalls{0};
    std::atomic<uint64_t> rendersServed{0};
    std::atomic<uint64_t> objectsCreated{0};

    std::thread worker;
    mutable std::mutex mutex;
    std::string status = "Stopped";
    std::string lastTool;
    std::string lastError;
    std::string sessionId = "gd-mcp-session-1";

    std::atomic<SocketHandle> listener{kInvalidSocket};

    void setStatus(std::string text, std::string err = "") {
        std::lock_guard lock(mutex);
        status = std::move(text);
        if (!err.empty()) lastError = std::move(err);
    }

    void closeListener() {
        auto sock = listener.exchange(kInvalidSocket);
        if (sock == kInvalidSocket) return;
#ifdef GEODE_IS_WINDOWS
        ::shutdown(sock, 2);
#else
        ::shutdown(sock, SHUT_RDWR);
#endif
        closeSocket(sock);
    }

    std::string handleHttpRequest(
        std::string const& method,
        std::string const& rawTarget,
        std::unordered_map<std::string, std::string> const& headers,
        std::string const& body
    ) {
        totalRequests.fetch_add(1, std::memory_order_relaxed);

        if (method == "OPTIONS") {
            return buildHttpResponse(204, "No Content", "text/plain", "");
        }

        std::string path = rawTarget;
        size_t q = path.find('?');
        if (q != std::string::npos) path.resize(q);

        if (method == "GET" && path == "/health") {
            auto info = matjson::makeObject({
                {"ok", true},
                {"name", "geometry-dash-editor-mcp"},
                {"port", static_cast<int>(activePort.load())},
                {"mcpUrl", fmt::format("http://127.0.0.1:{}/mcp", activePort.load())},
                {"renderUrl", fmt::format("http://127.0.0.1:{}/render.png", activePort.load())},
                {"toolCalls", static_cast<int64_t>(toolCalls.load())},
                {"rendersServed", static_cast<int64_t>(rendersServed.load())},
            });
            return buildHttpResponse(200, "OK", "application/json; charset=utf-8", info.dump(matjson::NO_INDENTATION));
        }

        if (method == "GET" && path == "/bridge.py") {
            return buildHttpResponse(200, "OK", "text/x-python; charset=utf-8", kBridgeScriptPy);
        }

        if (method == "GET" && path == "/render.png") {
            auto qp = parseQueryParams(rawTarget);
            RenderRequest req;
            if (qp.contains("mode")) req.mode = qp["mode"];
            if (qp.contains("x") || qp.contains("y")) {
                req.hasCenter = true;
                req.centerX = qp.contains("x") ? std::strtof(qp["x"].c_str(), nullptr) : 0.f;
                req.centerY = qp.contains("y") ? std::strtof(qp["y"].c_str(), nullptr) : 105.f;
            }
            if (qp.contains("zoom")) req.zoom = std::strtof(qp["zoom"].c_str(), nullptr);
            if (qp.contains("width")) req.width = std::atoi(qp["width"].c_str());
            if (qp.contains("height")) req.height = std::atoi(qp["height"].c_str());
            if (qp.contains("show_grid")) req.showGrid = (qp["show_grid"] == "1" || qp["show_grid"] == "true");
            if (qp.contains("show_ui")) req.showUI = (qp["show_ui"] == "1" || qp["show_ui"] == "true");

            RenderResult rr = capturePreviewMainThread(req);
            if (!rr.ok) {
                return buildHttpResponse(503, "Service Unavailable", "application/json; charset=utf-8",
                    matjson::makeObject({{"ok", false}, {"error", rr.error}}).dump(matjson::NO_INDENTATION));
            }
            std::string_view bytes(reinterpret_cast<char const*>(rr.pngBytes.data()), rr.pngBytes.size());
            return buildHttpResponse(200, "OK", "image/png", bytes);
        }

        if (method == "GET" && (path == "/" || path == "/mcp")) {
            if (path == "/mcp") {
                if (auto it = headers.find("accept"); it != headers.end()) {
                    std::string const acc = toLowerCopy(it->second);
                    if (acc.find("text/event-stream") != std::string::npos &&
                        acc.find("application/json") == std::string::npos) {
                        return buildHttpResponse(
                            405, "Method Not Allowed", "text/plain; charset=utf-8",
                            "SSE stream on GET /mcp is not offered; use POST /mcp.",
                            "Allow: POST, DELETE, OPTIONS\r\n"
                        );
                    }
                }
            }
            auto info = matjson::makeObject({
                {"name", "geometry-dash-editor-mcp"},
                {"protocol", "MCP Streamable HTTP (JSON-RPC 2.0)"},
                {"endpoint", "/mcp"},
                {"renderPreview", "/render.png"},
                {"stdioBridge", "/bridge.py"},
            });
            return buildHttpResponse(200, "OK", "application/json; charset=utf-8", info.dump(matjson::NO_INDENTATION));
        }

        if (method == "DELETE" && path == "/mcp") {
            return buildHttpResponse(200, "OK", "application/json; charset=utf-8", "{\"ok\":true}");
        }

        if (method == "POST" && (path == "/mcp" || path == "/")) {
            auto rpc = dispatchMcpJsonRpc(body);
            std::string extra = fmt::format("Mcp-Session-Id: {}\r\n", sessionId);
            if (rpc.httpStatus == 202 || rpc.body.empty()) {
                return buildHttpResponse(202, "Accepted", "application/json; charset=utf-8", "", extra);
            }
            char const* reason = rpc.httpStatus == 400 ? "Bad Request" : "OK";
            return buildHttpResponse(rpc.httpStatus, reason, "application/json; charset=utf-8", rpc.body, extra);
        }

        return buildHttpResponse(404, "Not Found", "text/plain; charset=utf-8", "Not Found");
    }

    void handleClient(SocketHandle client) {
        setSocketTimeouts(client, 3000, 10000);

        std::string raw;
        raw.reserve(4096);
        char buf[4096];
        size_t headerEnd = std::string::npos;

        while (raw.size() < 64 * 1024) {
            int n = ::recv(client, buf, sizeof(buf), 0);
            if (n <= 0) break;
            raw.append(buf, static_cast<size_t>(n));
            headerEnd = raw.find("\r\n\r\n");
            if (headerEnd != std::string::npos) break;
        }
        if (headerEnd == std::string::npos) return;

        std::string_view headerView(raw.data(), headerEnd);
        size_t lineEnd = headerView.find("\r\n");
        std::string_view reqLine = lineEnd == std::string_view::npos ? headerView : headerView.substr(0, lineEnd);

        size_t sp1 = reqLine.find(' ');
        size_t sp2 = sp1 == std::string_view::npos ? std::string_view::npos : reqLine.find(' ', sp1 + 1);
        if (sp1 == std::string_view::npos || sp2 == std::string_view::npos) {
            sendAll(client, buildHttpResponse(400, "Bad Request", "text/plain", "Malformed HTTP request"));
            return;
        }
        std::string const method(reqLine.substr(0, sp1));
        std::string const target(reqLine.substr(sp1 + 1, sp2 - sp1 - 1));

        std::unordered_map<std::string, std::string> headers;
        size_t hPos = (lineEnd == std::string_view::npos) ? headerView.size() : lineEnd + 2;
        while (hPos < headerView.size()) {
            size_t next = headerView.find("\r\n", hPos);
            std::string_view hLine = next == std::string_view::npos
                ? headerView.substr(hPos) : headerView.substr(hPos, next - hPos);
            size_t colon = hLine.find(':');
            if (colon != std::string_view::npos) {
                headers[toLowerCopy(trimAscii(hLine.substr(0, colon)))] = trimAscii(hLine.substr(colon + 1));
            }
            if (next == std::string_view::npos) break;
            hPos = next + 2;
        }

        if (auto it = headers.find("host"); it != headers.end() && !isValidLocalHost(it->second, activePort.load())) {
            sendAll(client, buildHttpResponse(421, "Misdirected Request", "text/plain", "Invalid Host"));
            return;
        }

        std::string body = raw.substr(headerEnd + 4);
        bool const isChunked = headers.contains("transfer-encoding") &&
            toLowerCopy(headers["transfer-encoding"]).find("chunked") != std::string::npos;

        if (isChunked) {
            while (body.find("\r\n0\r\n\r\n") == std::string::npos && body.find("0\r\n\r\n") != 0) {
                if (body.size() >= kMaxRequestBodyBytes) {
                    sendAll(client, buildHttpResponse(413, "Payload Too Large", "text/plain", "Body too large"));
                    return;
                }
                int n = ::recv(client, buf, sizeof(buf), 0);
                if (n <= 0) break;
                body.append(buf, static_cast<size_t>(n));
            }
            std::string decoded;
            if (!decodeChunkedBody(body, decoded)) {
                sendAll(client, buildHttpResponse(400, "Bad Request", "text/plain", "Invalid chunked body"));
                return;
            }
            body = std::move(decoded);
        } else if (auto it = headers.find("content-length"); it != headers.end()) {
            size_t contentLen = static_cast<size_t>(std::strtoull(it->second.c_str(), nullptr, 10));
            if (contentLen > kMaxRequestBodyBytes) {
                sendAll(client, buildHttpResponse(413, "Payload Too Large", "text/plain", "Body too large"));
                return;
            }
            while (body.size() < contentLen) {
                int n = ::recv(client, buf, sizeof(buf), 0);
                if (n <= 0) break;
                body.append(buf, static_cast<size_t>(n));
            }
            if (body.size() > contentLen) body.resize(contentLen);
        }

        std::string resp = handleHttpRequest(method, target, headers, body);
        sendAll(client, resp);
    }

    void run(uint16_t port) {
        struct FinishRun {
            Impl* impl;
            ~FinishRun() {
                impl->running = false;
                impl->finished = true;
            }
        } finish{this};

        activePort = port;

#ifdef GEODE_IS_WINDOWS
        WSADATA wsa{};
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            setStatus("Winsock init failed", "WSAStartup error");
            return;
        }
#endif

        SocketHandle sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == kInvalidSocket) {
            setStatus("Failed to create socket", "socket() failed");
#ifdef GEODE_IS_WINDOWS
            WSACleanup();
#endif
            return;
        }
        listener = sock;

        int reuse = 1;
#ifdef GEODE_IS_WINDOWS
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char const*>(&reuse), sizeof(reuse));
#else
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        if (::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
            ::listen(sock, kListenBacklog) != 0) {
            setStatus(fmt::format("Port {} busy", port), fmt::format("Could not bind 127.0.0.1:{}", port));
            closeListener();
#ifdef GEODE_IS_WINDOWS
            WSACleanup();
#endif
            return;
        }

        running = true;
        setStatus(fmt::format("Active at 127.0.0.1:{}", port));
        log::info("[EditorMCP] Listening on http://127.0.0.1:{}/mcp", port);

        while (!stopping.load(std::memory_order_acquire)) {
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(sock, &readSet);
            timeval tv{0, 250000};
#ifdef GEODE_IS_WINDOWS
            int ready = ::select(0, &readSet, nullptr, nullptr, &tv);
#else
            int ready = ::select(sock + 1, &readSet, nullptr, nullptr, &tv);
#endif
            if (ready <= 0 || stopping.load(std::memory_order_acquire)) continue;

            SocketHandle client = ::accept(sock, nullptr, nullptr);
            if (client == kInvalidSocket) continue;
            handleClient(client);
            closeSocket(client);
        }

        closeListener();
#ifdef GEODE_IS_WINDOWS
        WSACleanup();
#endif
        setStatus("Stopped");
    }
};

namespace {
std::atomic<bool> s_mcpServerInitialized{false};
}

bool EditorMcpServer::isInitialized() {
    return s_mcpServerInitialized.load(std::memory_order_acquire);
}

EditorMcpServer& EditorMcpServer::get() {
    static auto* instance = []() {
        auto* srv = new EditorMcpServer();
        s_mcpServerInitialized.store(true, std::memory_order_release);
        return srv;
    }();
    return *instance;
}

EditorMcpServer::EditorMcpServer() : m_impl(std::make_shared<Impl>()) {}

EditorMcpServer::~EditorMcpServer() {
    stop();
}

bool EditorMcpServer::supported() const {
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_MACOS) || defined(GEODE_IS_LINUX)
    return true;
#else
    return false;
#endif
}

void EditorMcpServer::init() {
    if (m_initialized) return;
    m_initialized = true;
    restart();
}

void EditorMcpServer::requestStop() {
    m_initialized = false;
    m_impl->stopping.store(true, std::memory_order_release);
    m_impl->closeListener();
}

void EditorMcpServer::waitShutdown(std::chrono::steady_clock::time_point deadline) {
    requestStop();
    if (m_impl->worker.joinable()) {
        auto const now = std::chrono::steady_clock::now();
        auto const remaining = now < deadline
            ? std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now)
            : std::chrono::milliseconds(0);
        auto impl = m_impl;
        bool const joined = paimon::joinOrDetachWithWarning(
            m_impl->worker, remaining, "EditorMcpServer",
            [impl] { return impl->finished.load(std::memory_order_acquire); }
        );
        if (joined) m_impl->finished = true;
    } else {
        m_impl->finished = true;
    }
    m_impl->running = false;
}

void EditorMcpServer::shutdown() {
    waitShutdown(std::chrono::steady_clock::now() + std::chrono::milliseconds(1500));
}

void EditorMcpServer::start() {
    if (!supported() || m_impl->worker.joinable() || !m_impl->finished) return;
    uint16_t const p = configuredPort();
    m_impl->stopping = false;
    m_impl->running = false;
    m_impl->finished = false;
    m_impl->setStatus("Starting...");
    try {
        auto impl = m_impl;
        m_impl->worker = std::thread([impl, p]() { impl->run(p); });
    } catch (...) {
        m_impl->finished = true;
        m_impl->setStatus("Failed to start server thread", "std::thread launch failed");
    }
}

void EditorMcpServer::stop() {
    waitShutdown(std::chrono::steady_clock::now() + std::chrono::milliseconds(1500));
}

void EditorMcpServer::restart() {
    stop();
    m_initialized = true;
    if (!paimon::modules::isEnabled(kModuleID)) {
        m_impl->setStatus("Disabled");
        return;
    }
    start();
}

bool EditorMcpServer::isRunning() const {
    return m_impl->running.load(std::memory_order_acquire);
}

uint16_t EditorMcpServer::port() const {
    return m_impl->activePort.load(std::memory_order_acquire);
}

std::string EditorMcpServer::mcpUrl() const {
    return fmt::format("http://127.0.0.1:{}/mcp", port());
}

std::string EditorMcpServer::renderUrl() const {
    return fmt::format("http://127.0.0.1:{}/render.png", port());
}

ServerStats EditorMcpServer::stats() const {
    ServerStats s;
    s.running = isRunning();
    s.supported = supported();
    s.port = port();
    s.totalRequests = m_impl->totalRequests.load(std::memory_order_relaxed);
    s.toolCalls = m_impl->toolCalls.load(std::memory_order_relaxed);
    s.rendersServed = m_impl->rendersServed.load(std::memory_order_relaxed);
    s.objectsCreated = m_impl->objectsCreated.load(std::memory_order_relaxed);
    std::lock_guard lock(m_impl->mutex);
    s.status = m_impl->status;
    s.lastTool = m_impl->lastTool;
    s.lastError = m_impl->lastError;
    return s;
}

void EditorMcpServer::recordToolCall(std::string_view toolName) {
    m_impl->toolCalls.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard lock(m_impl->mutex);
    m_impl->lastTool = std::string(toolName);
}

void EditorMcpServer::recordRenderServed() {
    m_impl->rendersServed.fetch_add(1, std::memory_order_relaxed);
}

void EditorMcpServer::recordObjectsCreated(uint64_t count) {
    m_impl->objectsCreated.fetch_add(count, std::memory_order_relaxed);
}

} // namespace paimon::editormcp
