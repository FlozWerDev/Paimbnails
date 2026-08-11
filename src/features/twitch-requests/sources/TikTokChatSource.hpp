#pragma once

#include "ChatSource.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace paimon::twitch {

// TikTok's own webcast endpoints refuse anything unsigned, so the chat comes
// from a public relay that returns the raw WebcastResponse protobuf. The room id
// is still resolved against tiktok.com, and the relay url is a setting so it can
// be swapped when it breaks.
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
