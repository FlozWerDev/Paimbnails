#include "TikTokChatSource.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <cctype>
#include <matjson.hpp>
#include <string_view>
#include <utility>

using namespace geode::prelude;

namespace paimon::twitch {

namespace {

constexpr char const* kRelayFallback = "https://tiktok.eulerstream.com/webcast/fetch";
constexpr int kMaxFailures = 4;
constexpr int kLiveStatus = 2;

// Just enough protobuf to walk WebcastResponse: varints and length delimited
// fields, everything else is skipped.
class Wire {
public:
    explicit Wire(std::string_view data) : m_data(data) {}

    bool next(uint32_t& field, uint32_t& type) {
        uint64_t key = 0;
        if (!varint(key)) return false;
        field = static_cast<uint32_t>(key >> 3);
        type = static_cast<uint32_t>(key & 7);
        return true;
    }

    bool varint(uint64_t& out) {
        out = 0;
        for (int shift = 0; shift < 64 && m_pos < m_data.size(); shift += 7) {
            auto byte = static_cast<uint8_t>(m_data[m_pos++]);
            out |= static_cast<uint64_t>(byte & 0x7F) << shift;
            if (!(byte & 0x80)) return true;
        }
        return false;
    }

    bool bytes(std::string_view& out) {
        uint64_t length = 0;
        if (!varint(length) || length > m_data.size() - m_pos) return false;
        out = m_data.substr(m_pos, static_cast<size_t>(length));
        m_pos += static_cast<size_t>(length);
        return true;
    }

    bool skip(uint32_t type) {
        switch (type) {
            case 0: {
                uint64_t ignored = 0;
                return varint(ignored);
            }
            case 1: return advance(8);
            case 2: {
                std::string_view ignored;
                return bytes(ignored);
            }
            case 5: return advance(4);
            default: return false;
        }
    }

private:
    bool advance(size_t count) {
        if (m_data.size() - m_pos < count) return false;
        m_pos += count;
        return true;
    }

    std::string_view m_data;
    size_t m_pos = 0;
};

// First length delimited field with this tag.
std::string_view subMessage(std::string_view message, uint32_t tag) {
    Wire wire(message);
    uint32_t field = 0;
    uint32_t type = 0;
    while (wire.next(field, type)) {
        if (type == 2) {
            std::string_view payload;
            if (!wire.bytes(payload)) break;
            if (field == tag) return payload;
            continue;
        }
        if (!wire.skip(type)) break;
    }
    return {};
}

std::string relayUrl() {
    auto configured = Mod::get()->hasSetting("twitch-requests-tiktok-relay")
        ? Mod::get()->getSettingValue<std::string>("twitch-requests-tiktok-relay")
        : std::string{};
    if (configured.empty()) return kRelayFallback;
    return configured;
}

} // namespace

TikTokChatSource::TikTokChatSource(std::string channel, ChatCallbacks callbacks)
    : ChatSourceBase(std::move(callbacks)), m_channel(std::move(channel)) {}

bool TikTokChatSource::isOpen() const {
    return m_primed && !m_room.empty();
}

void TikTokChatSource::start() {
    m_primed = false;
    m_failures = 0;
    m_cursor.clear();

    if (looksLikeRoomId(m_channel)) {
        m_room = m_channel;
        poll();
        return;
    }
    resolveRoom();
}

void TikTokChatSource::resolveRoom() {
    status("Buscando el directo de @" + m_channel + "...");
    httpGet(
        "https://www.tiktok.com/api-live/user/room/?aid=1988&sourceType=54&uniqueId=" + m_channel,
        [this](bool ok, std::string body) {
            if (stopped()) return;
            if (!ok || body.empty()) {
                fail("TikTok no respondio; reintentando...");
                return;
            }

            auto parsed = matjson::parse(body);
            if (!parsed) {
                fail("TikTok devolvio algo que no entendemos");
                return;
            }
            auto user = parsed.unwrap()["data"]["user"];
            m_room = user["roomId"].asString().unwrapOr("");
            if (m_room.empty() || m_room == "0") {
                fail("No encontramos a @" + m_channel + " en TikTok");
                return;
            }
            if (auto live = user["status"].asInt(); live && live.unwrap() != kLiveStatus) {
                fail("@" + m_channel + " no esta en directo");
                return;
            }
            poll();
        });
}

void TikTokChatSource::poll() {
    if (stopped() || m_room.empty()) return;

    auto url = relayUrl() + "?room_id=" + m_room + "&client=ttlive-node&uuc=1";
    if (!m_cursor.empty()) url += "&cursor=" + m_cursor;

    httpGetBinary(url, [this](bool ok, std::vector<uint8_t> body) {
        if (stopped()) return;
        if (!ok || body.empty()) {
            if (++m_failures >= kMaxFailures) {
                fail("Se corto el chat de TikTok");
                return;
            }
            later(3.f, [this] { poll(); });
            return;
        }
        m_failures = 0;
        handleResponse(body);
    });
}

void TikTokChatSource::handleResponse(std::vector<uint8_t> const& body) {
    std::string_view response(reinterpret_cast<char const*>(body.data()), body.size());

    Wire wire(response);
    uint32_t field = 0;
    uint32_t type = 0;
    std::string cursor;
    float wait = 1.5f;
    std::vector<std::pair<std::string, std::string>> messages;

    while (wire.next(field, type)) {
        if (type == 2) {
            std::string_view payload;
            if (!wire.bytes(payload)) break;
            if (field == 2) {
                cursor.assign(payload);
            } else if (field == 1) {
                // Message { 1: type name, 2: payload }
                if (subMessage(payload, 1) != "WebcastChatMessage") continue;
                auto chat = subMessage(payload, 2);
                auto text = subMessage(chat, 3);
                if (text.empty()) continue;
                auto user = subMessage(chat, 2);
                auto handle = subMessage(user, 38);
                if (handle.empty()) handle = subMessage(user, 3);
                messages.emplace_back(
                    handle.empty() ? "TikTok" : std::string(handle), std::string(text));
            }
            continue;
        }
        if (type == 0) {
            uint64_t value = 0;
            if (!wire.varint(value)) break;
            // fetchInterval, in milliseconds
            if (field == 3 && value > 0) {
                wait = std::clamp(static_cast<float>(value) / 1000.f, 1.f, 5.f);
            }
            continue;
        }
        if (!wire.skip(type)) break;
    }

    // No cursor means the relay did not hand us a WebcastResponse; polling the
    // same window again would just replay it forever.
    if (cursor.empty()) {
        if (++m_failures >= kMaxFailures) {
            fail("El relay de TikTok dejo de responder");
            return;
        }
        later(3.f, [this] { poll(); });
        return;
    }
    m_cursor = std::move(cursor);

    if (m_primed) {
        for (auto& [requester, text] : messages) {
            deliver(std::move(requester), std::move(text));
            if (stopped()) return;
        }
    } else {
        // The relay replays what happened before we joined; skip that batch.
        m_primed = true;
        ready("Escuchando el chat de TikTok");
    }

    later(wait, [this] { poll(); });
}

} // namespace paimon::twitch
