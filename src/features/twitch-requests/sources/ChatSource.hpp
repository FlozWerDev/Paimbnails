#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace paimon::twitch {

enum class Platform { Twitch, YouTube, Kick, TikTok, Web };
// chats with a typed channel: the ones walking the reconnect loops.
constexpr int kPlatformCount = 4;
// the above plus the web page, which is what the screen offers.
// web has no channel or chatsource: the gd account already names your url.
constexpr int kSelectableCount = 5;

char const* platformKey(Platform platform);
char const* platformName(Platform platform);
char const* platformFieldName(Platform platform);
char const* platformPlaceholder(Platform platform);
Platform platformFromKey(std::string_view key);
Platform platformFromIndex(int index);

// cleans what the user typed: handle, slug, url or numeric room id.
std::string normalizeChannel(Platform platform, std::string value);
// a tiktok room id instead of a handle (they are long numbers).
bool looksLikeRoomId(std::string_view channel);
// how the channel reads in the status pill.
std::string channelLabel(Platform platform, std::string const& channel);

struct ChatMessage {
    std::string requester;
    std::string text;
    std::string messageID;
    std::string userID;
    std::string rewardID;
};

struct ChatCallbacks {
    std::function<void(std::string)> onStatus;               // working on it
    std::function<void(std::string)> onReady;                // reading chat now
    std::function<void(ChatMessage)> onMessage;
    std::function<void(std::string)> onError;                // manager retries
};

// one live chat connection. everything downstream only needs onmessage.
class ChatSource {
public:
    virtual ~ChatSource() = default;

    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool isOpen() const = 0;
};

// shared plumbing: callbacks on the main thread, guarded so nothing fires after
// the source dies, plus the http calls the polling platforms need.
class ChatSourceBase : public ChatSource {
public:
    void stop() override;

protected:
    explicit ChatSourceBase(ChatCallbacks callbacks) : m_callbacks(std::move(callbacks)) {}

    void status(std::string text) const;
    void ready(std::string text) const;
    void deliver(std::string requester, std::string text) const;
    void deliver(ChatMessage message) const;
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
