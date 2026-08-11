#include "WebSocketClient.hpp"

#include <atomic>
#include <utility>

#ifdef GEODE_IS_WINDOWS
#include <Geode/Geode.hpp>
#include <Geode/utils/string.hpp>

#include <Windows.h>
#include <winhttp.h>

#include <array>
#include <limits>
#include <mutex>
#include <thread>
#endif

namespace paimon::net {

#ifdef GEODE_IS_WINDOWS

namespace {

constexpr size_t kMaxMessageSize = 512 * 1024;

std::wstring wide(std::string const& text) {
    return geode::utils::string::utf8ToWide(text);
}

} // namespace

struct WebSocketClient::Impl {
    Options options;
    OpenCallback onOpen;
    MessageCallback onMessage;
    CloseCallback onClose;

    std::atomic_bool stopping = false;
    std::atomic_bool open = false;
    std::atomic<HINTERNET> session = nullptr;
    std::atomic<HINTERNET> connection = nullptr;
    std::atomic<HINTERNET> request = nullptr;
    std::atomic<HINTERNET> socket = nullptr;
    std::mutex sendMutex;
    std::thread worker;

    ~Impl() {
        disconnect();
    }

    std::string error(char const* message, DWORD code = GetLastError()) const {
        return options.label.empty()
            ? message + std::string(" (") + std::to_string(code) + ")"
            : message + std::string(" ") + options.label + " (" + std::to_string(code) + ")";
    }

    static void closeHandle(std::atomic<HINTERNET>& slot) {
        if (auto handle = slot.exchange(nullptr)) WinHttpCloseHandle(handle);
    }

    void closeHandles() {
        std::lock_guard lock(sendMutex);
        closeHandle(socket);
        closeHandle(request);
        closeHandle(connection);
        closeHandle(session);
    }

    void disconnect() {
        stopping = true;
        open = false;
        closeHandles();
        if (worker.joinable()) worker.join();
    }

    void finish(std::string reason) {
        open = false;
        closeHandles();
        if (!stopping && onClose) onClose(std::move(reason));
    }

    void run() {
        auto fail = [this](char const* message) {
            finish(error(message));
        };

        auto newSession = WinHttpOpen(
            L"Paimbnails Level Requests",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0
        );
        if (!newSession) return fail("No se pudo iniciar WinHTTP para");
        session = newSession;
        if (stopping) return finish({});

        WinHttpSetTimeouts(newSession, 15000, 15000, 15000, 0);
        auto newConnection = WinHttpConnect(
            newSession, wide(options.host).c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!newConnection) return fail("No se pudo conectar con");
        connection = newConnection;
        if (stopping) return finish({});

        auto newRequest = WinHttpOpenRequest(
            newConnection,
            L"GET",
            wide(options.path).c_str(),
            nullptr,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE
        );
        if (!newRequest) return fail("No se pudo crear el WebSocket de");
        request = newRequest;

        if (!WinHttpSetOption(newRequest, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)) {
            return fail("No acepto el upgrade WebSocket");
        }
        for (auto const& [name, value] : options.headers) {
            auto header = wide(name + ": " + value);
            if (!WinHttpAddRequestHeaders(
                    newRequest,
                    header.c_str(),
                    static_cast<DWORD>(header.size()),
                    WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) {
                return fail("No pudo agregar las credenciales de");
            }
        }
        if (!WinHttpSendRequest(
            newRequest,
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0
        )) {
            return fail("No se pudo enviar el handshake a");
        }
        if (!WinHttpReceiveResponse(newRequest, nullptr)) {
            return fail("No hubo respuesta al handshake de");
        }

        auto newSocket = WinHttpWebSocketCompleteUpgrade(newRequest, 0);
        if (!newSocket) return fail("No se pudo completar el WebSocket de");
        socket = newSocket;
        closeHandle(request);
        if (stopping) return finish({});

        open = true;
        if (onOpen) onOpen();

        std::array<char, 8192> buffer{};
        std::string message;
        while (!stopping) {
            DWORD bytesRead = 0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};
            DWORD result = WinHttpWebSocketReceive(
                newSocket,
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                &bytesRead,
                &type
            );
            if (result != NO_ERROR) {
                if (stopping) return finish({});
                return finish(error("Se corto la conexion con", result));
            }
            if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) return finish({});
            if (type == WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE
                || type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) {
                message.clear();
                continue;
            }

            message.append(buffer.data(), bytesRead);
            if (message.size() > kMaxMessageSize) {
                return finish(error("Mensaje demasiado grande de", 0));
            }
            if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
                if (onMessage) onMessage(std::move(message));
                message.clear();
            }
        }
        finish({});
    }
};

#else

struct WebSocketClient::Impl {};

#endif

WebSocketClient::WebSocketClient() : m_impl(std::make_unique<Impl>()) {}

WebSocketClient::~WebSocketClient() = default;

bool WebSocketClient::connect(
    Options options,
    OpenCallback onOpen,
    MessageCallback onMessage,
    CloseCallback onClose
) {
#ifdef GEODE_IS_WINDOWS
    m_impl->disconnect();
    m_impl->stopping = false;
    m_impl->options = std::move(options);
    m_impl->onOpen = std::move(onOpen);
    m_impl->onMessage = std::move(onMessage);
    m_impl->onClose = std::move(onClose);
    try {
        m_impl->worker = std::thread([impl = m_impl.get()] { impl->run(); });
    } catch (...) {
        return false;
    }
    return true;
#else
    (void)options;
    (void)onOpen;
    (void)onMessage;
    (void)onClose;
    return false;
#endif
}

void WebSocketClient::disconnect() {
#ifdef GEODE_IS_WINDOWS
    m_impl->disconnect();
#endif
}

bool WebSocketClient::send(std::string const& message) {
#ifdef GEODE_IS_WINDOWS
    if (message.size() > std::numeric_limits<DWORD>::max()) return false;
    std::lock_guard lock(m_impl->sendMutex);
    auto handle = m_impl->socket.load();
    if (!m_impl->open || !handle) return false;
    return WinHttpWebSocketSend(
        handle,
        WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
        const_cast<char*>(message.data()),
        static_cast<DWORD>(message.size())
    ) == NO_ERROR;
#else
    (void)message;
    return false;
#endif
}

bool WebSocketClient::isOpen() const {
#ifdef GEODE_IS_WINDOWS
    return m_impl->open;
#else
    return false;
#endif
}

} // namespace paimon::net
