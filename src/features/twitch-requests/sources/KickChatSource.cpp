#include "KickChatSource.hpp"

#include "../../../utils/WebSocketClient.hpp"

#include <Geode/Geode.hpp>

#include <matjson.hpp>

using namespace geode::prelude;

namespace paimon::twitch {

namespace {

// Public Pusher app the kick.com frontend uses for chat.
constexpr char const* kPusherHost = "ws-us2.pusher.com";
constexpr char const* kPusherPath =
    "/app/32cbd69e4b950bf97679?protocol=7&client=js&version=8.4.0-rc2&flash=false";
constexpr float kSubscribeTimeout = 15.f;

// Kick sends emotes inline as [emote:1730794:emojiLol]; the digits inside would
// look like a level id to the parser, so they become the emote name.
std::string stripEmotes(std::string const& content) {
    std::string result;
    size_t pos = 0;
    while (pos < content.size()) {
        size_t open = content.find("[emote:", pos);
        if (open == std::string::npos) {
            result.append(content, pos, std::string::npos);
            break;
        }
        result.append(content, pos, open - pos);
        size_t close = content.find(']', open);
        if (close == std::string::npos) {
            result.append(content, open, std::string::npos);
            break;
        }
        size_t nameStart = content.rfind(':', close);
        if (nameStart != std::string::npos && nameStart > open + 6) {
            result.append(content, nameStart + 1, close - nameStart - 1);
        }
        pos = close + 1;
    }
    return result;
}

} // namespace

KickChatSource::KickChatSource(std::string channel, ChatCallbacks callbacks)
    : ChatSourceBase(std::move(callbacks)), m_channel(std::move(channel)) {}

KickChatSource::~KickChatSource() {
    if (m_socket) m_socket->disconnect();
}

void KickChatSource::start() {
    status("Buscando el chat de kick/" + m_channel + "...");
    resolveChatroom();
}

void KickChatSource::stop() {
    ChatSourceBase::stop();
    if (m_socket) m_socket->disconnect();
}

bool KickChatSource::isOpen() const {
    return m_socket && m_socket->isOpen();
}

void KickChatSource::resolveChatroom() {
    httpGet("https://kick.com/api/v2/channels/" + m_channel,
        [this](bool ok, std::string body) {
            if (stopped()) return;
            if (!ok || body.empty()) {
                fail("Kick no respondio; reintentando...");
                return;
            }

            auto parsed = matjson::parse(body);
            if (!parsed) {
                fail("Kick devolvio algo que no entendemos");
                return;
            }
            auto id = parsed.unwrap()["chatroom"]["id"].asInt();
            if (!id || id.unwrap() <= 0) {
                fail("No encontramos el canal kick/" + m_channel);
                return;
            }
            m_chatroom = id.unwrap();
            connectSocket();
        });
}

void KickChatSource::connectSocket() {
    if (stopped()) return;
    status("Conectando al chat de kick/" + m_channel + "...");

    m_socket = std::make_unique<paimon::net::WebSocketClient>();
    paimon::net::WebSocketClient::Options options;
    options.host = kPusherHost;
    options.path = kPusherPath;
    options.label = "Kick";

    bool const started = m_socket->connect(
        std::move(options),
        [this] {
            onMain([this] {
                if (stopped() || !m_socket) return;
                m_socket->send(fmt::format(
                    R"({{"event":"pusher:subscribe","data":{{"auth":"","channel":"chatrooms.{}.v2"}}}})",
                    m_chatroom));
            });
        },
        [this](std::string frame) {
            onMain([this, frame = std::move(frame)]() mutable {
                handleFrame(std::move(frame));
            });
        },
        [this](std::string error) {
            onMain([this, error = std::move(error)]() mutable {
                fail(error.empty() ? "Chat de Kick desconectado" : std::move(error));
            });
        }
    );
    if (!started) fail("El chat de Kick solo funciona en Windows");

    // Pusher accepts the subscribe silently when something is off, so give the
    // confirmation a deadline instead of sitting in "connecting" forever.
    later(kSubscribeTimeout, [this] {
        if (stopped() || m_listening) return;
        fail("Kick no confirmo el chat de kick/" + m_channel);
    });
}

void KickChatSource::handleFrame(std::string frame) {
    if (stopped()) return;

    auto parsed = matjson::parse(frame);
    if (!parsed) return;
    auto value = parsed.unwrap();
    auto event = value["event"].asString().unwrapOr("");
    if (event.empty()) return;

    if (event == "pusher:ping") {
        if (m_socket) m_socket->send(R"({"event":"pusher:pong","data":{}})");
        return;
    }
    if (event == "pusher:error") {
        fail("Kick rechazo la conexion al chat");
        return;
    }
    if (event == "pusher_internal:subscription_succeeded") {
        m_listening = true;
        ready("Escuchando el chat de kick/" + m_channel);
        return;
    }
    // Event name arrives as App\Events\ChatMessageEvent.
    if (event.find("ChatMessageEvent") == std::string::npos) return;

    // data is a JSON document inside a JSON string.
    if (auto data = value["data"].asString(); data) {
        handleChatMessage(data.unwrap());
    }
}

void KickChatSource::handleChatMessage(std::string const& payload) {
    auto parsed = matjson::parse(payload);
    if (!parsed) return;
    auto message = parsed.unwrap();

    auto content = message["content"].asString().unwrapOr("");
    if (content.empty()) return;
    auto requester = message["sender"]["username"].asString().unwrapOr("Kick");

    deliver(std::move(requester), stripEmotes(content));
}

} // namespace paimon::twitch
