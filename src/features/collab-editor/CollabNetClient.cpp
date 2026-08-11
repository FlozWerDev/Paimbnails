#include "CollabNetClient.hpp"

#include "../../utils/WebHelper.hpp"

#include <Geode/Geode.hpp>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <thread>

using namespace geode::prelude;

namespace paimon::collab {

namespace {
// Covers the short host-startup/reconnect window without hiding a bad room code.
constexpr int kJoinMaxRetries = 3;
constexpr int kJoinRetryMs = 2500;
constexpr int kHostReconnectMaxRetries = 40;
constexpr int kHostReconnectRetryMs = 2500;
constexpr int kEmptyPollDelayMs = 50;

std::string normalizeBaseUrl(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    while (!value.empty() && value.back() == '/') value.pop_back();
    return value;
}

std::string normalizeRoomCode(std::string const& value) {
    std::string out;
    out.reserve(value.size());
    for (unsigned char ch : value) {
        if (std::isalnum(ch)) out.push_back(static_cast<char>(std::toupper(ch)));
    }
    return out;
}
}

CollabNetClient::~CollabNetClient() {
    // Invalidate callbacks before stop() dispatches its best-effort leave.
    m_lifetime.reset();
    stop();
    m_onMessage = {};
    m_onState = {};
}

void CollabNetClient::setCallbacks(MessageCb onMessage, StateCb onState) {
    m_onMessage = std::move(onMessage);
    m_onState = std::move(onState);
}

std::string CollabNetClient::apiUrl(std::string const& suffix) const {
    return m_base + suffix;
}

void CollabNetClient::start(std::string baseUrl, std::string roomCode, std::string username,
                            PeerAppearance appearance, ConnectMode mode) {
    beginStart(std::move(baseUrl), std::move(roomCode), std::move(username),
               std::move(appearance), mode, false);
}

void CollabNetClient::beginStart(std::string baseUrl, std::string roomCode, std::string username,
                                 PeerAppearance appearance, ConnectMode mode,
                                 bool preserveResumeToken) {
    std::string resumeToken = preserveResumeToken ? m_resumeToken : "";
    // Host recovery keeps the old room alive until create-room replaces its session.
    stopInternal(!(preserveResumeToken && mode == ConnectMode::Create));
    m_base = normalizeBaseUrl(std::move(baseUrl));
    m_room = normalizeRoomCode(roomCode);
    m_user = std::move(username);
    m_appearance = std::move(appearance);
    m_resumeToken = std::move(resumeToken);
    m_mode = mode;
    m_active = true;
    m_pollFailures = 0;
    m_joinRetries = 0;
    m_hostReconnectRetries = 0;
    ++m_gen;
    uint64_t gen = m_gen;
    auto lifetime = std::weak_ptr<uint8_t>(m_lifetime);
    if (m_onState) {
        auto onState = m_onState;
        onState(ConnState::Connecting, "Conectando...");
        if (lifetime.expired() || !m_active || gen != m_gen) return;
    }
    if (mode == ConnectMode::Create) {
        doCreate();
    } else {
        doJoin();
    }
}

void CollabNetClient::stop() {
    stopInternal(true);
}

void CollabNetClient::stopInternal(bool notifyServer) {
    bool wasJoined = m_joined;
    std::string base = m_base;
    std::string room = m_room;
    int clientId = m_clientId;
    std::string sessionToken = m_sessionToken;

    m_active = false;
    m_joined = false;
    m_clientId = 0;
    m_sessionToken.clear();
    m_resumeToken.clear();
    m_resyncInFlight = false;
    m_pollFailures = 0;
    m_joinRetries = 0;
    m_hostReconnectRetries = 0;
    m_stateRequestsInFlight.clear();
    m_pendingStateRequests.clear();
    ++m_gen; // Invalidate in-flight callbacks.

    // Best-effort leave so the server frees the slot promptly.
    if (notifyServer && wasJoined && clientId > 0 && !base.empty()) {
        auto body = matjson::makeObject({
            {"room", room},
            {"client", static_cast<int64_t>(clientId)},
        });
        auto req = web::WebRequest();
        req.header("Content-Type", "application/json");
        req.header("Authorization", "Bearer " + sessionToken);
        req.bodyString(body.dump(matjson::NO_INDENTATION));
        WebHelper::dispatch(std::move(req), "POST", base + "/api/leave",
            [](web::WebResponse) {});
    }
}

void CollabNetClient::restart(ConnectMode mode) {
    if (m_base.empty() || m_room.empty()) return;
    // Copy because start() moves its arguments into these members.
    beginStart(std::string(m_base), std::string(m_room), std::string(m_user),
               m_appearance, mode, true);
}

void CollabNetClient::closeRoom() {
    if (!m_active || !m_joined || m_clientId == 0 || m_base.empty()) return;

    auto body = matjson::makeObject({
        {"room", m_room},
        {"client", static_cast<int64_t>(m_clientId)},
    });
    auto req = web::WebRequest();
    req.header("Content-Type", "application/json");
    req.header("Authorization", "Bearer " + m_sessionToken);
    req.bodyString(body.dump(matjson::NO_INDENTATION));
    WebHelper::dispatch(std::move(req), "POST", apiUrl("/api/close-room"),
        [](web::WebResponse) {});

    // Server closes the room; invalidate local state so poll/ops/leave stop.
    m_active = false;
    m_joined = false;
    m_clientId = 0;
    m_sessionToken.clear();
    m_resumeToken.clear();
    m_resyncInFlight = false;
    m_stateRequestsInFlight.clear();
    m_pendingStateRequests.clear();
    ++m_gen;
}

bool CollabNetClient::isOpen() const {
    return m_active && m_joined;
}

void CollabNetClient::requestResync() {
    if (!isOpen() || m_resyncInFlight) return;
    m_resyncInFlight = true;
    uint64_t gen = m_gen;
    auto lifetime = std::weak_ptr<uint8_t>(m_lifetime);
    auto body = matjson::makeObject({
        {"room", m_room},
        {"client", static_cast<int64_t>(m_clientId)},
    });
    auto req = web::WebRequest();
    req.timeout(std::chrono::seconds(20));
    req.header("Content-Type", "application/json");
    req.header("Authorization", "Bearer " + m_sessionToken);
    req.bodyString(body.dump(matjson::NO_INDENTATION));
    WebHelper::dispatch(std::move(req), "POST", apiUrl("/api/resync"),
        [this, lifetime, gen](web::WebResponse) {
            if (lifetime.expired() || gen != m_gen) return;
            m_resyncInFlight = false;
        });
}

void CollabNetClient::onJoinLikeSuccess(matjson::Value value) {
    auto lifetime = std::weak_ptr<uint8_t>(m_lifetime);
    uint64_t gen = m_gen;
    std::string sessionToken = value["sessionToken"].asString().unwrapOr("");
    if (sessionToken.size() < 32) {
        emitError("upgrade_required", "El servidor no entrego una sesion segura");
        return;
    }
    m_sessionToken = std::move(sessionToken);
    if (auto resume = value["resumeToken"].asString(); resume && !resume.unwrap().empty()) {
        m_resumeToken = resume.unwrap();
    }
    m_clientId = static_cast<int>(value["clientId"].asInt().unwrapOr(0));
    m_joined = true;
    m_pollFailures = 0;
    m_joinRetries = 0;
    m_hostReconnectRetries = 0;
    value["t"] = "join_ok";
    bool isHost = value.contains("isHost") && value["isHost"].asBool().unwrapOr(false);
    log::info("[Collab] joined room={} clientId={} isHost={}", m_room, m_clientId, isHost);
    if (m_onState) {
        auto onState = m_onState;
        onState(ConnState::Connected, "Conectado");
        if (lifetime.expired() || !m_active || gen != m_gen) return;
    }
    if (m_onMessage) {
        auto onMessage = m_onMessage;
        onMessage(value);
        if (lifetime.expired() || !m_active || gen != m_gen) return;
    }
    poll();
}

void CollabNetClient::emitError(std::string const& code, std::string const& message) {
    auto lifetime = std::weak_ptr<uint8_t>(m_lifetime);
    auto onMessage = m_onMessage;
    auto onState = m_onState;
    m_active = false;
    if (onMessage) {
        onMessage(matjson::makeObject({
            {"t", "error"}, {"code", code}, {"message", message},
        }));
        if (lifetime.expired()) return;
    }
    if (onState) onState(ConnState::Disconnected, message);
}

void CollabNetClient::doJoin() {
    uint64_t gen = m_gen;
    auto lifetime = std::weak_ptr<uint8_t>(m_lifetime);

    auto body = matjson::makeObject({
        {"roomCode", m_room},
        {"username", m_user},
        {"protocol", static_cast<int64_t>(kProtocolVersion)},
    });
    body["accountID"] = static_cast<int64_t>(m_appearance.accountID);
    body["iconID"] = static_cast<int64_t>(m_appearance.iconID);
    body["iconType"] = static_cast<int64_t>(m_appearance.iconType);
    body["color1"] = static_cast<int64_t>(m_appearance.color1);
    body["color2"] = static_cast<int64_t>(m_appearance.color2);
    body["glowColor"] = static_cast<int64_t>(m_appearance.glowColor);
    body["glowEnabled"] = m_appearance.glowEnabled;
    if (m_appearance.hasCustomCursor && !m_appearance.cursorData.empty()) {
        body["hasCustomCursor"] = true;
        body["cursorData"] = m_appearance.cursorData;
        body["cursorScale"] = static_cast<double>(m_appearance.cursorScale);
        body["cursorOpacity"] = static_cast<int64_t>(m_appearance.cursorOpacity);
    }

    auto req = web::WebRequest();
    // Allow slow VPS startup or a temporarily busy host.
    req.timeout(std::chrono::seconds(45));
    req.header("Content-Type", "application/json");
    req.bodyString(body.dump(matjson::NO_INDENTATION));

    log::info("[Collab] join room={} user={} ...", m_room, m_user);
    WebHelper::dispatch(std::move(req), "POST", apiUrl("/api/join"),
        [this, lifetime, gen](web::WebResponse res) {
            if (lifetime.expired() || !m_active || gen != m_gen) return;

            auto parsed = matjson::parse(res.string().unwrapOr(""));
            if (res.code() == 200 && parsed) {
                log::info("[Collab] join ok (HTTP 200)");
                onJoinLikeSuccess(parsed.unwrap());
                return;
            }

            log::warn("[Collab] join failed HTTP {} body={}", res.code(), res.string().unwrapOr(""));
            std::string message = "No se pudo unir a la sala";
            std::string code = "join_failed";
            if (parsed) {
                auto v = parsed.unwrap();
                if (v.contains("error")) {
                    code = v["error"]["code"].asString().unwrapOr(code);
                    message = v["error"]["message"].asString().unwrapOr(message);
                }
            }

            // Join never auto-creates; retry startup/reconnect 404s first.
            if (res.code() == 404 && code == "room_not_found") {
                if (m_joinRetries < kJoinMaxRetries) {
                    ++m_joinRetries;
                    if (m_onState) {
                        auto onState = m_onState;
                        onState(ConnState::Connecting, fmt::format(
                            "La sala aun no aparece; reintentando ({}/{})...",
                            m_joinRetries, kJoinMaxRetries));
                        if (lifetime.expired() || !m_active || gen != m_gen) return;
                    }
                    scheduleJoinRetry(gen, kJoinRetryMs);
                    return;
                }
                emitError("room_not_found",
                    "Esa sala no existe. Revisa el codigo con el host o crea una sala nueva.");
                return;
            }

            if (res.code() == 409 && code == "host_reconnecting") {
                if (m_hostReconnectRetries < kHostReconnectMaxRetries) {
                    ++m_hostReconnectRetries;
                    if (m_onState) {
                        auto onState = m_onState;
                        onState(ConnState::Connecting, fmt::format(
                            "El host se esta reconectando ({}/{})...",
                            m_hostReconnectRetries, kHostReconnectMaxRetries));
                        if (lifetime.expired() || !m_active || gen != m_gen) return;
                    }
                    scheduleJoinRetry(gen, kHostReconnectRetryMs);
                    return;
                }
                emitError("host_reconnecting",
                    "El host no pudo volver a la sala. Pidele que cree una nueva.");
                return;
            }

            emitError(code, message);
        });
}

void CollabNetClient::doCreate() {
    uint64_t gen = m_gen;
    auto lifetime = std::weak_ptr<uint8_t>(m_lifetime);

    // Send an empty initial snapshot: the manager streams the host's editor
    // objects afterwards via the bulk /api/seed path.
    auto body = matjson::makeObject({
        {"roomCode", m_room},
        {"username", m_user},
        {"protocol", static_cast<int64_t>(kProtocolVersion)},
        {"initialObjects", matjson::Value::array()},
    });
    if (!m_resumeToken.empty()) body["resumeToken"] = m_resumeToken;
    body["accountID"] = static_cast<int64_t>(m_appearance.accountID);
    body["iconID"] = static_cast<int64_t>(m_appearance.iconID);
    body["iconType"] = static_cast<int64_t>(m_appearance.iconType);
    body["color1"] = static_cast<int64_t>(m_appearance.color1);
    body["color2"] = static_cast<int64_t>(m_appearance.color2);
    body["glowColor"] = static_cast<int64_t>(m_appearance.glowColor);
    body["glowEnabled"] = m_appearance.glowEnabled;
    if (m_appearance.hasCustomCursor && !m_appearance.cursorData.empty()) {
        body["hasCustomCursor"] = true;
        body["cursorData"] = m_appearance.cursorData;
        body["cursorScale"] = static_cast<double>(m_appearance.cursorScale);
        body["cursorOpacity"] = static_cast<int64_t>(m_appearance.cursorOpacity);
    }

    auto req = web::WebRequest();
    // Same slow-start allowance as join.
    req.timeout(std::chrono::seconds(45));
    req.header("Content-Type", "application/json");
    req.bodyString(body.dump(matjson::NO_INDENTATION));

    log::info("[Collab] create-room room={} user={} ...", m_room, m_user);
    WebHelper::dispatch(std::move(req), "POST", apiUrl("/api/create-room"),
        [this, lifetime, gen](web::WebResponse res) {
            if (lifetime.expired() || !m_active || gen != m_gen) return;

            auto parsed = matjson::parse(res.string().unwrapOr(""));
            if (res.code() == 200 && parsed) {
                log::info("[Collab] create-room ok (HTTP 200)");
                onJoinLikeSuccess(parsed.unwrap());
                return;
            }

            log::warn("[Collab] create-room failed HTTP {} body={}", res.code(), res.string().unwrapOr(""));
            std::string message = "No se pudo crear la sala";
            std::string code = "create_failed";
            if (parsed) {
                auto v = parsed.unwrap();
                if (v.contains("error")) {
                    code = v["error"]["code"].asString().unwrapOr(code);
                    message = v["error"]["message"].asString().unwrapOr(message);
                }
            }

            // Create mode never silently joins someone else's room: the code
            // is taken, so ask for a new one.
            if (res.code() == 409 && code == "room_exists") {
                emitError("room_exists", "Ese codigo ya esta en uso. Genera uno nuevo.");
                return;
            }

            emitError(code, message);
        });
}

void CollabNetClient::poll() {
    if (!m_active || !m_joined) return;
    uint64_t gen = m_gen;
    auto lifetime = std::weak_ptr<uint8_t>(m_lifetime);

    auto req = web::WebRequest();
    req.timeout(std::chrono::seconds(35));
    req.header("Authorization", "Bearer " + m_sessionToken);
    std::string url = apiUrl(fmt::format("/api/poll?room={}&client={}", m_room, m_clientId));

    WebHelper::dispatch(std::move(req), "GET", url,
        [this, lifetime, gen](web::WebResponse res) {
            if (lifetime.expired() || !m_active || gen != m_gen) return;

            if (!res.ok()) {
                ++m_pollFailures;
                int shift = std::min(m_pollFailures - 1, 3);
                int delay = std::min(1500 * (1 << shift), 15000);
                delay += (m_clientId * 137 + m_pollFailures * 97) % 400;
                scheduleRetry(gen, delay);
                return;
            }

            auto parsed = matjson::parse(res.string().unwrapOr(""));
            if (!parsed) {
                ++m_pollFailures;
                int shift = std::min(m_pollFailures - 1, 3);
                int delay = std::min(1500 * (1 << shift), 15000);
                scheduleRetry(gen, delay);
                return;
            }

            m_pollFailures = 0;
            bool receivedMessage = false;
            auto value = parsed.unwrap();
            if (value.contains("messages")) {
                if (auto arr = value["messages"].asArray()) {
                    for (auto const& msg : arr.unwrap()) {
                        receivedMessage = true;
                        if (m_onMessage) {
                            auto onMessage = m_onMessage;
                            onMessage(msg);
                            if (lifetime.expired() || !m_active || gen != m_gen) return;
                        }
                    }
                }
            }
            if (receivedMessage) poll();
            else scheduleRetry(gen, kEmptyPollDelayMs);
        });
}

void CollabNetClient::scheduleJoinRetry(uint64_t gen, int ms) {
    auto lifetime = std::weak_ptr<uint8_t>(m_lifetime);
    std::thread([this, lifetime, gen, ms]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        if (lifetime.expired()) return;
        Loader::get()->queueInMainThread([this, lifetime, gen]() {
            if (lifetime.expired()) return;
            if (m_active && gen == m_gen) doJoin();
        });
    }).detach();
}

