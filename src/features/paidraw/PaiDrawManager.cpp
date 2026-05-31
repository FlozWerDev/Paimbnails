#include "PaiDrawManager.hpp"

#include "PaiDrawCodec.hpp"
#include "../../framework/EventBus.hpp"
#include "../../utils/AccountVerifier.hpp"
#include "../../utils/JsonHelper.hpp"
#include "../../utils/MainThreadDelay.hpp"
#include "../../utils/PaimonNotification.hpp"
#include "../../utils/WebHelper.hpp"
#include <Geode/loader/Loader.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/utils/web.hpp>

using namespace geode::prelude;

namespace paidraw {

namespace {

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string trimString(std::string value) {
    geode::utils::string::trimIP(value);
    return value;
}

std::string jsonString(matjson::Value const& value, char const* key, std::string fallback = "") {
    if (value.contains(key) && value[key].isString()) {
        return value[key].asString().unwrapOr(fallback);
    }
    return fallback;
}

int jsonInt(matjson::Value const& value, char const* key, int fallback = 0) {
    if (value.contains(key)) {
        return value[key].asInt().unwrapOr(fallback);
    }
    return fallback;
}

double jsonDouble(matjson::Value const& value, char const* key, double fallback = 0.0) {
    if (value.contains(key)) {
        return value[key].asDouble().unwrapOr(fallback);
    }
    return fallback;
}

bool jsonBool(matjson::Value const& value, char const* key, bool fallback = false) {
    if (value.contains(key) && value[key].isBool()) {
        return value[key].asBool().unwrapOr(fallback);
    }
    return fallback;
}

uint64_t jsonUInt64(matjson::Value const& value, char const* key, uint64_t fallback = 0) {
    if (!value.contains(key)) return fallback;
    if (value[key].isString()) {
        auto parsed = geode::utils::numFromString<uint64_t>(value[key].asString().unwrapOr("0"));
        if (parsed) return parsed.unwrap();
    }
    auto asInt = value[key].asInt();
    if (asInt) return static_cast<uint64_t>(asInt.unwrap());
    auto asDouble = value[key].asDouble();
    if (asDouble) return static_cast<uint64_t>(asDouble.unwrap());
    return fallback;
}

PlayerStatus parseStatus(std::string const& text) {
    auto lower = lowercase(text);
    if (lower == "playing" || lower == "jugando") return PlayerStatus::Playing;
    if (lower == "room" || lower == "in_room" || lower == "en sala") return PlayerStatus::InRoom;
    return PlayerStatus::Free;
}

RoomState parseRoomState(std::string const& text) {
    auto lower = lowercase(text);
    if (lower == "in_game" || lower == "playing" || lower == "en juego") return RoomState::InGame;
    return RoomState::Waiting;
}

GameMode parseMode(std::string const& text) {
    auto lower = lowercase(text);
    if (lower == "animation" || lower == "animacion") return GameMode::Animation;
    if (lower == "chain" || lower == "cadena") return GameMode::Chain;
    return GameMode::Classic;
}

WordLanguage parseLanguage(std::string const& text) {
    auto lower = lowercase(text);
    if (lower == "en" || lower == "english") return WordLanguage::English;
    if (lower == "both" || lower == "ambos") return WordLanguage::Both;
    return WordLanguage::Spanish;
}

WordDifficulty parseDifficulty(std::string const& text) {
    auto lower = lowercase(text);
    if (lower == "medium" || lower == "media") return WordDifficulty::Medium;
    if (lower == "hard" || lower == "dificil") return WordDifficulty::Hard;
    return WordDifficulty::Easy;
}

matjson::Value makePlayerJson(PlayerInfo const& player) {
    return matjson::makeObject({
        {"accountID", static_cast<int64_t>(player.accountID)},
        {"name", player.name},
        {"level", player.level},
        {"iconID", player.iconID},
        {"iconType", player.iconType},
        {"color1", player.color1},
        {"color2", player.color2},
        {"glow", player.glow},
        {"status", std::string(statusLabel(player.status))},
        {"ready", player.ready},
        {"host", player.host},
        {"guessed", player.guessed},
        {"pingMs", player.pingMs},
        {"score", player.score},
    });
}

matjson::Value makeRoomJson(RoomInfo const& room) {
    auto players = matjson::Value::array();
    for (auto const& player : room.players) {
        players.push(makePlayerJson(player));
    }
    return matjson::makeObject({
        {"id", static_cast<int64_t>(room.id)},
        {"name", room.config.name},
        {"hostId", static_cast<int64_t>(room.hostId)},
        {"host", room.hostName},
        {"maxPlayers", room.config.maxPlayers},
        {"rounds", room.config.rounds},
        {"roundTimeSeconds", room.config.roundTimeSeconds},
        {"mode", std::string(modeLabel(room.config.mode))},
        {"language", std::string(languageLabel(room.config.language))},
        {"hasPassword", room.hasPassword},
        {"state", std::string(roomStateLabel(room.state))},
        {"players", players},
    });
}

} // namespace

PaiDrawManager& PaiDrawManager::get() {
    static PaiDrawManager instance;
    return instance;
}

PaiDrawManager::PaiDrawManager() = default;

PaiDrawManager::~PaiDrawManager() {
    shutdown();
}

void PaiDrawManager::init() {
    if (m_initialized) return;
    m_initialized = true;

    {
        std::lock_guard lock(m_mutex);
        m_state.initialized = true;
    }

    loadSavedState();
    seedWordBank();
    configureOfflinePreview();
}

void PaiDrawManager::shutdown() {
    m_shuttingDown = true;
    disconnect();
}

SessionState PaiDrawManager::snapshot() const {
    std::lock_guard lock(m_mutex);
    return m_state;
}

bool PaiDrawManager::isReady() const {
    std::lock_guard lock(m_mutex);
    return m_state.connected || m_state.offlinePreview;
}

void PaiDrawManager::queueMain(geode::Function<void()> fn) const {
    Loader::get()->queueInMainThread([fn = std::move(fn)]() mutable {
        fn();
    });
}

void PaiDrawManager::loadSavedState() {
    auto* mod = Mod::get();
    if (!mod) return;

    std::lock_guard lock(m_mutex);
    m_state.authToken = mod->getSavedValue<std::string>("paidraw_auth_token", "");
    m_state.serverURL = mod->getSavedValue<std::string>("paidraw_server_url", "https://paimbnailsbot.onrender.com");
    // Migrate users who still have a stale URL (old retired Vercel host or
    // the original placeholder `paidraw.example.com`) so PaiDraw points at
    // the live Render server. The saved value is rewritten on the next save
    // call.
    {
        constexpr const char* kCanonicalURL = "https://paimbnailsbot.onrender.com";
        static constexpr std::string_view kLegacyHosts[] = {
            "paimbnails-emote.vercel.app",
            "paidraw.example.com",
            "example.com",
        };
        for (auto host : kLegacyHosts) {
            if (m_state.serverURL.find(host) != std::string::npos) {
                m_state.serverURL = kCanonicalURL;
                break;
            }
        }
    }
    m_state.displayName = mod->getSavedValue<std::string>("paidraw_username", "");
    m_state.settings = mod->getSavedValue<matjson::Value>("paidraw_settings", matjson::Value::object());
    m_state.stats = mod->getSavedValue<matjson::Value>("paidraw_stats", matjson::Value::object());
}

void PaiDrawManager::saveSessionData() {
    auto* mod = Mod::get();
    if (!mod) return;

    SessionState state;
    {
        std::lock_guard lock(m_mutex);
        state = m_state;
    }

    mod->setSavedValue("paidraw_auth_token", state.authToken);
    mod->setSavedValue("paidraw_server_url", state.serverURL);
    mod->setSavedValue("paidraw_username", state.displayName);
    mod->setSavedValue("paidraw_settings", state.settings);
    paimon::requestDeferredModSave();
}

void PaiDrawManager::saveStats() {
    auto* mod = Mod::get();
    if (!mod) return;

    SessionState state;
    {
        std::lock_guard lock(m_mutex);
        state = m_state;
    }

    mod->setSavedValue("paidraw_stats", state.stats);
    paimon::requestDeferredModSave();
}

void PaiDrawManager::seedWordBank() {
    m_wordBank = {
        {"Orb", WordDifficulty::Easy, "GD"},
        {"Spike", WordDifficulty::Easy, "GD"},
        {"Wave", WordDifficulty::Easy, "GD"},
        {"Paimon", WordDifficulty::Easy, "Community"},
        {"Creator", WordDifficulty::Medium, "Community"},
        {"Bloodbath", WordDifficulty::Medium, "Levels"},
        {"Trigger", WordDifficulty::Medium, "Editor"},
        {"Robot", WordDifficulty::Easy, "GD"},
        {"Editor", WordDifficulty::Easy, "GD"},
        {"Gauntlet", WordDifficulty::Medium, "GD"},
        {"Frame Perfect", WordDifficulty::Hard, "Community"},
        {"Telefono Descompuesto", WordDifficulty::Hard, "Social"},
        {"Gato", WordDifficulty::Easy, "General"},
        {"Arquitecto", WordDifficulty::Medium, "General"},
        {"Helicoptero", WordDifficulty::Hard, "General"},
    };
}

void PaiDrawManager::configureOfflinePreview() {
    // Initialize local player identity from the GD account. We never seed
    // example rooms/players anymore — every list shown to the user comes from
    // the live PaiDraw server (PaimbnailsBot /api/paidraw/*). Until the first
    // server snapshot arrives the lobby is intentionally empty.
    auto account = AccountVerifier::get().verify();

    std::lock_guard lock(m_mutex);
    m_state.localAccountID = static_cast<uint32_t>(std::max(account.accountID, 0));
    if (m_state.displayName.empty()) {
        m_state.displayName = account.username.empty() ? "Player" : account.username;
    }

    m_state.onlinePlayers.clear();
    m_state.rooms.clear();
    m_state.onlineCount = 0;
    m_state.currentRoom = RoomInfo();
    m_state.currentRoomId = 0;
    m_state.currentRound = RoundState();
    m_state.roomChat.clear();
    m_state.recentStrokes.clear();
    m_state.results = MatchResults();
    m_state.offlinePreview = false;
}

std::string PaiDrawManager::baseServerUrl() const {
    std::lock_guard lock(m_mutex);
    return m_state.serverURL.empty() ? "https://paimbnailsbot.onrender.com" : m_state.serverURL;
}

std::string PaiDrawManager::wsServerUrl() const {
    auto base = baseServerUrl();
    if (base.starts_with("https://")) {
        base.replace(0, 8, "wss://");
    }
    else if (base.starts_with("http://")) {
        base.replace(0, 7, "ws://");
    }
    return base;
}

std::string PaiDrawManager::endpointUrl(char const* suffix) const {
    auto base = wsServerUrl();
    if (!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    return base + suffix;
}

bool PaiDrawManager::hasValidLogin() const {
    auto* accountManager = GJAccountManager::get();
    return accountManager && accountManager->m_accountID > 0 && !accountManager->m_username.empty() && !accountManager->m_GJP2.empty();
}

void PaiDrawManager::ensureConnected() {
    init();

    if (m_socketOpen || m_authInFlight) return;

    {
        std::lock_guard lock(m_mutex);
        m_state.connecting = true;
        m_state.offlinePreview = false;
        m_state.connected = false;
        m_state.authenticated = false;
    }
    publishConnection("Conectando a PaiDraw...");

    authenticate();
}

void PaiDrawManager::disconnect() {
    m_socketOpen = false;
    m_authInFlight = false;
    m_lobbyRequestInFlight = false;
    m_roomRequestInFlight = false;
    m_strokeFlushScheduled = false;
    ++m_pollGeneration;

    {
        std::lock_guard lock(m_mutex);
        m_state.connected = false;
        m_state.connecting = false;
        m_state.authenticated = false;
        m_pendingStrokes.clear();
    }
    if (!m_shuttingDown) {
        publishConnection("Desconectado");
    }
}

geode::ByteVector PaiDrawManager::encodeJson(matjson::Value const& value) const {
    codec::MsgPackWriter writer;
    std::function<void(matjson::Value const&)> encode = [&](matjson::Value const& input) {
        if (input.isBool()) {
            writer.boolean(input.asBool().unwrapOr(false));
            return;
        }
        if (input.isString()) {
            writer.string(input.asString().unwrapOr(""));
            return;
        }
        if (input.isArray()) {
            auto arrRes = input.asArray();
            if (!arrRes.isOk()) {
                writer.array(0);
                return;
            }
            auto arr = arrRes.unwrap();
            writer.array(arr.size());
            for (auto const& value : arr) {
                encode(value);
            }
            return;
        }

        auto asInt = input.asInt();
        if (asInt) {
            writer.integer(asInt.unwrap());
            return;
        }

        auto asDouble = input.asDouble();
        if (asDouble) {
            writer.floating(asDouble.unwrap());
            return;
        }

        auto serialized = input.dump();
        auto parsed = matjson::parse(serialized);
        if (!parsed) {
            writer.nil();
            return;
        }

        auto reparsed = parsed.unwrap();
        if (reparsed.isArray()) {
            auto arrRes = reparsed.asArray();
            if (!arrRes.isOk()) {
                writer.array(0);
                return;
            }
            auto arr = arrRes.unwrap();
            writer.array(arr.size());
            for (auto const& value : arr) {
                encode(value);
            }
            return;
        }

        if (reparsed.isObject()) {
            // Properly iterate the JSON object via matjson rather than the
            // hand-rolled parser that previously tried to split the dump()
            // output by commas. That parser broke on values containing `:`
            // or escaped quotes (chat messages, usernames with colons,
            // numbers in scientific notation), corrupting the msgpack
            // payload on the wire and desyncing PaiDraw state.
            std::vector<std::pair<std::string, matjson::Value>> entries;
            for (auto const& [key, value] : reparsed) {
                entries.emplace_back(std::string(key), value);
            }
            writer.map(entries.size());
            for (auto const& [key, value] : entries) {
                writer.key(key);
                encode(value);
            }
            return;
        }

        writer.nil();
    };

    encode(value);
    return writer.bytes();
}

void PaiDrawManager::sendPacket(PacketType type, matjson::Value const& payload, uint32_t roomId) {
    if (!m_socketOpen) return;

    PaiDrawPacket packet;
    packet.type = type;
    packet.roomId = roomId;
    packet.timestamp = nowMs();
    packet.payload = encodeJson(payload);

    (void)codec::encodeEnvelope(packet);
}

void PaiDrawManager::authenticate() {
    if (!hasValidLogin()) {
        publishConnection("Inicia sesion en GD para usar PaiDraw online");
        return;
    }

    auto* accountManager = GJAccountManager::get();
    if (!accountManager) return;

    m_authInFlight = true;
    auto payload = matjson::makeObject({
        {"accountID", accountManager->m_accountID},
        {"username", std::string(accountManager->m_username)},
        {"sessionToken", std::string(accountManager->m_GJP2)},
        {"savedJwt", snapshot().authToken},
        {"client", "PaiDraw/Geode"},
    });

    requestJson("POST", "/api/paidraw/auth", &payload, false,
        [this, accountID = accountManager->m_accountID](bool ok, matjson::Value const& response, std::string const& error) {
            if (!ok) {
                m_authInFlight = false;
                applyRequestFailure(error.empty() ? "No se pudo autenticar en PaiDraw" : error, true);
                return;
            }

            bool shouldSaveSession = false;
            {
                std::lock_guard lock(m_mutex);
                m_authInFlight = false;
                m_socketOpen = true;
                m_state.connecting = false;
                m_state.connected = true;
                m_state.authenticated = true;
                m_state.offlinePreview = false;
                m_state.localAccountID = static_cast<uint32_t>(std::max(accountID, 0));

                auto token = jsonString(response, "jwt", "");
                if (!token.empty()) {
                    m_state.authToken = token;
                    shouldSaveSession = true;
                }
                // Intentionally do NOT seed m_lobbyVersion from the auth
                // response. We want the first refreshLobby() call to fetch a
                // full snapshot (no `since` query) so we hydrate the player
                // and room lists. If we copied the version from auth, the
                // server would short-circuit with {changed: false} and the
                // UI would stay empty until somebody else triggered a bump.
                m_lobbyVersion.clear();
                if (response.contains("player") && response["player"].isObject()) {
                    auto player = parsePlayer(response["player"]);
                    if (!player.name.empty()) {
                        m_state.displayName = player.name;
                    }
                }
            }

            if (shouldSaveSession) {
                saveSessionData();
            }
            publishConnection("Autenticado en PaiDraw");
            refreshLobby();
            schedulePollLoop();
        }
    );
}

void PaiDrawManager::refreshLobby() {
    ensureConnected();
    if (!m_socketOpen || m_lobbyRequestInFlight) return;

    std::string path = "/api/paidraw/lobby";
    if (!m_lobbyVersion.empty()) {
        path += fmt::format("?since={}", m_lobbyVersion);
    }

    m_lobbyRequestInFlight = true;
    requestJson("GET", path, nullptr, true,
        [this](bool ok, matjson::Value const& response, std::string const& error) {
            m_lobbyRequestInFlight = false;
            if (!ok) {
                applyRequestFailure(error.empty() ? "No se pudo actualizar el lobby" : error);
                return;
            }

            if (jsonBool(response, "changed", true) == false) {
                auto version = jsonString(response, "lobbyVersion", jsonString(response, "timelast", m_lobbyVersion));
                if (!version.empty()) {
                    std::lock_guard lock(m_mutex);
                    m_lobbyVersion = version;
                }
                return;
            }

            handleLobbyHttpSnapshot(response);
        }
    );
}

void PaiDrawManager::createRoom(RoomConfig const& config) {
    RoomConfig sanitized = config;
    sanitized.name = sanitizeRoomName(sanitized.name);
    if (sanitized.name.empty()) {
        sanitized.name = "PaiDraw Room";
    }
    sanitized.maxPlayers = std::clamp(sanitized.maxPlayers, 2, kMaxRoomPlayers);
    sanitized.rounds = std::clamp(sanitized.rounds, 3, 10);
    sanitized.roundTimeSeconds = std::clamp(sanitized.roundTimeSeconds, 60, 120);

    auto payload = matjson::makeObject({
        {"name", sanitized.name},
        {"maxPlayers", sanitized.maxPlayers},
        {"rounds", sanitized.rounds},
        {"roundTimeSeconds", sanitized.roundTimeSeconds},
        {"mode", std::string(modeLabel(sanitized.mode))},
        {"password", sanitized.password},
        {"language", std::string(languageLabel(sanitized.language))},
    });

    requestJson("POST", "/api/paidraw/rooms", &payload, true,
        [this](bool ok, matjson::Value const& response, std::string const& error) {
            if (!ok) {
                applyRequestFailure(error.empty() ? "No se pudo crear la sala" : error);
                return;
            }
            handleLobbyHttpSnapshot(response);
            if (response.contains("room") && response["room"].isObject()) {
                handleRoomHttpSnapshot(response["room"]);
            }
        }
    );
}

void PaiDrawManager::joinRoom(uint32_t roomId, std::string const& password) {
    auto payload = matjson::makeObject({
        {"roomId", static_cast<int64_t>(roomId)},
        {"password", password},
        {"type", "join"},
    });
    requestJson("POST", fmt::format("/api/paidraw/rooms/{}/action", roomId), &payload, true,
        [this](bool ok, matjson::Value const& response, std::string const& error) {
            if (!ok) {
                applyRequestFailure(error.empty() ? "No se pudo unir a la sala" : error);
                return;
            }
            handleRoomHttpSnapshot(response);
            refreshLobby();
        }
    );
}

void PaiDrawManager::leaveRoom() {
    auto roomId = snapshot().currentRoomId;
    if (!roomId) return;
    // If the user bails mid-round we still want their strokes to land on
    // the server, otherwise the rest of the room sees an empty canvas.
    queueStrokeFlush();
    auto payload = matjson::makeObject({{"type", "leave"}});
    requestJson("POST", fmt::format("/api/paidraw/rooms/{}/action", roomId), &payload, true,
        [this](bool ok, matjson::Value const& response, std::string const& error) {
            if (!ok) {
                applyRequestFailure(error.empty() ? "No se pudo salir de la sala" : error);
                return;
            }

            if (jsonBool(response, "removed", false)) {
                {
                    std::lock_guard lock(m_mutex);
                    m_state.currentRoom = RoomInfo();
                    m_state.currentRoomId = 0;
                    m_state.roomChat.clear();
                    m_state.recentStrokes.clear();
                    m_roomVersion.clear();
                }
                publishRoom();
                publishChat();
                refreshLobby();
                return;
            }

            handleRoomHttpSnapshot(response);
            refreshLobby();
        }
    );
}

void PaiDrawManager::toggleReady() {
    auto room = snapshot().currentRoom;
    auto roomId = room.id;
    auto payload = matjson::makeObject({{"type", "ready"}});
    requestJson("POST", fmt::format("/api/paidraw/rooms/{}/action", roomId), &payload, true,
        [this](bool ok, matjson::Value const& response, std::string const& error) {
            if (!ok) {
                applyRequestFailure(error.empty() ? "No se pudo cambiar el estado listo" : error);
                return;
            }
            handleRoomHttpSnapshot(response);
        }
    );
}

void PaiDrawManager::startGame() {
    auto roomId = snapshot().currentRoomId;
    auto payload = matjson::makeObject({{"type", "start"}});
    requestJson("POST", fmt::format("/api/paidraw/rooms/{}/action", roomId), &payload, true,
        [this](bool ok, matjson::Value const& response, std::string const& error) {
            if (!ok) {
                applyRequestFailure(error.empty() ? "No se pudo iniciar la partida" : error);
                return;
            }
            handleRoomHttpSnapshot(response);
            refreshLobby();
        }
    );
}

void PaiDrawManager::updateRoomConfig(RoomConfig const& config) {
    auto roomId = snapshot().currentRoomId;
    auto payload = matjson::makeObject({
        {"name", config.name},
        {"maxPlayers", config.maxPlayers},
        {"rounds", config.rounds},
        {"roundTimeSeconds", config.roundTimeSeconds},
        {"mode", std::string(modeLabel(config.mode))},
        {"language", std::string(languageLabel(config.language))},
    });

    requestJson("PATCH", fmt::format("/api/paidraw/rooms/{}", roomId), &payload, true,
        [this](bool ok, matjson::Value const& response, std::string const& error) {
            if (!ok) {
                applyRequestFailure(error.empty() ? "No se pudo actualizar la sala" : error);
                return;
            }
            handleRoomHttpSnapshot(response);
            refreshLobby();
        }
    );
}

void PaiDrawManager::sendRoomChat(std::string const& text) {
    auto trimmedText = trimString(text);
    if (trimmedText.empty()) return;

    auto roomId = snapshot().currentRoomId;
    auto payload = matjson::makeObject({
        {"type", "chat"},
        {"message", trimmedText},
    });

    requestJson("POST", fmt::format("/api/paidraw/rooms/{}/action", roomId), &payload, true,
        [this](bool ok, matjson::Value const& response, std::string const& error) {
            if (!ok) {
                applyRequestFailure(error.empty() ? "No se pudo enviar el mensaje" : error);
                return;
            }
            handleRoomHttpSnapshot(response);
        }
    );
}

void PaiDrawManager::sendGuess(std::string const& text) {
    auto trimmedText = trimString(text);
    if (trimmedText.empty()) return;

    auto roomId = snapshot().currentRoomId;
    auto payload = matjson::makeObject({{"type", "guess"}, {"guess", trimmedText}});
    requestJson("POST", fmt::format("/api/paidraw/rooms/{}/action", roomId), &payload, true,
        [this](bool ok, matjson::Value const& response, std::string const& error) {
            if (!ok) {
                applyRequestFailure(error.empty() ? "No se pudo enviar la respuesta" : error);
                return;
            }
            handleRoomHttpSnapshot(response);
        }
    );
}

void PaiDrawManager::sendStroke(StrokeSegment const& stroke) {
    {
        std::lock_guard lock(m_mutex);
        m_state.recentStrokes.push_back(stroke);
        if (m_state.recentStrokes.size() > 2048) {
            m_state.recentStrokes.erase(m_state.recentStrokes.begin(), m_state.recentStrokes.begin() + 512);
        }
        m_pendingStrokes.push_back(stroke);
    }
    // Render locally right away so the artist sees their own stroke without
    // a network round-trip. We deliberately do NOT call queueStrokeFlush()
    // here: strokes are buffered locally for the entire round and only
    // shipped to the server when the round ends (either because the timer
    // hit zero, the round number changed, or the room transitioned out of
    // "EN JUEGO"). This keeps the wire quiet during gameplay and lets the
    // drawing feel as fluid as a single-player canvas.
    publishStroke(stroke);
}

void PaiDrawManager::clearCanvas() {
    auto roomId = snapshot().currentRoomId;
    {
        std::lock_guard lock(m_mutex);
        m_state.recentStrokes.clear();
        m_pendingStrokes.clear();
    }

    auto payload = matjson::makeObject({{"type", "clear"}});
    requestJson("POST", fmt::format("/api/paidraw/rooms/{}/action", roomId), &payload, true,
        [this](bool ok, matjson::Value const& response, std::string const& error) {
            if (!ok) {
                applyRequestFailure(error.empty() ? "No se pudo limpiar el canvas" : error);
                return;
            }
            handleRoomHttpSnapshot(response);
        }
    );
}

void PaiDrawManager::publishPresence(std::string const& status) {
    (void)status;
}

WordEntry PaiDrawManager::currentWord() const {
    if (m_wordBank.empty()) {
        return {"Orb", WordDifficulty::Easy, "GD"};
    }
    size_t index = static_cast<size_t>(snapshot().currentRound.currentRound - 1) % m_wordBank.size();
    return m_wordBank[index];
}

void PaiDrawManager::handlePacket(PaiDrawPacket const& packet) {
    auto decodedPayload = codec::decodePayload(std::span<uint8_t const>(packet.payload.data(), packet.payload.size()));
    matjson::Value payload = decodedPayload ? decodedPayload.unwrap() : matjson::Value::object();

    switch (packet.type) {
        case PacketType::AuthAccepted: {
            auto token = jsonString(payload, "jwt", "");
            bool shouldSaveSession = false;
            {
                std::lock_guard lock(m_mutex);
                m_state.authenticated = true;
                m_authInFlight = false;
                if (!token.empty()) {
                    m_state.authToken = token;
                    shouldSaveSession = true;
                }
            }
            if (shouldSaveSession) saveSessionData();
            publishConnection("Autenticado en PaiDraw");
            refreshLobby();
            break;
        }
        case PacketType::AuthRejected:
            publishConnection(jsonString(payload, "message", "Autenticacion rechazada"));
            break;
        case PacketType::RoomsListSnapshot:
            handleLobbySnapshot(payload);
            break;
        case PacketType::RoomStateSnapshot:
            handleRoomSnapshot(payload);
            break;
        case PacketType::ChatMessageReceive:
            handleChatPacket(payload);
            break;
        case PacketType::GuessFeedback:
            handleGuessFeedback(payload);
            break;
        case PacketType::RoundSync:
            handleRoundSync(payload);
            break;
        case PacketType::DrawStroke:
            handleStrokePacket(payload);
            break;
        case PacketType::ResultsSnapshot:
            handleResults(payload);
            break;
        case PacketType::PresenceUpdate:
            handlePresence(payload);
            break;
        case PacketType::Error:
            PaimonNotify::show(jsonString(payload, "message", "PaiDraw server error"), NotificationIcon::Warning);
            break;
        default:
            break;
    }
}

void PaiDrawManager::handleLobbySnapshot(matjson::Value const& payload) {
    std::vector<PlayerInfo> players;
    std::vector<RoomInfo> rooms;

    paimon::json::forEachInArray(payload["players"], [&](matjson::Value const& item) {
        players.push_back(parsePlayer(item));
    });
    paimon::json::forEachInArray(payload["rooms"], [&](matjson::Value const& item) {
        rooms.push_back(parseRoom(item));
    });

    {
        std::lock_guard lock(m_mutex);
        m_state.onlinePlayers = std::move(players);
        m_state.rooms = std::move(rooms);
        m_state.onlineCount = jsonInt(payload, "onlineCount", static_cast<int>(m_state.onlinePlayers.size()));
        m_state.offlinePreview = false;
    }
    publishLobby();
}

void PaiDrawManager::handleLobbyHttpSnapshot(matjson::Value const& payload) {
    auto version = jsonString(payload, "lobbyVersion", "");
    if (!version.empty()) {
        std::lock_guard lock(m_mutex);
        m_lobbyVersion = version;
    }
    handleLobbySnapshot(payload);
}

void PaiDrawManager::handleRoomSnapshot(matjson::Value const& payload) {
    auto room = parseRoom(payload);
    {
        std::lock_guard lock(m_mutex);
        m_state.currentRoom = room;
        m_state.currentRoomId = room.id;
        m_state.offlinePreview = false;
    }
    publishRoom();
}

void PaiDrawManager::handleRoomHttpSnapshot(matjson::Value const& payload) {
    auto room = parseRoom(payload);
    RoundState round = m_state.currentRound;
    std::vector<ChatMessage> chat;
    std::vector<StrokeSegment> strokes;
    MatchResults results = m_state.results;

    paimon::json::forEachInArray(payload["chat"], [&](matjson::Value const& item) {
        chat.push_back(parseChat(item));
    });
    paimon::json::forEachInArray(payload["recentStrokes"], [&](matjson::Value const& item) {
        strokes.push_back(parseStroke(item));
    });
    auto previousStrokeCount = snapshot().recentStrokes.size();
    if (payload.contains("round") && payload["round"].isObject()) {
        auto const& roundValue = payload["round"];
        round.currentRound = jsonInt(roundValue, "currentRound", 1);
        round.totalRounds = jsonInt(roundValue, "totalRounds", room.config.rounds);
        round.drawingPlayerId = static_cast<uint32_t>(jsonInt(roundValue, "drawingPlayerId", 0));
        round.drawingPlayerName = jsonString(roundValue, "drawingPlayerName", room.hostName);
        round.maskedWord = jsonString(roundValue, "maskedWord", "_ _ _");
        round.drawerWord = jsonString(roundValue, "drawerWord", "");
        round.revealedLetter = jsonString(roundValue, "revealedLetter", "");
        round.correctWord = jsonString(roundValue, "correctWord", "");
        round.timeLeftSeconds = jsonInt(roundValue, "timeLeftSeconds", 0);
        round.localPlayerIsDrawer = jsonBool(roundValue, "localPlayerIsDrawer", false);

        // Compute the absolute deadline in our LOCAL clock. The server
        // gives us its own `serverNowMs` and `endsAtMs`; we add the delta
        // to our local now() so the timer keeps ticking accurately even
        // when the two clocks drift. Falls back to deriving from
        // timeLeftSeconds when the server hasn't sent the new fields.
        uint64_t serverNow = jsonUInt64(roundValue, "serverNowMs", 0);
        uint64_t serverEndsAt = jsonUInt64(roundValue, "endsAtMs", 0);
        if (serverEndsAt > 0 && serverNow > 0) {
            uint64_t localNow = nowMs();
            int64_t remaining = static_cast<int64_t>(serverEndsAt) - static_cast<int64_t>(serverNow);
            if (remaining < 0) remaining = 0;
            round.endsAtLocalMs = localNow + static_cast<uint64_t>(remaining);
        } else if (round.timeLeftSeconds > 0) {
            round.endsAtLocalMs = nowMs() + static_cast<uint64_t>(round.timeLeftSeconds) * 1000ULL;
        } else {
            round.endsAtLocalMs = 0;
        }
    }
    if (payload.contains("results") && payload["results"].isObject()) {
        results = parseResults(payload["results"]);
    }

    // Detect end-of-round transitions before mutating state. We flush all
    // buffered strokes once when the round we were drawing/guessing on
    // closes (timer hit 0 server-side, or the round number advanced, or
    // the room dropped out of "EN JUEGO" entirely).
    bool shouldFlushStrokes = false;
    {
        auto previous = snapshot();
        bool wasInGame = previous.currentRoom.state == RoomState::InGame;
        bool nowInGame = room.state == RoomState::InGame;
        bool roundAdvanced = wasInGame && previous.currentRound.currentRound != round.currentRound;
        bool gameEnded = wasInGame && !nowInGame;
        if ((roundAdvanced || gameEnded) && previous.currentRoom.id == room.id) {
            shouldFlushStrokes = true;
        }
    }

    {
        std::lock_guard lock(m_mutex);
        m_state.currentRoom = room;
        m_state.currentRoomId = room.id;
        m_state.currentRound = round;
        m_state.roomChat = std::move(chat);
        m_state.recentStrokes = std::move(strokes);
        m_state.results = std::move(results);
        m_state.offlinePreview = false;

        auto roomVersion = jsonString(payload, "roomVersion", "");
        if (!roomVersion.empty()) {
            m_roomVersion = roomVersion;
        }
        auto lobbyVersion = jsonString(payload, "lobbyVersion", "");
        if (!lobbyVersion.empty()) {
            m_lobbyVersion = lobbyVersion;
        }
    }

    syncCurrentRoomIntoLobby();
    publishRoom();
    publishChat();
    publishRound();
    publishResults();

    if (strokes.size() > previousStrokeCount) {
        for (size_t i = previousStrokeCount; i < strokes.size(); ++i) {
            publishStroke(strokes[i]);
        }
    }

    // Flush the round's buffered drawing once the round transition is
    // confirmed by the server. queueStrokeFlush() collapses repeated calls
    // through a 80 ms scheduler window so even back-to-back snapshots only
    // fire one POST.
    if (shouldFlushStrokes) {
        queueStrokeFlush();
    }
}

void PaiDrawManager::handleChatPacket(matjson::Value const& payload) {
    auto message = parseChat(payload);
    {
        std::lock_guard lock(m_mutex);
        if (m_state.roomChat.size() >= kMaxChatMessages) {
            m_state.roomChat.erase(m_state.roomChat.begin());
        }
        m_state.roomChat.push_back(message);
    }
    publishChat();
}

void PaiDrawManager::handleGuessFeedback(matjson::Value const& payload) {
    auto status = jsonString(payload, "status", "");
    if (status.empty()) return;

    {
        std::lock_guard lock(m_mutex);
        if (lowercase(status) == "correct") {
            for (auto& player : m_state.currentRoom.players) {
                if (player.accountID == m_state.localAccountID) {
                    player.guessed = true;
                    player.score += jsonInt(payload, "points", 300);
                    break;
                }
            }
        }
    }
    publishRoom();
}

void PaiDrawManager::handleRoundSync(matjson::Value const& payload) {
    RoundState round;
    round.currentRound = jsonInt(payload, "currentRound", 1);
    round.totalRounds = jsonInt(payload, "totalRounds", 6);
    round.drawingPlayerId = static_cast<uint32_t>(jsonInt(payload, "drawingPlayerId", 0));
    round.drawingPlayerName = jsonString(payload, "drawingPlayerName", "");
    round.maskedWord = jsonString(payload, "maskedWord", "_ _ _");
    round.drawerWord = jsonString(payload, "drawerWord", "");
    round.correctWord = jsonString(payload, "correctWord", "");
    round.revealedLetter = jsonString(payload, "revealedLetter", "");
    round.timeLeftSeconds = jsonInt(payload, "timeLeftSeconds", 0);

    uint64_t serverNow = jsonUInt64(payload, "serverNowMs", 0);
    uint64_t serverEndsAt = jsonUInt64(payload, "endsAtMs", 0);
    if (serverEndsAt > 0 && serverNow > 0) {
        uint64_t localNow = nowMs();
        int64_t remaining = static_cast<int64_t>(serverEndsAt) - static_cast<int64_t>(serverNow);
        if (remaining < 0) remaining = 0;
        round.endsAtLocalMs = localNow + static_cast<uint64_t>(remaining);
    } else if (round.timeLeftSeconds > 0) {
        round.endsAtLocalMs = nowMs() + static_cast<uint64_t>(round.timeLeftSeconds) * 1000ULL;
    } else {
        round.endsAtLocalMs = 0;
    }

    {
        std::lock_guard lock(m_mutex);
        round.localPlayerIsDrawer = round.drawingPlayerId == m_state.localAccountID;
        m_state.currentRound = round;
    }
    publishRound();
}

void PaiDrawManager::handleStrokePacket(matjson::Value const& payload) {
    auto stroke = parseStroke(payload);
    {
        std::lock_guard lock(m_mutex);
        m_state.recentStrokes.push_back(stroke);
    }
    publishStroke(stroke);
}

void PaiDrawManager::handleResults(matjson::Value const& payload) {
    {
        std::lock_guard lock(m_mutex);
        m_state.results = parseResults(payload);

        auto games = jsonInt(m_state.stats, "gamesPlayed", 0) + 1;
        auto wins = jsonInt(m_state.stats, "wins", 0);
        auto best = jsonInt(m_state.stats, "bestScore", 0);

        m_state.stats["gamesPlayed"] = games;
        if (!m_state.results.leaderboard.empty() && m_state.results.leaderboard.front().accountID == m_state.localAccountID) {
            m_state.stats["wins"] = wins + 1;
        }
        for (auto const& player : m_state.results.leaderboard) {
            if (player.accountID == m_state.localAccountID) {
                m_state.stats["bestScore"] = std::max(best, player.score);
                break;
            }
        }
    }

    saveStats();
    publishResults();
}

void PaiDrawManager::handlePresence(matjson::Value const& payload) {
    auto status = jsonString(payload, "status", "");
    if (!status.empty()) {
        log::debug("[PaiDraw] Presence sync: {}", status);
    }
}

PlayerInfo PaiDrawManager::parsePlayer(matjson::Value const& value) const {
    PlayerInfo player;
    player.accountID = static_cast<uint32_t>(jsonInt(value, "accountID", 0));
    player.name = jsonString(value, "name", jsonString(value, "username", "Player"));
    player.level = jsonInt(value, "level", 0);
    player.iconID = std::max(1, jsonInt(value, "iconID", 1));
    player.iconType = jsonInt(value, "iconType", 0);
    player.color1 = jsonInt(value, "color1", 1);
    player.color2 = jsonInt(value, "color2", 2);
    player.glow = jsonBool(value, "glow", false);
    player.ready = jsonBool(value, "ready", false);
    player.host = jsonBool(value, "host", false);
    player.guessed = jsonBool(value, "guessed", false);
    player.pingMs = jsonInt(value, "pingMs", 0);
    player.score = jsonInt(value, "score", 0);
    player.status = parseStatus(jsonString(value, "status", "free"));
    return player;
}

RoomInfo PaiDrawManager::parseRoom(matjson::Value const& value) const {
    RoomInfo room;
    room.id = static_cast<uint32_t>(jsonInt(value, "id", 0));
    room.config.name = jsonString(value, "name", "PaiDraw Room");
    room.config.maxPlayers = jsonInt(value, "maxPlayers", 8);
    room.config.rounds = jsonInt(value, "rounds", 6);
    room.config.roundTimeSeconds = jsonInt(value, "roundTimeSeconds", 90);
    room.config.mode = parseMode(jsonString(value, "mode", "Clasico"));
    room.config.language = parseLanguage(jsonString(value, "language", "ES"));
    room.hostId = static_cast<uint32_t>(jsonInt(value, "hostId", 0));
    room.hostName = jsonString(value, "host", "Host");
    room.hasPassword = jsonBool(value, "hasPassword", false);
    room.state = parseRoomState(jsonString(value, "state", "ESPERANDO"));

    paimon::json::forEachInArray(value["players"], [&](matjson::Value const& playerValue) {
        room.players.push_back(parsePlayer(playerValue));
    });
    return room;
}

ChatMessage PaiDrawManager::parseChat(matjson::Value const& value) const {
    ChatMessage message;
    message.senderId = static_cast<uint32_t>(jsonInt(value, "senderId", 0));
    message.senderName = jsonString(value, "senderName", jsonString(value, "name", "Sistema"));
    message.text = jsonString(value, "text", jsonString(value, "message", ""));
    message.timestamp = jsonUInt64(value, "timestamp", nowMs());
    message.system = jsonBool(value, "system", false);
    message.correct = jsonBool(value, "correct", false);
    message.nearGuess = jsonBool(value, "nearGuess", false);
    return message;
}

StrokeSegment PaiDrawManager::parseStroke(matjson::Value const& value) const {
    StrokeSegment stroke;
    stroke.x1 = static_cast<float>(jsonDouble(value, "x1", 0.0));
    stroke.y1 = static_cast<float>(jsonDouble(value, "y1", 0.0));
    stroke.x2 = static_cast<float>(jsonDouble(value, "x2", 0.0));
    stroke.y2 = static_cast<float>(jsonDouble(value, "y2", 0.0));
    stroke.size = static_cast<float>(jsonDouble(value, "size", 8.0));
    stroke.color = {
        static_cast<GLubyte>(jsonInt(value, "colorR", 255)),
        static_cast<GLubyte>(jsonInt(value, "colorG", 255)),
        static_cast<GLubyte>(jsonInt(value, "colorB", 255)),
    };
    stroke.eraser = jsonBool(value, "eraser", false);
    return stroke;
}

MatchResults PaiDrawManager::parseResults(matjson::Value const& value) const {
    MatchResults results;
    paimon::json::forEachInArray(value["leaderboard"], [&](matjson::Value const& item) {
        results.leaderboard.push_back(parsePlayer(item));
    });
    results.bestDrawer = jsonString(value, "bestDrawer", "-");
    results.fastestGuesser = jsonString(value, "fastestGuesser", "-");
    results.hardestWord = jsonString(value, "hardestWord", "-");
    return results;
}

void PaiDrawManager::publishConnection(std::string const& message) {
    auto state = snapshot();
    paimon::EventBus::get().publish(ConnectionEvent {
        state.connected,
        state.authenticated,
        state.offlinePreview,
        message,
    });
}

void PaiDrawManager::publishLobby() {
    auto state = snapshot();
    paimon::EventBus::get().publish(LobbyUpdatedEvent {
        state.onlineCount,
        state.onlinePlayers,
        state.rooms,
    });
}

void PaiDrawManager::publishRoom() {
    paimon::EventBus::get().publish(RoomUpdatedEvent { snapshot().currentRoom });
}

void PaiDrawManager::publishChat() {
    paimon::EventBus::get().publish(ChatUpdatedEvent { snapshot().roomChat });
}

void PaiDrawManager::publishRound() {
    paimon::EventBus::get().publish(RoundUpdatedEvent { snapshot().currentRound });
}

void PaiDrawManager::publishResults() {
    paimon::EventBus::get().publish(ResultsUpdatedEvent { snapshot().results });
}

void PaiDrawManager::publishStroke(StrokeSegment const& stroke) {
    paimon::EventBus::get().publish(StrokeEvent { stroke });
}

void PaiDrawManager::syncCurrentRoomIntoLobby() {
    std::lock_guard lock(m_mutex);
    if (!m_state.currentRoomId) return;
    for (auto& room : m_state.rooms) {
        if (room.id == m_state.currentRoomId) {
            room = m_state.currentRoom;
            return;
        }
    }
    m_state.rooms.insert(m_state.rooms.begin(), m_state.currentRoom);
}

float PaiDrawManager::pollIntervalForState() const {
    auto state = snapshot();
    if (!state.connected || !state.authenticated) return 4.5f;
    if (state.currentRoomId != 0 && state.currentRoom.state == RoomState::InGame) return 0.9f;
    if (state.currentRoomId != 0) return 1.6f;
    return 3.5f;
}

void PaiDrawManager::schedulePollLoop() {
    auto generation = ++m_pollGeneration;
    paimon::scheduleMainThreadDelay(pollIntervalForState(), [this, generation]() {
        pollLoopTick(generation);
    });
}

void PaiDrawManager::pollLoopTick(uint32_t generation) {
    if (generation != m_pollGeneration || m_shuttingDown) return;

    auto state = snapshot();
    if (!state.connected || !state.authenticated) return;

    refreshLobby();

    if (state.currentRoomId != 0 && !m_roomRequestInFlight) {
        std::string path = fmt::format("/api/paidraw/rooms/{}", state.currentRoomId);
        if (!m_roomVersion.empty()) {
            path += fmt::format("?since={}", m_roomVersion);
        }

        m_roomRequestInFlight = true;
        requestJson("GET", path, nullptr, true,
            [this](bool ok, matjson::Value const& response, std::string const& error) {
                m_roomRequestInFlight = false;
                if (!ok) {
                    applyRequestFailure(error.empty() ? "No se pudo actualizar la sala" : error);
                    return;
                }

                if (jsonBool(response, "changed", true) == false) {
                    auto roomVersion = jsonString(response, "roomVersion", m_roomVersion);
                    auto lobbyVersion = jsonString(response, "lobbyVersion", m_lobbyVersion);
                    std::lock_guard lock(m_mutex);
                    if (!roomVersion.empty()) m_roomVersion = roomVersion;
                    if (!lobbyVersion.empty()) m_lobbyVersion = lobbyVersion;
                    return;
                }

                handleRoomHttpSnapshot(response);
            }
        );
    }

    schedulePollLoop();
}

void PaiDrawManager::queueStrokeFlush() {
    if (m_strokeFlushScheduled) return;
    m_strokeFlushScheduled = true;
    paimon::scheduleMainThreadDelay(0.08f, [this]() {
        flushPendingStrokes();
    });
}

void PaiDrawManager::flushPendingStrokes() {
    m_strokeFlushScheduled = false;

    auto state = snapshot();
    if (!state.connected || !state.authenticated || state.currentRoomId == 0) return;

    std::vector<StrokeSegment> pending;
    {
        std::lock_guard lock(m_mutex);
        if (m_pendingStrokes.empty()) return;
        pending.swap(m_pendingStrokes);
    }

    auto strokes = matjson::Value::array();
    for (auto const& stroke : pending) {
        strokes.push(matjson::makeObject({
            {"x1", stroke.x1},
            {"y1", stroke.y1},
            {"x2", stroke.x2},
            {"y2", stroke.y2},
            {"size", stroke.size},
            {"colorR", stroke.color.r},
            {"colorG", stroke.color.g},
            {"colorB", stroke.color.b},
            {"eraser", stroke.eraser},
        }));
    }

    auto payload = matjson::makeObject({
        {"type", "strokes"},
        {"strokes", strokes},
    });

    requestJson("POST", fmt::format("/api/paidraw/rooms/{}/action", state.currentRoomId), &payload, true,
        [this, pending = std::move(pending)](bool ok, matjson::Value const& response, std::string const& error) {
            if (!ok) {
                {
                    std::lock_guard lock(m_mutex);
                    m_pendingStrokes.insert(m_pendingStrokes.begin(), pending.begin(), pending.end());
                }
                applyRequestFailure(error.empty() ? "No se pudieron sincronizar los trazos" : error);
                return;
            }

            if (response.contains("recentStrokes")) {
                handleRoomHttpSnapshot(response);
            }
        }
    );
}

void PaiDrawManager::applyRequestFailure(std::string const& message, bool disconnectTransport) {
    if (disconnectTransport) {
        disconnect();
    }
    else {
        publishConnection(message);
    }
}

void PaiDrawManager::requestJson(
    std::string const& method,
    std::string const& path,
    matjson::Value const* body,
    bool requireAuth,
    geode::CopyableFunction<void(bool, matjson::Value const&, std::string const&)> callback
) {
    auto base = baseServerUrl();
    if (!base.empty() && base.back() == '/') {
        base.pop_back();
    }

    auto url = base + path;
    auto req = geode::utils::web::WebRequest();
    req.timeout(std::chrono::seconds(10));
    req.acceptEncoding("gzip, deflate");
    req.header("Accept", "application/json");

    if (requireAuth) {
        auto token = snapshot().authToken;
        if (!token.empty()) {
            req.header("Authorization", fmt::format("Bearer {}", token));
        }
    }

    if (body) {
        req.header("Content-Type", "application/json");
        req.bodyString(body->dump());
    }

    WebHelper::dispatch(std::move(req), method, url,
        [callback = std::move(callback)](geode::utils::web::WebResponse res) mutable {
            if (!callback) return;
            if (!res.ok()) {
                callback(false, matjson::Value::object(), fmt::format("HTTP {}", res.code()));
                return;
            }

            auto bodyText = res.string().unwrapOr("{}");
            auto parsed = matjson::parse(bodyText);
            if (!parsed) {
                callback(false, matjson::Value::object(), "Invalid JSON response");
                return;
            }

            callback(true, parsed.unwrap(), "");
        }
    );
}

} // namespace paidraw
