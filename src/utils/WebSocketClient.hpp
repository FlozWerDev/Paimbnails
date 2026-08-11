#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace paimon::net {

// WinHTTP WebSocket client over TLS. Only Windows has an implementation;
// elsewhere connect() returns false so the caller can report it.
class WebSocketClient {
public:
    struct Options {
        std::string host;
        std::string path = "/";  // may carry a query string
        std::string label;       // shown in error messages ("Twitch", "Kick", ...)
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
    void disconnect();
    bool send(std::string const& message);
    bool isOpen() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace paimon::net
