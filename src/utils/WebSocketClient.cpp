#include "WebSocketClient.hpp"

#include <atomic>
#include <cstdint>
#include <utility>

#ifdef GEODE_IS_WINDOWS
#include <Geode/Geode.hpp>
#include <Geode/utils/string.hpp>
#include "ThreadTracker.hpp"
#include "../core/BanGate.hpp"
#include "../core/RuntimeLifecycle.hpp"

#include <Windows.h>
#include <winhttp.h>

#include <array>
#include <algorithm>
#include <mutex>
#include <condition_variable>
#include <deque>
#endif

namespace paimon::net {

#ifdef GEODE_IS_WINDOWS

namespace {

constexpr size_t kMaxMessageSize = 512 * 1024;

std::wstring wide(std::string const& text) {
    return geode::utils::string::utf8ToWide(text);
}

} // namespace

struct WebSocketClient::Impl : std::enable_shared_from_this<Impl> {
    Options options;
    OpenCallback onOpen;
    MessageCallback onMessage;
    CloseCallback onClose;

    std::atomic_bool cancelled = false;
    std::atomic_bool done = false;
    std::atomic_bool open = false;
    std::uint64_t authorization = 0;
    // callbacks are main-thread only; workers queue gated deliveries instead
    // of running UI-owning callbacks on the network thread.
    std::atomic<HINTERNET> session = nullptr;
    std::atomic<HINTERNET> connection = nullptr;
    std::atomic<HINTERNET> request = nullptr;
    std::atomic<HINTERNET> socket = nullptr;
    std::mutex handleMutex;
    std::mutex queueMutex;
    std::condition_variable ready;
    std::deque<std::string> outgoing;
    size_t queuedBytes = 0;

    static std::mutex& registryMutex() {
        static auto* mutex = new std::mutex();
        return *mutex;
    }

    static auto& registry() {
        static auto* connections = new std::vector<std::weak_ptr<Impl>>;
        return *connections;
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
        // sender/closer worker only. a late handshake handle is published
        // under guard so cancellation can't miss (and leak) it.
        std::lock_guard lock(handleMutex);
        closeHandle(socket);
        closeHandle(request);
        closeHandle(connection);
        closeHandle(session);
    }

    bool publish(std::atomic<HINTERNET>& slot, HINTERNET handle) {
        std::lock_guard lock(handleMutex);
        if (done) {
            WinHttpCloseHandle(handle);
            return false;
        }
        slot = handle;
        return true;
    }

    void disconnect() {
        cancelled = true;
        open = false;
        onOpen = {};
        onMessage = {};
        onClose = {};
        {
            std::lock_guard lock(queueMutex);
            done = true;
            outgoing.clear();
            queuedBytes = 0;
        }
        ready.notify_all();
    }

    void finish(std::string reason) {
        open = false;
        {
            std::lock_guard lock(queueMutex);
            if (done.exchange(true)) return;
        }
        ready.notify_all();
        if (cancelled || paimon::isRuntimeShuttingDown()) return;
        auto self = shared_from_this();
        geode::Loader::get()->queueInMainThread([self, reason = std::move(reason)]() mutable {
            if (self->cancelled || paimon::isRuntimeShuttingDown()) return;
            auto cb = self->onClose;
            if (cb) cb(std::move(reason));
        });
    }

    bool enqueue(std::string const& message) {
        constexpr size_t kMaxQueuedMessages = 128;
        constexpr size_t kMaxQueuedBytes = 512 * 1024;
        if (message.size() > kMaxQueuedBytes) return false;
        {
            std::lock_guard lock(queueMutex);
            if (done || !open || outgoing.size() >= kMaxQueuedMessages ||
                message.size() > kMaxQueuedBytes - queuedBytes) return false;
            outgoing.push_back(message);
            queuedBytes += message.size();
        }
        ready.notify_one();
        return true;
    }

