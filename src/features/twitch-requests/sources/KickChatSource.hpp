#pragma once

#include "ChatSource.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace paimon::net { class WebSocketClient; }

namespace paimon::twitch {

// Kick's own web client reads chat through Pusher, and the subscription is
// public: resolve the chatroom id over HTTP, then subscribe to
// chatrooms.<id>.v2 with an empty auth.
class KickChatSource final : public ChatSourceBase {
public:
    KickChatSource(std::string channel, ChatCallbacks callbacks);
    ~KickChatSource() override;

    void start() override;
    void stop() override;
    bool isOpen() const override;

private:
    void resolveChatroom();
    void connectSocket();
    void handleFrame(std::string frame);
    void handleChatMessage(std::string const& payload);

    std::string m_channel;
    int64_t m_chatroom = 0;
    bool m_listening = false;
    std::unique_ptr<paimon::net::WebSocketClient> m_socket;
};

} // namespace paimon::twitch
