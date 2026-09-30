#pragma once

#include <memory>
#include <string>

namespace paimon::net { class WebSocketClient; }

namespace paimon::thumbalerts {

// live half: socket push, poll as catch-up for downtime (and only path without websocketclient).
class ThumbFeedSocket {
public:
    static ThumbFeedSocket& get();

    void start();
    void stop();
    void resumeAfterConsent();

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
    // invalidates the timers of a connection that has already been replaced.
    uint64_t m_generation = 0;
};

} // namespace paimon::thumbalerts
