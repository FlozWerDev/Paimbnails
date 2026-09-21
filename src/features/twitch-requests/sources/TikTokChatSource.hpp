#pragma once

#include "ChatSource.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace paimon::twitch {

// TikTok solo firma su webcast propio: el chat sale de un relay publico con la
// protobuf cruda; la URL es ajuste por si cae.
class TikTokChatSource final : public ChatSourceBase {
public:
    TikTokChatSource(std::string channel, ChatCallbacks callbacks);

    void start() override;
    bool isOpen() const override;

private:
    void resolveRoom();
    void poll();
    void handleResponse(std::vector<uint8_t> const& body);

    std::string m_channel;
    std::string m_room;
    std::string m_cursor;
    bool m_primed = false;  // first batch is backlog
    int m_failures = 0;
};

} // namespace paimon::twitch
