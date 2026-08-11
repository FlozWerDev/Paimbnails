#include "ChatSource.hpp"

#include "KickChatSource.hpp"
#include "TikTokChatSource.hpp"
#include "TwitchIrcSource.hpp"
#include "YouTubeChatSource.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/MainThreadDelay.hpp"
#include "../../../utils/WebHelper.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ranges>

using namespace geode::prelude;

namespace paimon::twitch {

namespace {

// Every platform we read is a website first, so we ask like a browser.
constexpr char const* kUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/124.0.0.0 Safari/537.36";

std::string trimmed(std::string value) {
    auto first = std::ranges::find_if(value, [](unsigned char ch) { return !std::isspace(ch); });
    value.erase(value.begin(), first);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

std::string lowered(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool allDigits(std::string_view value) {
    return !value.empty() && std::ranges::all_of(value, [](unsigned char ch) {
        return std::isdigit(ch);
    });
}

// Last non-empty path segment of a url, without query or fragment.
std::string lastSegment(std::string_view value) {
    if (auto cut = value.find_first_of("?#"); cut != std::string_view::npos) {
        value = value.substr(0, cut);
    }
    while (!value.empty() && value.back() == '/') value.remove_suffix(1);
    if (auto slash = value.rfind('/'); slash != std::string_view::npos) {
        value = value.substr(slash + 1);
    }
    return std::string(value);
}

std::string keepChars(std::string value, std::string_view extra) {
    std::string result;
    for (unsigned char ch : value) {
        if (std::isalnum(ch) || extra.find(static_cast<char>(ch)) != std::string_view::npos) {
            result.push_back(static_cast<char>(ch));
        }
    }
    return result;
}

// youtube.com/watch?v=ID, youtu.be/ID, youtube.com/live/ID
std::string youtubeVideoId(std::string const& url) {
    auto pick = [&](size_t start) {
        std::string id;
        for (size_t i = start; i < url.size() && id.size() < 11; ++i) {
            char ch = url[i];
            if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-') {
                id.push_back(ch);
            } else {
                break;
            }
        }
        return id.size() == 11 ? id : std::string{};
    };

    if (auto pos = url.find("v="); pos != std::string::npos) {
        if (auto id = pick(pos + 2); !id.empty()) return id;
    }
    if (auto pos = url.find("/live/"); pos != std::string::npos) {
        if (auto id = pick(pos + 6); !id.empty()) return id;
    }
    if (auto pos = url.find("youtu.be/"); pos != std::string::npos) {
        if (auto id = pick(pos + 9); !id.empty()) return id;
    }
    return {};
}

} // namespace

char const* platformKey(Platform platform) {
    switch (platform) {
        case Platform::YouTube: return "youtube";
        case Platform::Kick: return "kick";
        case Platform::TikTok: return "tiktok";
        case Platform::Web: return "web";
        default: return "twitch";
    }
}

char const* platformName(Platform platform) {
    switch (platform) {
        case Platform::YouTube: return "YouTube";
        case Platform::Kick: return "Kick";
        case Platform::TikTok: return "TikTok";
        case Platform::Web: return "Web";
        default: return "Twitch";
    }
}

char const* platformFieldName(Platform platform) {
    switch (platform) {
        case Platform::YouTube: return "Canal o link del directo";
        case Platform::Kick: return "Canal de Kick";
        case Platform::TikTok: return "Usuario de TikTok";
        case Platform::Web: return "Pagina de requests";
        default: return "Usuario de Twitch";
    }
}

char const* platformPlaceholder(Platform platform) {
    switch (platform) {
        case Platform::YouTube: return "@tu_canal";
        case Platform::Kick: return "tu_canal";
        case Platform::TikTok: return "@tu_usuario";
        case Platform::Web: return "tu_usuario_de_gd";
        default: return "tu_usuario";
    }
}

Platform platformFromKey(std::string_view key) {
    for (int index = 0; index < kSelectableCount; ++index) {
        auto platform = static_cast<Platform>(index);
        if (key == platformKey(platform)) return platform;
    }
    return Platform::Twitch;
}

Platform platformFromIndex(int index) {
    if (index < 0 || index >= kSelectableCount) return Platform::Twitch;
    return static_cast<Platform>(index);
}

std::string normalizeChannel(Platform platform, std::string value) {
    value = trimmed(std::move(value));
    while (!value.empty() && (value.front() == '@' || value.front() == '#')) {
        value.erase(value.begin());
    }
    if (value.empty()) return {};

    switch (platform) {
        case Platform::Twitch: {
            value = lowered(std::move(value));
            if (value.contains('/')) value = lastSegment(value);
            auto clean = keepChars(value, "_");
            return clean == value ? clean : std::string{};
        }
        case Platform::Kick: {
            value = lowered(std::move(value));
            if (value.contains('/')) value = lastSegment(value);
            auto clean = keepChars(value, "_-");
            return clean == value ? clean : std::string{};
        }
        case Platform::YouTube: {
            auto lower = lowered(value);
            if (lower.contains("youtu")) {
                if (auto id = youtubeVideoId(value); !id.empty()) return "youtu.be/" + id;
                auto handle = lastSegment(value);
                if (handle == "live" || handle == "streams") {
                    // .../@handle/live -> drop the trailing page
                    auto trimmedUrl = value.substr(0, value.rfind('/'));
                    handle = lastSegment(trimmedUrl);
                }
                if (!handle.empty() && handle.front() == '@') handle.erase(handle.begin());
                return keepChars(handle, "_-.");
            }
            auto clean = keepChars(value, "_-.");
            return clean == value ? clean : std::string{};
        }
        case Platform::TikTok: {
            if (value.contains('/')) {
                auto segment = lastSegment(value);
                if (segment == "live") {
                    auto trimmedUrl = value.substr(0, value.rfind('/'));
                    segment = lastSegment(trimmedUrl);
                }
                value = segment;
                if (!value.empty() && value.front() == '@') value.erase(value.begin());
            }
            return keepChars(lowered(std::move(value)), "_-.");
        }
        case Platform::Web:
            return keepChars(lowered(std::move(value)), "_");
    }
    return {};
}

bool looksLikeRoomId(std::string_view channel) {
    return channel.size() >= 15 && allDigits(channel);
}

std::string channelLabel(Platform platform, std::string const& channel) {
    switch (platform) {
        case Platform::YouTube:
            return channel.contains('/') ? channel : "@" + channel;
        case Platform::Kick:
            return "kick/" + channel;
        case Platform::TikTok:
            return looksLikeRoomId(channel) ? "sala " + channel : "@" + channel;
        case Platform::Web:
            return "flozwer.org/request/" + channel;
        default:
            return "#" + channel;
    }
}

void ChatSourceBase::stop() {
    m_stopped = true;
}

bool ChatSourceBase::alive() const {
    return !m_stopped && !paimon::isRuntimeShuttingDown();
}

void ChatSourceBase::status(std::string text) const {
    if (alive() && m_callbacks.onStatus) m_callbacks.onStatus(std::move(text));
}

void ChatSourceBase::ready(std::string text) const {
    if (alive() && m_callbacks.onReady) m_callbacks.onReady(std::move(text));
}

void ChatSourceBase::deliver(std::string requester, std::string text) const {
    if (alive() && m_callbacks.onMessage) m_callbacks.onMessage(std::move(requester), std::move(text));
}

// One error per source. The transport goes down right away: the retry builds a
// new source, and we do not want two live connections during the backoff.
void ChatSourceBase::fail(std::string error) {
    if (m_stopped) return;
    this->stop();
    if (!paimon::isRuntimeShuttingDown() && m_callbacks.onError) {
        m_callbacks.onError(std::move(error));
    }
}

void ChatSourceBase::onMain(std::function<void()> work) {
    std::weak_ptr<uint8_t> life = m_life;
    Loader::get()->queueInMainThread([life, work = std::move(work)]() mutable {
        if (paimon::isRuntimeShuttingDown() || life.expired()) return;
        work();
    });
}

void ChatSourceBase::later(float seconds, std::function<void()> work) {
    std::weak_ptr<uint8_t> life = m_life;
    paimon::scheduleMainThreadDelay(seconds, [life, work = std::move(work)]() mutable {
        if (life.expired()) return;
        work();
    });
}

void ChatSourceBase::httpGet(
    std::string url,
    std::function<void(bool, std::string)> handler
) {
    std::weak_ptr<uint8_t> life = m_life;
    web::WebRequest request;
    request.userAgent(kUserAgent)
        .acceptEncoding("gzip, deflate")
        .followRedirects(true)
        .timeout(std::chrono::seconds(15));
    WebHelper::dispatch(std::move(request), "GET", url,
        [life, handler = std::move(handler)](web::WebResponse response) mutable {
            if (paimon::isRuntimeShuttingDown() || life.expired()) return;
            handler(response.ok(), response.string().unwrapOr(""));
        });
}

void ChatSourceBase::httpGetBinary(
    std::string url,
    std::function<void(bool, std::vector<uint8_t>)> handler
) {
    std::weak_ptr<uint8_t> life = m_life;
    web::WebRequest request;
    request.userAgent(kUserAgent)
        .acceptEncoding("gzip, deflate")
        .followRedirects(true)
        .timeout(std::chrono::seconds(15));
    WebHelper::dispatch(std::move(request), "GET", url,
        [life, handler = std::move(handler)](web::WebResponse response) mutable {
            if (paimon::isRuntimeShuttingDown() || life.expired()) return;
            handler(response.ok(), std::move(response).data());
        });
}

void ChatSourceBase::httpPostJson(
    std::string url,
    std::string body,
    std::function<void(bool, std::string)> handler
) {
    std::weak_ptr<uint8_t> life = m_life;
    web::WebRequest request;
    request.userAgent(kUserAgent)
        .acceptEncoding("gzip, deflate")
        .header("Content-Type", "application/json")
        .timeout(std::chrono::seconds(15))
        .bodyString(std::move(body));
    WebHelper::dispatch(std::move(request), "POST", url,
        [life, handler = std::move(handler)](web::WebResponse response) mutable {
            if (paimon::isRuntimeShuttingDown() || life.expired()) return;
            handler(response.ok(), response.string().unwrapOr(""));
        });
}

std::unique_ptr<ChatSource> makeChatSource(
    Platform platform,
    std::string channel,
    ChatCallbacks callbacks
) {
    switch (platform) {
        case Platform::YouTube:
            return std::make_unique<YouTubeChatSource>(std::move(channel), std::move(callbacks));
        case Platform::Kick:
            return std::make_unique<KickChatSource>(std::move(channel), std::move(callbacks));
        case Platform::TikTok:
            return std::make_unique<TikTokChatSource>(std::move(channel), std::move(callbacks));
        case Platform::Web:
            return nullptr;
        default:
            return std::make_unique<TwitchIrcSource>(std::move(channel), std::move(callbacks));
    }
}

} // namespace paimon::twitch
