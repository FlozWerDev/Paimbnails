#pragma once

#include "ChatSource.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace paimon::twitch {

// tiktok only signs its own webcast: chat comes from a public relay with the
// raw protobuf; the url is a fallback setting.
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