void CollabNetClient::scheduleRetry(uint64_t gen, int ms) {
    auto lifetime = std::weak_ptr<uint8_t>(m_lifetime);
    std::thread([this, lifetime, gen, ms]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        if (lifetime.expired()) return;
        Loader::get()->queueInMainThread([this, lifetime, gen]() {
            if (lifetime.expired()) return;
            if (m_active && gen == m_gen) poll();
        });
    }).detach();
}

void CollabNetClient::dispatchStateJson(std::string channel, std::string suffix,
                                        matjson::Value body) {
    if (!isOpen()) return;

    if (!m_stateRequestsInFlight.insert(channel).second) {
        m_pendingStateRequests.insert_or_assign(
            channel, PendingStateRequest{std::move(suffix), std::move(body)}
        );
        return;
    }

    uint64_t gen = m_gen;
    auto lifetime = std::weak_ptr<uint8_t>(m_lifetime);
    auto req = web::WebRequest();
    req.timeout(std::chrono::seconds(6));
    req.header("Content-Type", "application/json");
    req.header("Authorization", "Bearer " + m_sessionToken);
    req.bodyString(body.dump(matjson::NO_INDENTATION));
    WebHelper::dispatch(std::move(req), "POST", apiUrl(suffix),
        [this, lifetime, gen, channel = std::move(channel)](web::WebResponse) {
            if (lifetime.expired() || gen != m_gen) return;
            m_stateRequestsInFlight.erase(channel);

            auto pending = m_pendingStateRequests.find(channel);
            if (pending == m_pendingStateRequests.end() || !isOpen()) return;
            auto next = std::move(pending->second);
            m_pendingStateRequests.erase(pending);
            dispatchStateJson(channel, std::move(next.suffix), std::move(next.body));
        });
}

