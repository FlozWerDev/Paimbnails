#pragma once

#include "ChatSource.hpp"

#include <string>

namespace paimon::twitch {

// reads chat like the watch page: innertube key + /live_chat continuation,
// no google account or quota.
class YouTubeChatSource final : public ChatSourceBase {
public:
    YouTubeChatSource(std::string channel, ChatCallbacks callbacks);

    void start() override;
    bool isOpen() const override;

private:
    void resolveVideo();
    void loadChatPage();
    void poll();
    void handlePoll(std::string const& body);

    std::string m_channel;
    std::string m_video;
    std::string m_key;
    std::string m_clientVersion;
    std::string m_continuation;
    bool m_primed = false;  // first page is history, not new requests
    int m_failures = 0;
};

} // namespace paimon::twitch