    void sendLoop() {
        // no queue lock spans winhttp: slow sends/closes never block the
        // game thread, including during reconnects.
        try {
            for (;;) {
                std::string message;
                {
                    std::unique_lock lock(queueMutex);
                    ready.wait(lock, [this] { return done || !outgoing.empty(); });
                    if (done) break;
                    message = std::move(outgoing.front());
                    outgoing.pop_front();
                    queuedBytes -= message.size();
                }
                auto handle = socket.load();
                if (!handle) break;
                auto result = WinHttpWebSocketSend(handle, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                    message.data(), static_cast<DWORD>(message.size()));
                if (result != NO_ERROR) {
                    finish(error("No se pudo enviar a", result));
                    break;
                }
            }
        } catch (...) {
            finish("WebSocket send failed");
        }
        closeHandles();
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
        if (!publish(session, newSession)) return;

        WinHttpSetTimeouts(newSession, 15000, 15000, 5000, 0);
        auto newConnection = WinHttpConnect(
            newSession, wide(options.host).c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!newConnection) return fail("No se pudo conectar con");
        if (!publish(connection, newConnection)) return;

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
        if (!publish(request, newRequest)) return;

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
        if (!publish(socket, newSocket)) return;
        {
            std::lock_guard lock(handleMutex);
            closeHandle(request);
        }
        if (done) return;

        open = true;
        if (!cancelled && !paimon::isRuntimeShuttingDown()) {
            auto self = shared_from_this();
            geode::Loader::get()->queueInMainThread([self] {
                if (self->cancelled || paimon::isRuntimeShuttingDown()) return;
                if (paimon::ban::networkAuthorization() != self->authorization) {
                    self->disconnect();
                    return;
                }
                auto cb = self->onOpen;
                if (cb) cb();
            });
        }

        std::array<char, 8192> buffer{};
        std::string message;
        while (!done) {
            DWORD bytesRead = 0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};
            DWORD result = WinHttpWebSocketReceive(
                newSocket,
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                &bytesRead,
                &type
            );
            if (done) return;
            if (result != NO_ERROR) {
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
                if (!cancelled && !paimon::isRuntimeShuttingDown()) {
                    auto self = shared_from_this();
                    geode::Loader::get()->queueInMainThread([self, message = std::move(message)]() mutable {
                        if (self->cancelled || paimon::isRuntimeShuttingDown()) return;
                        if (paimon::ban::networkAuthorization() != self->authorization) {
                            self->disconnect();
                            return;
                        }
                        auto cb = self->onMessage;
                        if (cb) cb(std::move(message));
                    });
                }
                message.clear();
            }
        }
        finish({});
    }
};

#else

struct WebSocketClient::Impl {};

#endif

WebSocketClient::WebSocketClient() = default;

WebSocketClient::~WebSocketClient() { disconnect(); }

bool WebSocketClient::connect(
    Options options,
    OpenCallback onOpen,
    MessageCallback onMessage,
    CloseCallback onClose
) {
#ifdef GEODE_IS_WINDOWS
    disconnect();
    if (paimon::isRuntimeShuttingDown() || paimon::ThreadTracker::get().isShuttingDown()) return false;
    auto const authorization = paimon::ban::networkAuthorization();
    if (!authorization) return false;
    auto impl = std::make_shared<Impl>();
    impl->authorization = authorization;
    impl->options = std::move(options);
    impl->onOpen = std::move(onOpen);
    impl->onMessage = std::move(onMessage);
    impl->onClose = std::move(onClose);
    {
        std::lock_guard<std::mutex> regLock(Impl::registryMutex());
        auto& registry = Impl::registry();
        std::erase_if(registry, [](auto const& weak) { return weak.expired(); });
        registry.push_back(impl);
    }
    m_impl = impl;
    // start the closer first: cancellation mid-handshake always has a worker
    // to release handles and wake the reader.
    try {
        if (!paimon::ThreadTracker::get().spawnWithPolicy(
                "PaimonWS Send",
                paimon::ThreadTracker::ShutdownPolicy::BestEffort,
                [impl] { impl->sendLoop(); })) {
            disconnect();
            return false;
        }
        if (!paimon::ThreadTracker::get().spawnWithPolicy(
                "PaimonWS Recv",
                paimon::ThreadTracker::ShutdownPolicy::BestEffort,
                [impl] {
                    try { impl->run(); }
                    catch (...) { impl->finish("WebSocket receive failed"); }
                })) {
            disconnect();
            return false;
        }
    } catch (...) {
        disconnect();
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
    if (m_impl) m_impl->disconnect();
    m_impl.reset();
#endif
}

bool WebSocketClient::send(std::string const& message) {
#ifdef GEODE_IS_WINDOWS
    if (m_impl && paimon::ban::networkAuthorization() != m_impl->authorization) {
        disconnect();
        return false;
    }
    return m_impl && m_impl->enqueue(message);
#else
    (void)message;
    return false;
#endif
}

bool WebSocketClient::isOpen() const {
#ifdef GEODE_IS_WINDOWS
    return m_impl && !m_impl->done && m_impl->open;
#else
    return false;
#endif
}

void WebSocketClient::shutdownAll() {
#ifdef GEODE_IS_WINDOWS
    std::vector<std::weak_ptr<Impl>> snapshot;
    {
        std::lock_guard<std::mutex> regLock(Impl::registryMutex());
        snapshot.swap(Impl::registry());
    }
    for (auto const& weak : snapshot) {
        if (auto impl = weak.lock()) impl->disconnect();
    }
#endif
}

} // namespace paimon::net
