#pragma once

#include "ChatSource.hpp"

#include <string>

namespace paimon::twitch {

// Lee el chat como la pagina de watch: clave innertube + continuation de
// /live_chat, sin cuenta de Google ni cuota.
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
