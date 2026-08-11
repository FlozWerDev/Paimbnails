#include "YouTubeChatSource.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <matjson.hpp>

using namespace geode::prelude;

namespace paimon::twitch {

namespace {

constexpr int kMaxFailures = 4;

// First "key":"value" in the page, unescaped: every field we read here is ASCII.
std::string jsonField(std::string const& html, std::string_view key) {
    auto needle = std::string("\"") + std::string(key) + "\":\"";
    auto pos = html.find(needle);
    if (pos == std::string::npos) return {};
    pos += needle.size();
    auto end = html.find('"', pos);
    if (end == std::string::npos) return {};
    return html.substr(pos, end - pos);
}

std::string firstVideoId(std::string const& html) {
    auto id = jsonField(html, "videoId");
    return id.size() == 11 ? id : std::string{};
}

std::string runsText(matjson::Value const& message) {
    auto runs = message["runs"].asArray();
    if (!runs) return message["simpleText"].asString().unwrapOr("");

    std::string text;
    for (auto const& run : runs.unwrap()) {
        text += run["text"].asString().unwrapOr("");
        // Emojis come as a shortcut like :face-blue-smiling:; the label is enough.
        if (auto emoji = run["emoji"]["shortcuts"].asArray(); emoji && !emoji.unwrap().empty()) {
            text += emoji.unwrap().front().asString().unwrapOr("");
        }
    }
    return text;
}

} // namespace

YouTubeChatSource::YouTubeChatSource(std::string channel, ChatCallbacks callbacks)
    : ChatSourceBase(std::move(callbacks)), m_channel(std::move(channel)) {}

bool YouTubeChatSource::isOpen() const {
    return !m_continuation.empty() && m_primed;
}

void YouTubeChatSource::start() {
    m_primed = false;
    m_failures = 0;

    // A link already points at one video; a handle needs a lookup.
    if (auto slash = m_channel.rfind('/'); slash != std::string::npos) {
        m_video = m_channel.substr(slash + 1);
        loadChatPage();
        return;
    }
    resolveVideo();
}

void YouTubeChatSource::resolveVideo() {
    status("Buscando el directo de @" + m_channel + "...");
    httpGet("https://www.youtube.com/@" + m_channel + "/live",
        [this](bool ok, std::string body) {
            if (stopped()) return;
            if (!ok || body.empty()) {
                fail("YouTube no respondio; reintentando...");
                return;
            }
            m_video = firstVideoId(body);
            if (m_video.empty()) {
                fail("@" + m_channel + " no tiene un directo ahora");
                return;
            }
            loadChatPage();
        });
}

void YouTubeChatSource::loadChatPage() {
    status("Abriendo el chat del directo...");
    httpGet("https://www.youtube.com/live_chat?v=" + m_video + "&is_popout=1",
        [this](bool ok, std::string body) {
            if (stopped()) return;
            if (!ok || body.empty()) {
                fail("No pudimos abrir el chat del directo");
                return;
            }

            m_key = jsonField(body, "INNERTUBE_API_KEY");
            m_clientVersion = jsonField(body, "INNERTUBE_CLIENT_VERSION");
            m_continuation = jsonField(body, "continuation");
            if (m_key.empty() || m_continuation.empty()) {
                fail("Ese directo no tiene chat en vivo");
                return;
            }
            if (m_clientVersion.empty()) m_clientVersion = "2.20240101.00.00";
            poll();
        });
}

void YouTubeChatSource::poll() {
    if (stopped() || m_continuation.empty()) return;

    auto body = matjson::makeObject({
        {"context", matjson::makeObject({
            {"client", matjson::makeObject({
                {"clientName", "WEB"},
                {"clientVersion", m_clientVersion},
            })},
        })},
        {"continuation", m_continuation},
    });

    httpPostJson(
        "https://www.youtube.com/youtubei/v1/live_chat/get_live_chat?key=" + m_key
            + "&prettyPrint=false",
        body.dump(matjson::NO_INDENTATION),
        [this](bool ok, std::string response) {
            if (stopped()) return;
            if (!ok || response.empty()) {
                if (++m_failures >= kMaxFailures) {
                    fail("Se corto el chat de YouTube");
                    return;
                }
                later(4.f, [this] { poll(); });
                return;
            }
            m_failures = 0;
            handlePoll(response);
        });
}

void YouTubeChatSource::handlePoll(std::string const& body) {
    auto parsed = matjson::parse(body);
    if (!parsed) {
        if (++m_failures >= kMaxFailures) {
            fail("YouTube devolvio algo que no entendemos");
            return;
        }
        later(5.f, [this] { poll(); });
        return;
    }

    auto chat = parsed.unwrap()["continuationContents"]["liveChatContinuation"];
    if (!chat.isObject()) {
        fail("El directo termino o cerro el chat");
        return;
    }

    std::string next;
    float wait = 5.f;
    if (auto continuations = chat["continuations"].asArray(); continuations) {
        for (auto const& entry : continuations.unwrap()) {
            if (!entry.isObject()) continue;
            // The wrapper key changes (invalidation/timed/reload), the shape does not.
            for (auto const& data : entry) {
                if (auto token = data["continuation"].asString(); token) next = token.unwrap();
                if (auto timeout = data["timeoutMs"].asInt(); timeout) {
                    wait = std::clamp(static_cast<float>(timeout.unwrap()) / 1000.f, 2.f, 12.f);
                }
            }
        }
    }

    if (m_primed) {
        if (auto actions = chat["actions"].asArray(); actions) {
            for (auto const& action : actions.unwrap()) {
                auto item = action["addChatItemAction"]["item"]["liveChatTextMessageRenderer"];
                if (!item.isObject()) continue;
                auto author = runsText(item["authorName"]);
                auto text = runsText(item["message"]);
                if (text.empty()) continue;
                deliver(author.empty() ? "YouTube" : std::move(author), std::move(text));
                if (stopped()) return;
            }
        }
    } else {
        // The first page is the backlog; the queue only wants what comes next.
        m_primed = true;
        ready("Escuchando el chat de YouTube");
    }

    if (next.empty()) {
        fail("YouTube corto el chat");
        return;
    }
    m_continuation = std::move(next);
    later(wait, [this] { poll(); });
}

} // namespace paimon::twitch
