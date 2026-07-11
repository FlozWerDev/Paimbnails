#include "CollabNetClient.hpp"

#include "../../utils/WebHelper.hpp"

#include <Geode/Geode.hpp>
#include <chrono>
#include <thread>

using namespace geode::prelude;

namespace paimon::collab {

namespace {
// How many times a join 404 is retried before giving up. Covers the window
// where the host is still creating the room (a Render cold start can take
// tens of seconds) without making a genuinely wrong code feel broken.
constexpr int kJoinMaxRetries = 3;
constexpr int kJoinRetryMs = 2500;
} // namespace

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
    stop();
    m_base = std::move(baseUrl);
    m_room = std::move(roomCode);
    m_user = std::move(username);
    m_appearance = std::move(appearance);
    m_resumeToken = std::move(resumeToken);
    m_mode = mode;
    m_active = true;
    m_joinRetries = 0;
    ++m_gen;
    if (m_onState) m_onState(ConnState::Connecting, "Conectando...");
    if (mode == ConnectMode::Create) {
        doCreate();
    } else {
        doJoin();
    }
}

void CollabNetClient::stop() {
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
    ++m_gen; // invalidate in-flight callbacks

    // Best-effort leave so the server frees the slot promptly.
    if (wasJoined && clientId > 0 && !base.empty()) {
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
    // Copies on purpose: start() moves its arguments into these same members.
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

    // Server destroys the room; invalidate local state so poll/ops/leave stop.
    m_active = false;
    m_joined = false;
    m_clientId = 0;
    m_sessionToken.clear();
    m_resumeToken.clear();
    ++m_gen;
}

bool CollabNetClient::isOpen() const {
    return m_active && m_joined;
}

void CollabNetClient::requestResync() {
    if (!isOpen()) return;
    auto body = matjson::makeObject({
        {"room", m_room},
        {"client", static_cast<int64_t>(m_clientId)},
    });
    auto req = web::WebRequest();
    req.header("Content-Type", "application/json");
    req.header("Authorization", "Bearer " + m_sessionToken);
    req.bodyString(body.dump(matjson::NO_INDENTATION));
    WebHelper::dispatch(std::move(req), "POST", apiUrl("/api/resync"),
        [](web::WebResponse) {});
}

void CollabNetClient::onJoinLikeSuccess(matjson::Value value) {
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
    m_joinRetries = 0;
    value["t"] = "join_ok";
    bool isHost = value.contains("isHost") && value["isHost"].asBool().unwrapOr(false);
    log::info("[Collab] joined room={} clientId={} isHost={}", m_room, m_clientId, isHost);
    if (m_onState) m_onState(ConnState::Connected, "Conectado");
    if (m_onMessage) m_onMessage(value);
    poll();
}

void CollabNetClient::emitError(std::string const& code, std::string const& message) {
    if (m_onMessage) {
        m_onMessage(matjson::makeObject({
            {"t", "error"}, {"code", code}, {"message", message},
        }));
    }
    if (m_onState) m_onState(ConnState::Disconnected, message);
    m_active = false;
}

void CollabNetClient::doJoin() {
    uint64_t gen = m_gen;

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

    auto req = web::WebRequest();
    // The shared Render instance sleeps after ~15 min idle; a cold start can
    // take well over 15s, so give the first request room to wake it up.
    req.timeout(std::chrono::seconds(45));
    req.header("Content-Type", "application/json");
    req.bodyString(body.dump(matjson::NO_INDENTATION));

    log::info("[Collab] join room={} user={} ...", m_room, m_user);
    WebHelper::dispatch(std::move(req), "POST", apiUrl("/api/join"),
        [this, gen](web::WebResponse res) {
            if (!m_active || gen != m_gen) return;

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

            // Join mode never auto-creates. A 404 is retried a few times
            // first: the host may still be creating the room (cold starts on
            // Render take a while) or be mid-reconnect after a server restart.
            if (res.code() == 404 && code == "room_not_found") {
                if (m_joinRetries < kJoinMaxRetries) {
                    ++m_joinRetries;
                    if (m_onState) {
                        m_onState(ConnState::Connecting, fmt::format(
                            "La sala aun no aparece; reintentando ({}/{})...",
                            m_joinRetries, kJoinMaxRetries));
                    }
                    scheduleJoinRetry(gen, kJoinRetryMs);
                    return;
                }
                emitError("room_not_found",
                    "Esa sala no existe. Revisa el codigo con el host o crea una sala nueva.");
                return;
            }

            emitError(code, message);
        });
}

void CollabNetClient::doCreate() {
    uint64_t gen = m_gen;

    // Send an empty initial snapshot: the manager streams the host's editor
    // objects afterwards via /api/ops (existing seeding path).
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

    auto req = web::WebRequest();
    // Same cold-start allowance as join: waking the Render instance can take
    // much longer than a normal request.
    req.timeout(std::chrono::seconds(45));
    req.header("Content-Type", "application/json");
    req.bodyString(body.dump(matjson::NO_INDENTATION));

    log::info("[Collab] create-room room={} user={} ...", m_room, m_user);
    WebHelper::dispatch(std::move(req), "POST", apiUrl("/api/create-room"),
        [this, gen](web::WebResponse res) {
            if (!m_active || gen != m_gen) return;

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

    auto req = web::WebRequest();
    req.timeout(std::chrono::seconds(35));
    req.header("Authorization", "Bearer " + m_sessionToken);
    std::string url = apiUrl(fmt::format("/api/poll?room={}&client={}", m_room, m_clientId));

    WebHelper::dispatch(std::move(req), "GET", url,
        [this, gen](web::WebResponse res) {
            if (!m_active || gen != m_gen) return;

            if (!res.ok()) {
                scheduleRetry(gen, 1500);
                return;
            }

            auto parsed = matjson::parse(res.string().unwrapOr(""));
            if (parsed) {
                auto value = parsed.unwrap();
                if (value.contains("messages")) {
                    if (auto arr = value["messages"].asArray()) {
                        for (auto const& msg : arr.unwrap()) {
                            if (m_onMessage) m_onMessage(msg);
                        }
                    }
                }
            }
            // Immediately re-poll.
            poll();
        });
}

void CollabNetClient::scheduleJoinRetry(uint64_t gen, int ms) {
    std::thread([this, gen, ms]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        Loader::get()->queueInMainThread([this, gen]() {
            if (m_active && gen == m_gen) doJoin();
        });
    }).detach();
}

void CollabNetClient::scheduleRetry(uint64_t gen, int ms) {
    std::thread([this, gen, ms]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        Loader::get()->queueInMainThread([this, gen]() {
            if (m_active && gen == m_gen) poll();
        });
    }).detach();
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

    auto req = web::WebRequest();
    // Voice frames are perishable: time them out fast so a slow connection
    // doesn't pile up 15s-long in-flight requests. Selection is similarly
    // fire-and-forget.
    req.timeout(std::chrono::seconds((t == "voice" || t == "select") ? 6 : 15));
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
        [this, gen, cb = std::move(cb)](web::WebResponse res) {
            if (!cb) return;
            // A different generation means stop()/start() ran while this was
            // in flight; the manager reset its outbox too, so stay silent.
            if (gen != m_gen) return;
            int accepted = 0;
            if (auto parsed = matjson::parse(res.string().unwrapOr(""))) {
                accepted = static_cast<int>(parsed.unwrap()["count"].asInt().unwrapOr(0));
            }
            cb(res.code() == 200, res.code(), accepted);
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

} // namespace paimon::collab