void CollabNetClient::sendJson(matjson::Value const& value) {
    if (!isOpen()) return;
    std::string t = value.contains("t") ? value["t"].asString().unwrapOr("") : "";

    matjson::Value body;
    std::string suffix;
    if (t == "op_batch") {
        body = matjson::makeObject({
            {"room", m_room},
            {"client", static_cast<int64_t>(m_clientId)},
            {"ops", value.contains("ops") ? value["ops"] : matjson::Value::array()},
        });
        suffix = "/api/ops";
    } else if (t == "set_perms") {
        body = matjson::makeObject({
            {"room", m_room},
            {"client", static_cast<int64_t>(m_clientId)},
            {"permissions", value.contains("permissions") ? value["permissions"] : matjson::Value::object()},
        });
        suffix = "/api/perms";
    } else if (t == "chat") {
        body = matjson::makeObject({
            {"room", m_room},
            {"client", static_cast<int64_t>(m_clientId)},
            {"text", value.contains("text") ? value["text"] : matjson::Value("")},
        });
        suffix = "/api/chat";
    } else if (t == "voice") {
        body = matjson::makeObject({
            {"room", m_room},
            {"client", static_cast<int64_t>(m_clientId)},
            {"seq", value.contains("seq") ? value["seq"] : matjson::Value(0)},
            {"data", value.contains("data") ? value["data"] : matjson::Value("")},
        });
        suffix = "/api/voice";
    } else if (t == "select") {
        // Ephemeral peer-selection presence (not part of level LWW state).
        body = matjson::makeObject({
            {"room", m_room},
            {"client", static_cast<int64_t>(m_clientId)},
            {"rects", value.contains("rects") ? value["rects"] : matjson::Value::array()},
        });
        suffix = "/api/select";
    } else if (t == "camera") {
        body = matjson::makeObject({
            {"room", m_room},
            {"client", static_cast<int64_t>(m_clientId)},
            {"x", value.contains("x") ? value["x"] : matjson::Value(0)},
            {"y", value.contains("y") ? value["y"] : matjson::Value(0)},
            {"z", value.contains("z") ? value["z"] : matjson::Value(1)},
            {"mx", value.contains("mx") ? value["mx"] : matjson::Value(0)},
            {"my", value.contains("my") ? value["my"] : matjson::Value(0)},
            {"mv", value.contains("mv") ? value["mv"] : matjson::Value(true)},
        });
        suffix = "/api/camera";
    } else if (t == "workzone") {
        body = matjson::makeObject({
            {"room", m_room},
            {"client", static_cast<int64_t>(m_clientId)},
            {"x", value.contains("x") ? value["x"] : matjson::Value(0)},
            {"y", value.contains("y") ? value["y"] : matjson::Value(0)},
            {"w", value.contains("w") ? value["w"] : matjson::Value(0)},
            {"h", value.contains("h") ? value["h"] : matjson::Value(0)},
        });
        suffix = "/api/workzone";
    } else if (t == "ping") {
        body = matjson::makeObject({
            {"room", m_room},
            {"client", static_cast<int64_t>(m_clientId)},
            {"x", value.contains("x") ? value["x"] : matjson::Value(0)},
            {"y", value.contains("y") ? value["y"] : matjson::Value(0)},
        });
        suffix = "/api/ping";
    } else if (t == "claim_layer") {
        body = matjson::makeObject({
            {"room", m_room},
            {"client", static_cast<int64_t>(m_clientId)},
            {"layer", value.contains("layer") ? value["layer"] : matjson::Value(0)},
        });
        suffix = "/api/claim-layer";
    } else if (t == "level_settings") {
        body = value;
        body["room"] = m_room;
        body["client"] = static_cast<int64_t>(m_clientId);
        suffix = "/api/level-settings";
    } else if (t == "kick") {
        body = matjson::makeObject({
            {"room", m_room},
            {"client", static_cast<int64_t>(m_clientId)},
            {"target", value.contains("target") ? value["target"] : matjson::Value(0)},
        });
        suffix = "/api/kick";
    } else {
        return;
    }

    if (t == "select" || t == "camera" || t == "workzone" ||
        t == "set_perms" || t == "level_settings") {
        dispatchStateJson(std::move(t), std::move(suffix), std::move(body));
        return;
    }

    auto req = web::WebRequest();
    // Voice frames are perishable: time them out fast so a slow connection
    // doesn't pile up 15s-long in-flight requests. Discrete pings and layer
    // claims use the same short timeout; state presence is coalesced above.
    bool ephemeral = (t == "voice" || t == "ping" || t == "claim_layer");
    req.timeout(std::chrono::seconds(ephemeral ? 6 : 15));
    req.header("Content-Type", "application/json");
    req.header("Authorization", "Bearer " + m_sessionToken);
    req.bodyString(body.dump(matjson::NO_INDENTATION));
    WebHelper::dispatch(std::move(req), "POST", apiUrl(suffix),
        [](web::WebResponse) {});
}

