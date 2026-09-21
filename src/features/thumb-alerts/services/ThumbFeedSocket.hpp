#pragma once

#include <memory>
#include <string>

namespace paimon::net { class WebSocketClient; }

namespace paimon::thumbalerts {

// Live half of the feed: socket push with the poll as catch-up for anything
// missed while down (and the only path where WebSocketClient is unimplemented).
class ThumbFeedSocket {
public:
    static ThumbFeedSocket& get();

    void start();
    void stop();

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
