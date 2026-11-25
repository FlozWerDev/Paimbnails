#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace paimon::net {

// winhttp websocket client over tls. only windows has an implementation;
// elsewhere connect() returns false so the caller can report it.
class WebSocketClient {
public:
    struct Options {
        std::string host;
        std::string path = "/";  // may carry a query string
        std::string label;       // shown in error messages ("twitch", "kick", ...)
        std::vector<std::pair<std::string, std::string>> headers;
    };

    using OpenCallback = std::function<void()>;
    using MessageCallback = std::function<void(std::string)>;
    using CloseCallback = std::function<void(std::string)>;

    WebSocketClient();
    ~WebSocketClient();

    WebSocketClient(WebSocketClient const&) = delete;
    WebSocketClient& operator=(WebSocketClient const&) = delete;

    bool connect(
        Options options,
        OpenCallback onOpen,
        MessageCallback onMessage,
        CloseCallback onClose
    );
    // main-thread api. callbacks are delivered on the main thread; disconnect
    // invalidates queued callbacks without waiting for network activity.
    void disconnect();
    // true = accepted into the bounded fifo, not acknowledged by the peer.
    bool send(std::string const& message);
    bool isOpen() const;
    static void shutdownAll();

private:
    struct Impl;
    std::shared_ptr<Impl> m_impl;
};

} // namespace paimon::net