void CollabNetClient::sendOps(matjson::Value const& ops, OpsCb cb) {
    if (!isOpen()) {
        if (cb) cb(false, 0, 0);
        return;
    }
    uint64_t gen = m_gen;
    auto lifetime = std::weak_ptr<uint8_t>(m_lifetime);

    auto body = matjson::makeObject({
        {"room", m_room},
        {"client", static_cast<int64_t>(m_clientId)},
        {"ops", ops},
    });

    auto req = web::WebRequest();
    req.timeout(std::chrono::seconds(20));
    req.header("Content-Type", "application/json");
    req.header("Authorization", "Bearer " + m_sessionToken);
    req.bodyString(body.dump(matjson::NO_INDENTATION));

    WebHelper::dispatch(std::move(req), "POST", apiUrl("/api/ops"),
        [this, lifetime, gen, cb = std::move(cb)](web::WebResponse res) {
            if (!cb) return;
            // A different generation means stop()/start() ran while this was
            // in flight; the manager reset its outbox too, so stay silent.
            if (lifetime.expired() || gen != m_gen) return;
            int accepted = 0;
            if (auto parsed = matjson::parse(res.string().unwrapOr(""))) {
                accepted = static_cast<int>(parsed.unwrap()["count"].asInt().unwrapOr(0));
            }
            cb(res.code() == 200, res.code(), accepted);
        });
}

