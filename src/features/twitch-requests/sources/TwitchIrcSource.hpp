#pragma once

#include "ChatSource.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace paimon::net { class WebSocketClient; }

namespace paimon::twitch {

// Anonymous read-only IRC over WebSocket: any nick of the form justinfan<digits>
// is accepted without a token, so only the channel name is needed.
class TwitchIrcSource final : public ChatSourceBase {
public:
    TwitchIrcSource(std::string channel, ChatCallbacks callbacks);
    ~TwitchIrcSource() override;

    void start() override;
    void stop() override;
    bool isOpen() const override;

private:
    void handleOpen();
    void handleMessage(std::string message);
    void handleLine(std::string_view line);

    std::string m_channel;
    std::string m_nick;
    std::string m_buffer;
    bool m_joined = false;
    std::unique_ptr<paimon::net::WebSocketClient> m_socket;
};

} // namespace paimon::twitch
