#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace paimon::twitch {

enum class Platform { Twitch, YouTube, Kick, TikTok, Web };
// Chats con canal escrito: los que recorren los bucles de conexion.
constexpr int kPlatformCount = 4;
// Lo anterior mas la pagina web, que es lo que se puede elegir en la pantalla.
// La web no tiene canal ni ChatSource: la cuenta de GD ya dice cual es tu URL.
constexpr int kSelectableCount = 5;

char const* platformKey(Platform platform);
char const* platformName(Platform platform);
char const* platformFieldName(Platform platform);
char const* platformPlaceholder(Platform platform);
Platform platformFromKey(std::string_view key);
Platform platformFromIndex(int index);

// Cleans what the user typed: handle, slug, url or numeric room id.
std::string normalizeChannel(Platform platform, std::string value);
// A TikTok room id instead of a handle (they are long numbers).
bool looksLikeRoomId(std::string_view channel);
// How the channel reads in the status pill.
std::string channelLabel(Platform platform, std::string const& channel);

struct ChatCallbacks {
    std::function<void(std::string)> onStatus;               // working on it
    std::function<void(std::string)> onReady;                // reading chat now
    std::function<void(std::string, std::string)> onMessage;  // requester, text
    std::function<void(std::string)> onError;                // manager retries
};

// One live chat connection. Everything downstream only needs onMessage.
class ChatSource {
public:
    virtual ~ChatSource() = default;

    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool isOpen() const = 0;
};

// Shared plumbing: callbacks on the main thread, guarded so nothing fires after
// the source dies, plus the HTTP calls the polling platforms need.
class ChatSourceBase : public ChatSource {
public:
    void stop() override;

protected:
    explicit ChatSourceBase(ChatCallbacks callbacks) : m_callbacks(std::move(callbacks)) {}

    void status(std::string text) const;
    void ready(std::string text) const;
    void deliver(std::string requester, std::string text) const;
    void fail(std::string error);

    bool stopped() const { return m_stopped; }

    void onMain(std::function<void()> work);
    void later(float seconds, std::function<void()> work);

    void httpGet(std::string url, std::function<void(bool ok, std::string body)> handler);
    void httpGetBinary(
        std::string url, std::function<void(bool ok, std::vector<uint8_t> body)> handler);
    void httpPostJson(
        std::string url, std::string body,
        std::function<void(bool ok, std::string response)> handler);

private:
    bool alive() const;

    ChatCallbacks m_callbacks;
    bool m_stopped = false;
    std::shared_ptr<uint8_t> m_life = std::make_shared<uint8_t>(0);
};

std::unique_ptr<ChatSource> makeChatSource(
    Platform platform, std::string channel, ChatCallbacks callbacks);

} // namespace paimon::twitch