void CollabNetClient::sendSeed(matjson::Value const& objects, bool finalChunk, SeedCb cb) {
    if (!isOpen()) {
        if (cb) cb(false, 0, 0, 0);
        return;
    }
    uint64_t gen = m_gen;
    auto lifetime = std::weak_ptr<uint8_t>(m_lifetime);

    auto body = matjson::makeObject({
        {"room", m_room},
        {"client", static_cast<int64_t>(m_clientId)},
        {"objects", objects},
        {"final", finalChunk},
    });

    auto req = web::WebRequest();
    // Big levels: a chunk can be ~1MB of JSON; give the host time to upload.
    req.timeout(std::chrono::seconds(45));
    req.header("Content-Type", "application/json");
    req.header("Authorization", "Bearer " + m_sessionToken);
    req.bodyString(body.dump(matjson::NO_INDENTATION));

    WebHelper::dispatch(std::move(req), "POST", apiUrl("/api/seed"),
        [this, lifetime, gen, cb = std::move(cb)](web::WebResponse res) {
            if (!cb) return;
            if (lifetime.expired() || gen != m_gen) return;
            int accepted = 0;
            int total = 0;
            if (auto parsed = matjson::parse(res.string().unwrapOr(""))) {
                auto v = parsed.unwrap();
                accepted = static_cast<int>(v["accepted"].asInt().unwrapOr(0));
                total = static_cast<int>(v["total"].asInt().unwrapOr(0));
            }
            cb(res.code() == 200, res.code(), accepted, total);
        });
}

