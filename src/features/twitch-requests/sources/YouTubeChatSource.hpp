#pragma once

#include "ChatSource.hpp"

#include <string>

namespace paimon::twitch {

// Reads the live chat the same way the watch page does: grab the innertube key
// and the first continuation token from /live_chat, then keep asking
// get_live_chat for the next page. No Google account, no API key, no quota.
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
