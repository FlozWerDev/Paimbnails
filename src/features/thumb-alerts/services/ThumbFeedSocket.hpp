#pragma once

#include <memory>
#include <string>

namespace paimon::net { class WebSocketClient; }

namespace paimon::thumbalerts {

// Live half of the feed: holds a socket open to the server so an upload lands
// as it happens instead of at the next poll. The poll stays on as the catch-up
// path for anything missed while the socket was down, and as the only path on
// the platforms where WebSocketClient has no implementation.
class ThumbFeedSocket {
public:
    static ThumbFeedSocket& get();

    void start();
    void stop();
    bool isConnected() const { return m_connected; }

private:
    ThumbFeedSocket() = default;

    void connect();
    void scheduleReconnect();
    void scheduleKeepalive();

    std::unique_ptr<paimon::net::WebSocketClient> m_socket;
    bool m_started = false;
    bool m_connecting = false;
    bool m_connected = false;
    int m_attempt = 0;
    // Invalidates the timers of a connection that has already been replaced.
    uint64_t m_generation = 0;
};

} // namespace paimon::thumbalerts