void CollabNetClient::sendInvite(int accountId, std::string const& fromName, InviteCb cb) {
    if (!isOpen()) {
        if (cb) cb(false, false, "No estas conectado a la sala");
        return;
    }

    auto body = matjson::makeObject({
        {"room", m_room},
        {"client", static_cast<int64_t>(m_clientId)},
        {"account", static_cast<int64_t>(accountId)},
        {"fromName", fromName},
    });

    auto req = web::WebRequest();
    req.timeout(std::chrono::seconds(15));
    req.header("Content-Type", "application/json");
    req.header("Authorization", "Bearer " + m_sessionToken);
    req.bodyString(body.dump(matjson::NO_INDENTATION));

    WebHelper::dispatch(std::move(req), "POST", apiUrl("/api/invite"),
        [cb = std::move(cb)](web::WebResponse res) {
            if (!cb) return;
            auto parsed = matjson::parse(res.string().unwrapOr(""));
            if (res.code() == 200 && parsed) {
                auto v = parsed.unwrap();
                bool online = v.contains("online") && v["online"].asBool().unwrapOr(false);
                cb(true, online, online ? "Invitacion enviada" : "El usuario no esta en linea");
                return;
            }
            std::string message = "No se pudo enviar la invitacion";
            if (parsed) {
                auto v = parsed.unwrap();
                if (v.contains("error")) message = v["error"]["message"].asString().unwrapOr(message);
            }
            cb(false, false, message);
        });
}

}
