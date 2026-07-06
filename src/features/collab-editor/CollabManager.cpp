#include "CollabManager.hpp"

#include "CollabOverlay.hpp"
#include "CollabPopups.hpp"
#include "CollabVoice.hpp"

#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/loader/Mod.hpp>

using namespace geode::prelude;

namespace paimon::collab {

namespace {

// Max snapshot objects applied per editor tick (keeps frames smooth on join).
constexpr size_t kSnapshotApplyPerTick = 200;

// Editor tick period (LevelEditorLayer schedules collabTick at this rate).
constexpr float kTickInterval = 0.05f;

// Update-only batches are throttled to this period (~5 flushes/s). Batches
// containing adds/deletes flush on the next tick regardless.
constexpr float kUpdateFlushInterval = 0.2f;

// Selected-object reconcile: period in ticks and max selection size. Catches
// edits made through popups (colors, groups, z-order) and undo shuffles that
// no direct hook covers, at the cost of a save-string per selected object.
constexpr int kReconcileEveryTicks = 10;
constexpr size_t kReconcileMaxObjects = 500;

constexpr size_t kChatLogCap = 100;

std::string normalizeBaseUrl(std::string base) {
    geode::utils::string::trimIP(base);
    while (!base.empty() && base.back() == '/') base.pop_back();
    if (!base.starts_with("http://") && !base.starts_with("https://")) {
        base = "https://" + base;
    }
    return base;
}

} // namespace

CollabManager& CollabManager::get() {
    static CollabManager instance;
    return instance;
}

CollabManager::CollabManager() = default;

CollabManager::~CollabManager() {
    disconnect();
}

void CollabManager::connect(std::string const& roomCode, std::string const& username, ConnectMode mode, GJGameLevel* hostLevel) {
    // Guard against a footgun: if you already host this room and try to "join"
    // it, the disconnect() below would leave as host and the server closes the
    // room for everyone (dropClient -> host_left). Just drop back into your
    // host editor instead. You can't be host and peer of the same room from one
    // client.
    if (mode == ConnectMode::Join && m_isHost && m_state == ConnState::Connected && m_roomCode == roomCode) {
        log::info("[Collab] already hosting room={}, reopening host editor instead of self-join", roomCode);
        setStatus("Ya eres el host de esta sala. Volviendo a tu editor...");
        openHostEditor();
        return;
    }

    disconnect();

    m_hostLevel = hostLevel;
    m_roomCode = roomCode;
    m_username = username.empty() ? "editor" : username;
    m_clientId = 0;
    m_isHost = false;
    m_serverSeq = 0;
    m_localSeq = 1;
    m_applyingRemote = false;
    m_permissions = {};
    m_peers.clear();
    m_chat.clear();
    ++m_chatRevision;

    m_uidToGid.clear();
    m_gidToObj.clear();
    m_versionByGid.clear();
    m_lastSentSave.clear();
    m_pendingOps.clear();
    m_pendingIndexByGid.clear();
    m_sinceFlush = 0.f;
    m_pendingStructural = false;
    m_applyQueue.clear();
    m_snapshotReceived = 0;
    m_snapshotComplete = false;
    m_seeded = false;
    m_seeding = false;
    m_seedIdleTicks = 0;
    m_seedTotalTicks = 0;
    m_reconcileTicks = 0;
    m_joinerEditorOpened = false;
    m_needsResyncOnEntry = false;
    m_recovering = false;
    m_recoverAttempts = 0;
    m_lastRecoverAt = {};

    // Server URL is fixed: pasting it into the mod settings tends to mangle the
    // value, so we always use the known Render host.
    std::string base = normalizeBaseUrl(kServerBaseUrl);

    m_net.setCallbacks(
        [this](matjson::Value const& msg) { onMessage(msg); },
        [this](ConnState st, std::string const& m) { onState(st, m); }
    );

    m_state = ConnState::Connecting;
    setStatus("Conectando... (puede tardar si el servidor estaba dormido)");
    log::info("[Collab] connect room={} mode={} host={}", m_roomCode,
              mode == ConnectMode::Create ? "create" : "join", m_hostLevel != nullptr);
    m_net.start(base, m_roomCode, m_username, mode);
}

void CollabManager::disconnect() {
    bool wasActive = m_state != ConnState::Disconnected;
    m_recovering = false;
    CollabVoice::get().stopAll();
    m_net.stop();
    m_state = ConnState::Disconnected;
    m_clientId = 0;
    m_isHost = false;
    m_peers.clear();
    m_pendingOps.clear();
    m_pendingIndexByGid.clear();
    m_pendingStructural = false;
    m_applyQueue.clear();
    discardJoinerLevel();
    m_hostLevel = nullptr;
    if (wasActive) m_status = "Collab apagado";
}

void CollabManager::closeRoom() {
    if (!m_isHost || m_state == ConnState::Disconnected) {
        disconnect();
        return;
    }
    CollabVoice::get().stopAll();
    m_net.closeRoom();
    m_state = ConnState::Disconnected;
    m_clientId = 0;
    m_isHost = false;
    m_peers.clear();
    m_pendingOps.clear();
    m_pendingIndexByGid.clear();
    m_pendingStructural = false;
    m_applyQueue.clear();
    m_status = "Sala cerrada";
    log::info("[Collab] {}", m_status);
}

ConnState CollabManager::state() const { return m_state; }
bool CollabManager::connected() const { return m_state == ConnState::Connected; }
bool CollabManager::isHost() const { return m_isHost; }
bool CollabManager::isApplyingRemote() const { return m_applyingRemote; }
HostPermissions CollabManager::permissions() const { return m_permissions; }
std::string CollabManager::status() const { return m_status; }
std::string CollabManager::roomCode() const { return m_roomCode; }
int CollabManager::peerCount() const { return static_cast<int>(m_peers.size()); }

std::vector<PeerInfo> CollabManager::peers() const {
    std::vector<PeerInfo> out;
    out.reserve(m_peers.size());
    for (auto const& [id, info] : m_peers) out.push_back(info);
    std::sort(out.begin(), out.end(), [](PeerInfo const& a, PeerInfo const& b) {
        if (a.isHost != b.isHost) return a.isHost;
        return a.clientId < b.clientId;
    });
    return out;
}

std::string CollabManager::peerName(int clientId) const {
    auto it = m_peers.find(clientId);
    if (it != m_peers.end() && !it->second.username.empty()) return it->second.username;
    return fmt::format("editor #{}", clientId);
}

void CollabManager::setStatus(std::string message) {
    m_status = std::move(message);
    log::info("[Collab] {}", m_status);
}

void CollabManager::setEditor(LevelEditorLayer* editor) {
    m_editor = editor;
    // Re-entering the editor with the room still open: rebuild object state
    // from the server instead of running the first-join seed/snapshot path.
    if (editor && connected() && m_needsResyncOnEntry) {
        m_needsResyncOnEntry = false;
        // The room now mirrors whatever level the host reopened (keeps the
        // "live" dot on the right cell too).
        if (m_isHost && editor->m_level) m_hostLevel = editor->m_level;
        beginResync();
    }
}

void CollabManager::clearEditor(LevelEditorLayer* editor) {
    if (m_editor != editor) return;
    m_editor = nullptr;
    m_overlay = nullptr;
    CollabVoice::get().stopAll();

    // Joiners edit a throwaway preview level, so leaving it just drops them.
    // The host's room stays open so they can pop back into their level; the
    // per-editor object state (dead GameObject pointers) is reset and rebuilt
    // on re-entry via a resync.
    if (connected() && m_isHost) {
        resetEditorState();
        m_needsResyncOnEntry = true;
        setStatus(fmt::format("Sala '{}' activa (fuera del editor)", m_roomCode));
    } else {
        disconnect();
    }
}

void CollabManager::setOverlay(CollabEditorOverlay* overlay) {
    m_overlay = overlay;
}

void CollabManager::clearOverlay(CollabEditorOverlay* overlay) {
    // Only clear if it's still the overlay we know about: during a scene
    // transition a new overlay can register before the old one is destroyed.
    if (m_overlay == overlay) m_overlay = nullptr;
}

bool CollabManager::shouldEmit() const {
    return m_state == ConnState::Connected && m_clientId > 0 && !m_applyingRemote && m_editor;
}

std::string CollabManager::makeLocalGid() {
    return fmt::format("{}:{}", m_clientId, m_localSeq++);
}

void CollabManager::mapGid(std::string const& gid, GameObject* object) {
    if (!object) return;
    m_gidToObj[gid] = object;
    m_uidToGid[object->m_uniqueID] = gid;
}

void CollabManager::unmapGid(std::string const& gid) {
    auto it = m_gidToObj.find(gid);
    if (it != m_gidToObj.end()) {
        if (it->second) m_uidToGid.erase(it->second->m_uniqueID);
        m_gidToObj.erase(it);
    }
    m_lastSentSave.erase(gid);
}

std::string CollabManager::saveObject(GameObject* object) const {
    if (!m_editor || !object) return {};
    return std::string(object->getSaveString(m_editor));
}

GameObject* CollabManager::findTrackedObject(std::string const& gid) const {
    auto it = m_gidToObj.find(gid);
    return it != m_gidToObj.end() ? it->second.data() : nullptr;
}

} // namespace paimon::collab

// ---------------------------------------------------------------------------
// Network callbacks (main thread) + tick
// ---------------------------------------------------------------------------

namespace paimon::collab {

void CollabManager::onState(ConnState st, std::string const& message) {
    m_state = st;
    setStatus(message);

    if (st == ConnState::Disconnected) {
        m_clientId = 0;
        m_isHost = false;
    }
}

void CollabManager::onMessage(matjson::Value const& msg) {
    // Callbacks run on the main thread (WebHelper guarantees it). Control
    // messages (join_ok/peers/perms/error) are light and must be handled even
    // before the editor exists, otherwise a joiner never learns it joined and
    // never gets dropped into the editor. Object ops are only parsed here into
    // m_applyQueue; the heavy createObjectsFromString work stays in tick().
    handleMessage(msg);
}

void CollabManager::tick() {
    // 1) Apply remote ops only when the editor exists (so connecting before
    //    entering the editor never drops objects). Budgeted to keep frames
    //    smooth on large snapshots.
    if (m_editor) {
        if (m_seeding) {
            size_t added = seedFromEditor();
            ++m_seedTotalTicks;
            m_seedIdleTicks = added > 0 ? 0 : m_seedIdleTicks + 1;
            // Stop once no new objects have shown up for a while (objects can
            // stream in over several frames), with a hard cap as a safety net.
            if (m_seedIdleTicks >= 20 || m_seedTotalTicks >= 200) m_seeding = false;
        }
        size_t applied = 0;
        while (!m_applyQueue.empty() && applied < kSnapshotApplyPerTick) {
            auto op = std::move(m_applyQueue.front());
            m_applyQueue.pop_front();
            if (op.kind == "delete") {
                applyRemoteDelete(op);
            } else if (op.kind == "update") {
                applyRemoteUpdate(op);
            } else {
                applyRemoteAdd(op);
            }
            ++applied;
        }

        // 2) Catch edits with no direct hook (edit popups, color/group changes,
        //    undo shuffles): re-send selected objects whose save changed.
        if (connected() && ++m_reconcileTicks >= kReconcileEveryTicks) {
            m_reconcileTicks = 0;
            if (m_editor->m_editorUI) {
                auto* selected = m_editor->m_editorUI->getSelectedObjects();
                if (selected && selected->count() > 0 && selected->count() <= kReconcileMaxObjects) {
                    reconcileObjects(selected);
                }
            }
        }
    }

    // 3) Voice capture (frames are produced/relayed from here).
    CollabVoice::get().update(kTickInterval);

    // 4) Flush outgoing edits: instantly when something structural (add or
    //    delete) is pending, throttled for pure update streams (drags).
    if (connected() && !m_pendingOps.empty()) {
        m_sinceFlush += kTickInterval;
        if (m_pendingStructural || m_sinceFlush >= kUpdateFlushInterval) {
            flushOutgoing();
        }
    }
}

void CollabManager::handleMessage(matjson::Value const& msg) {
    std::string t = msg.contains("t") ? msg["t"].asString().unwrapOr("") : "";
    if (t.empty()) return;

    if (t == "join_ok") {
        bool wasRecovering = m_recovering;
        m_clientId = static_cast<int>(msg["clientId"].asInt().unwrapOr(0));
        m_isHost = msg.contains("isHost") && msg["isHost"].asBool().unwrapOr(false);
        m_serverSeq = static_cast<uint64_t>(msg["seq"].asInt().unwrapOr(0));
        if (msg.contains("permissions")) m_permissions = HostPermissions::fromJson(msg["permissions"]);
        if (msg.contains("peers")) handlePeerList(msg["peers"]);
        m_snapshotReceived = 0;
        m_snapshotComplete = false;
        m_seeded = false;
        m_seeding = false;
        m_seedIdleTicks = 0;
        m_seedTotalTicks = 0;
        setStatus(fmt::format("En sala '{}' como editor #{}{}", m_roomCode, m_clientId, m_isHost ? " (host)" : ""));
        if (m_recovering) {
            m_recovering = false;
            m_recoverAttempts = 0;
            pushChatMessage({0, "", "Conexion restablecida"});
        } else {
            pushChatMessage({0, "", fmt::format("Conectado a la sala {}", m_roomCode)});
        }
        m_recoverAttempts = 0;

        if (m_isHost) {
            // Host doesn't receive a snapshot (create-room seeds server state
            // from what we upload). Kick off seeding immediately; tick() waits
            // for the editor and streams objects via op_batch.
            m_seeded = true;
            m_seeding = true;
            m_snapshotComplete = true;
            // Rooms are usually created from the level info screen, so drop the
            // host into their own level's editor (seeding starts once it's up).
            // Not on a recovery though: never yank the user into a new scene —
            // if they left the editor, re-entry re-seeds via the resync path.
            log::info("[Collab] join_ok -> host path (recovered={})", wasRecovering);
            if (!wasRecovering) openHostEditor();
        } else {
            log::info("[Collab] join_ok -> joiner path (recovered={})", wasRecovering);
            if (!wasRecovering) openJoinerEditor();
        }
        return;
    }

    if (t == "snapshot") {
        int idx = static_cast<int>(msg["chunkIndex"].asInt().unwrapOr(0));
        int cnt = static_cast<int>(msg["chunkCount"].asInt().unwrapOr(1));
        if (msg.contains("objects")) {
            if (auto arr = msg["objects"].asArray()) {
                for (auto const& item : arr.unwrap()) {
                    if (!item.isObject()) continue;
                    ApplyObj op;
                    op.kind = "add";
                    op.gid = item["gid"].asString().unwrapOr("");
                    op.save = item["save"].asString().unwrapOr("");
                    op.version = static_cast<uint32_t>(item["version"].asInt().unwrapOr(0));
                    m_applyQueue.push_back(std::move(op));
                    ++m_snapshotReceived;
                }
            }
        }
        if (idx >= cnt - 1) {
            m_snapshotComplete = true;
            // If the room was empty and we are the host, seed it with our level
            // (deferred until the editor exists; handled in tick()).
            if (m_isHost && m_snapshotReceived == 0 && !m_seeded) {
                m_seeded = true;
                m_seeding = true;
                m_seedIdleTicks = 0;
                m_seedTotalTicks = 0;
            }
        }
        return;
    }

    if (t == "op_batch") {
        m_serverSeq = static_cast<uint64_t>(msg["seq"].asInt().unwrapOr(m_serverSeq));
        int origin = static_cast<int>(msg["origin"].asInt().unwrapOr(0));
        std::string by = msg.contains("by") ? msg["by"].asString().unwrapOr("") : "";
        if (auto arr = msg["ops"].asArray()) {
            for (auto const& item : arr.unwrap()) {
                if (!item.isObject()) continue;
                ApplyObj op;
                op.kind = item["kind"].asString().unwrapOr("");
                op.gid = item["gid"].asString().unwrapOr("");
                op.version = static_cast<uint32_t>(item["version"].asInt().unwrapOr(0));
                op.save = item["save"].asString().unwrapOr("");
                op.origin = origin;
                op.by = by;
                if (op.gid.empty() || op.kind.empty()) continue;
                m_applyQueue.push_back(std::move(op));
            }
        }
        return;
    }

    if (t == "resync_ready") {
        // Server cleared the room for our resync request: re-upload the level
        // the host just re-opened (streamed from tick()).
        if (m_isHost) {
            m_seeded = true;
            m_seeding = true;
            m_snapshotComplete = true;
            m_seedIdleTicks = 0;
            m_seedTotalTicks = 0;
        }
        return;
    }

    if (t == "resync") {
        // The host re-seeded the room from a fresh editor: drop our local copy
        // and rebuild from the incoming ops.
        m_versionByGid.clear();
        m_applyQueue.clear();
        m_uidToGid.clear();
        m_gidToObj.clear();
        m_lastSentSave.clear();
        wipeEditorObjects();
        return;
    }

    if (t == "chat") {
        ChatMessage cm;
        cm.from = static_cast<int>(msg["from"].asInt().unwrapOr(0));
        cm.name = msg["name"].asString().unwrapOr("");
        cm.text = msg["text"].asString().unwrapOr("");
        if (!cm.text.empty()) pushChatMessage(std::move(cm));
        return;
    }

    if (t == "voice") {
        int from = static_cast<int>(msg["from"].asInt().unwrapOr(0));
        if (from > 0 && from != m_clientId) {
            std::string name = msg["name"].asString().unwrapOr("");
            std::string data = msg["data"].asString().unwrapOr("");
            if (!data.empty()) {
                CollabVoice::get().onRemoteFrame(from, name.empty() ? peerName(from) : name, data);
            }
        }
        return;
    }

    if (t == "perms") {
        if (msg.contains("permissions")) m_permissions = HostPermissions::fromJson(msg["permissions"]);
        return;
    }

    if (t == "peers") {
        if (msg.contains("peers")) handlePeerList(msg["peers"]);
        return;
    }

    if (t == "error") {
        std::string code = msg["code"].asString().unwrapOr("");
        std::string message = msg["message"].asString().unwrapOr("Error");

        // Session lost: the server restarted or dropped us (common on the free
        // tier). Try to recover transparently first — the host re-creates the
        // room with the same code (the server lets it reclaim it) and a peer
        // re-joins and rebuilds from a fresh snapshot. Only tear down when
        // recovery isn't possible or keeps failing.
        if (code == "not_joined") {
            if (m_state == ConnState::Disconnected) return;
            if (tryRecoverSession()) return;
            teardownAndNotify("Se perdio la conexion con la sala y no se pudo recuperar. Vuelve a conectar.");
            return;
        }

        setStatus(fmt::format("Error: {}", message));
        if (code == "bad_access_code" || code == "room_full" || code == "bad_room" ||
            code == "room_not_found" || code == "room_exists" ||
            code == "create_failed" || code == "join_failed") {
            // teardownAndNotify handles every context: it pops the joiner's
            // editor scene when a recovery attempt fails mid-session, and for
            // a plain failed connect it just shows the alert.
            m_recovering = false;
            teardownAndNotify(message);
        }
        return;
    }

    if (t == "room_closed") {
        std::string reason = msg["reason"].asString().unwrapOr("");
        std::string text = (reason == "host_closed")
            ? "El host cerro la sala."
            : "El host se fue, la sala se cerro.";
        teardownAndNotify(text);
        return;
    }

    // op_ack / pong: nothing to do.
}

void CollabManager::handlePeerList(matjson::Value const& peersJson) {
    auto arr = peersJson.asArray();
    if (!arr) return;

    std::unordered_map<int, PeerInfo> next;
    for (auto const& item : arr.unwrap()) {
        if (!item.isObject()) continue;
        PeerInfo info;
        info.clientId = static_cast<int>(item["clientId"].asInt().unwrapOr(0));
        info.username = item["username"].asString().unwrapOr("");
        info.isHost = item.contains("isHost") && item["isHost"].asBool().unwrapOr(false);
        if (info.clientId > 0) next[info.clientId] = info;
    }

    // Join/leave notices (skip the very first list and ourselves).
    if (!m_peers.empty()) {
        for (auto const& [id, info] : next) {
            if (id != m_clientId && !m_peers.count(id)) {
                pushChatMessage({0, "", fmt::format("{} se unio a la sala", info.username)});
            }
        }
        for (auto const& [id, info] : m_peers) {
            if (id != m_clientId && !next.count(id)) {
                pushChatMessage({0, "", fmt::format("{} salio de la sala", info.username)});
                CollabVoice::get().dropPeer(id);
            }
        }
    }

    m_peers = std::move(next);
}

void CollabManager::pushChatMessage(ChatMessage msg) {
    m_chat.push_back(msg);
    while (m_chat.size() > kChatLogCap) m_chat.pop_front();
    ++m_chatRevision;
    if (m_overlay) m_overlay->onChat(msg);
}

std::vector<ChatMessage> CollabManager::recentChat(size_t maxCount) const {
    size_t n = std::min(maxCount, m_chat.size());
    return {m_chat.end() - static_cast<long>(n), m_chat.end()};
}

void CollabManager::sendChat(std::string const& text) {
    if (!connected() || text.empty()) return;
    m_net.sendJson(matjson::makeObject({
        {"t", "chat"},
        {"text", text},
    }));
}

void CollabManager::inviteUser(int accountId, std::string const& /*targetName*/, InviteCb cb) {
    if (!connected() || !m_isHost) {
        if (cb) cb(false, false, "Solo el host puede invitar");
        return;
    }
    if (accountId <= 0) {
        if (cb) cb(false, false, "Cuenta invalida");
        return;
    }
    m_net.sendInvite(accountId, m_username, std::move(cb));
}

void CollabManager::sendVoiceFrame(uint32_t seq, std::string const& b64) {
    if (!connected() || b64.empty()) return;
    m_net.sendJson(matjson::makeObject({
        {"t", "voice"},
        {"seq", static_cast<int64_t>(seq)},
        {"data", b64},
    }));
}

bool CollabManager::tryRecoverSession() {
    // A peer with no editor has nothing to rebuild into; the host can recover
    // even from outside the editor (the room is reclaimed empty and re-seeded
    // on the next editor entry through the resync path).
    if (!m_isHost && !m_editor) return false;

    auto nowTp = std::chrono::steady_clock::now();
    if (m_lastRecoverAt.time_since_epoch().count() != 0 &&
        nowTp - m_lastRecoverAt > std::chrono::minutes(2)) {
        m_recoverAttempts = 0; // old failures no longer count against us
    }
    if (m_recoverAttempts >= 3) return false;
    ++m_recoverAttempts;
    m_lastRecoverAt = nowTp;

    bool asHost = m_isHost;
    m_recovering = true;

    // Object state belongs to the dead session. The host re-uploads its level
    // once the room is back (the join_ok host path arms seeding); a peer wipes
    // its local copy and rebuilds from the snapshot the server re-sends.
    resetEditorState();
    if (!asHost) wipeEditorObjects();

    log::info("[Collab] session lost, recovery attempt {} as {} room={}",
              m_recoverAttempts, asHost ? "host" : "peer", m_roomCode);
    m_net.restart(asHost ? ConnectMode::Create : ConnectMode::Join);
    setStatus(asHost ? "Se perdio la sesion; recreando tu sala..."
                     : "Se perdio la sesion; reconectando a la sala...");
    return true;
}

void CollabManager::teardownAndNotify(std::string const& text) {
    bool wasJoiner = m_joinerEditorOpened && !m_isHost;

    setStatus(text);

    // Server already dropped us; tear down without another /api/leave round-trip
    // being needed for correctness (stop() still best-effort sends one).
    m_state = ConnState::Disconnected;
    m_clientId = 0;
    m_isHost = false;
    m_peers.clear();
    m_pendingOps.clear();
    m_pendingIndexByGid.clear();
    m_pendingStructural = false;
    m_applyQueue.clear();
    CollabVoice::get().stopAll();
    m_net.stop();

    queueInMainThread([text, wasJoiner]() {
        // Pop the joiner editor scene we pushed on join, then show the notice
        // one frame later so it lands on the destination scene (popScene only
        // swaps at end of frame; showing the alert now would attach it to the
        // editor scene that's about to be destroyed).
        if (wasJoiner && LevelEditorLayer::get()) {
            CCDirector::sharedDirector()->popScene();
        }
        queueInMainThread([text, wasJoiner]() {
            // After a scene swap, a collab popup left open on the destination
            // scene has broken touch priority; remove it before alerting. When
            // no swap happened (plain failed connect) the popup stays open so
            // the user keeps their typed code.
            if (wasJoiner) closeSessionPopups();
            FLAlertLayer::create("Collab Editor", text.c_str(), "OK")->show();
        });
    });
}

void CollabManager::discardJoinerLevel() {
    if (!m_joinerLevel) return;
    Ref<GJGameLevel> level = m_joinerLevel;
    m_joinerLevel = nullptr;
    m_joinerEditorOpened = false;
    // The joiner's local copy is only a preview of the host's level; delete it
    // once the editor scene is fully gone so it doesn't pile up in My Levels.
    queueInMainThread([level]() {
        queueInMainThread([level]() {
            if (LevelEditorLayer::get()) return; // still (or again) editing
            if (auto* glm = GameLevelManager::get()) glm->deleteLevel(level);
        });
    });
}

} // namespace paimon::collab

// ---------------------------------------------------------------------------
// Outgoing edits
// ---------------------------------------------------------------------------

namespace paimon::collab {

void CollabManager::enqueueOp(std::string kind, std::string const& gid, uint32_t version, std::string save) {
    if (m_pendingOps.size() >= kMaxOpsPerFlush) flushOutgoing();

    if (kind == "update") {
        auto it = m_pendingIndexByGid.find(gid);
        if (it != m_pendingIndexByGid.end()) {
            auto& op = m_pendingOps[it->second];
            if (op.kind != "delete") {
                op.version = version;
                op.save = std::move(save);
                return;
            }
        }
        m_pendingIndexByGid[gid] = m_pendingOps.size();
        m_pendingOps.push_back({std::move(kind), gid, version, std::move(save)});
        return;
    }

    m_pendingStructural = true;

    if (kind == "delete") {
        m_pendingIndexByGid.erase(gid);
        m_pendingOps.push_back({std::move(kind), gid, version, std::move(save)});
        return;
    }

    // add
    m_pendingIndexByGid[gid] = m_pendingOps.size();
    m_pendingOps.push_back({std::move(kind), gid, version, std::move(save)});
}

void CollabManager::flushOutgoing() {
    m_sinceFlush = 0.f;
    m_pendingStructural = false;
    if (m_pendingOps.empty()) return;
    if (!m_net.isOpen()) return; // keep them until the socket is ready

    auto ops = matjson::Value::array();
    for (auto const& op : m_pendingOps) {
        ops.push(matjson::makeObject({
            {"kind", op.kind},
            {"gid", op.gid},
            {"version", static_cast<int64_t>(op.version)},
            {"save", op.save},
        }));
    }
    size_t opCount = m_pendingOps.size();
    m_pendingOps.clear();
    m_pendingIndexByGid.clear();

    // Timestamp the send so it can be compared against the server's "ops recv"
    // line to measure the real network latency.
    log::info("[Collab] enviando {} op(s)", opCount);

    m_net.sendJson(matjson::makeObject({
        {"t", "op_batch"},
        {"ops", ops},
    }));
}

void CollabManager::openJoinerEditor() {
    // The host keeps editing whatever level they opened (it seeds the room);
    // only joiners get pulled into a fresh editor. Guard against opening twice.
    if (m_isHost || m_joinerEditorOpened) {
        log::info("[Collab] openJoinerEditor skipped (isHost={} alreadyOpened={})", m_isHost, m_joinerEditorOpened);
        return;
    }
    // A live editor is the only real reason not to push one. Trust the engine's
    // LevelEditorLayer::get() here rather than m_editor, which can lag behind a
    // scene teardown and would otherwise strand the joiner on the popup.
    if (LevelEditorLayer::get()) {
        log::info("[Collab] openJoinerEditor: editor already live, staying");
        m_joinerEditorOpened = true;
        return;
    }
    m_joinerEditorOpened = true;

    std::string room = m_roomCode;
    Loader::get()->queueInMainThread([room]() {
        auto& mgr = CollabManager::get();
        if (LevelEditorLayer::get()) return;

        auto* glm = GameLevelManager::get();
        auto* level = glm ? glm->createNewLevel() : nullptr;
        auto* scene = level ? LevelEditorLayer::scene(level, false) : nullptr;
        if (!scene) {
            // Don't strand the joiner: let them retry and tell them what broke.
            log::error("[Collab] openJoinerEditor failed (glm={} level={} scene=null)",
                       (void*)glm, (void*)level);
            mgr.m_joinerEditorOpened = false;
            mgr.setStatus("No se pudo abrir el editor. Reintenta conectar.");
            FLAlertLayer::create("Collab Editor",
                "No se pudo abrir el editor de la sala. Vuelve a intentarlo.", "OK")->show();
            return;
        }

        level->m_levelName = room.empty() ? gd::string("Collab") : gd::string("Collab " + room);
        level->m_levelType = GJLevelType::Editor;
        mgr.m_joinerLevel = level;

        log::info("[Collab] opening joiner editor for room={}", room);
        // Close the connect popup before pushing the editor: left open, it
        // survives in the scene below with broken touch priority (the frozen
        // popup users hit when coming back from the room).
        closeSessionPopups();
        CCDirector::get()->pushScene(CCTransitionFade::create(0.5f, scene));
    });
}

void CollabManager::openHostEditor() {
    // Only when we host and aren't editing yet: the room is typically created
    // from the level info screen, so we open the level the host picked. If an
    // editor is already up (host started collab from inside it), keep it.
    if (!m_isHost || LevelEditorLayer::get()) return;
    if (!m_hostLevel) {
        // Room was created without a level to open (e.g. from the unlock gate):
        // there's nothing to seed, so tell the host instead of stranding them.
        log::warn("[Collab] openHostEditor: no host level to open");
        setStatus(fmt::format("Sala '{}' creada. Abre un nivel para editar.", m_roomCode));
        return;
    }

    Ref<GJGameLevel> level = m_hostLevel;
    Loader::get()->queueInMainThread([level]() {
        if (LevelEditorLayer::get()) return;
        auto* scene = LevelEditorLayer::scene(level, false);
        if (!scene) {
            log::error("[Collab] openHostEditor: scene creation failed");
            return;
        }
        log::info("[Collab] opening host editor");
        closeSessionPopups(); // same stale-popup guard as the joiner path
        CCDirector::get()->pushScene(CCTransitionFade::create(0.5f, scene));
    });
}

void CollabManager::resetEditorState() {
    m_uidToGid.clear();
    m_gidToObj.clear();
    m_versionByGid.clear();
    m_lastSentSave.clear();
    m_pendingOps.clear();
    m_pendingIndexByGid.clear();
    m_pendingStructural = false;
    m_applyQueue.clear();
    m_sinceFlush = 0.f;
    m_seeding = false;
    m_seedIdleTicks = 0;
    m_seedTotalTicks = 0;
    m_reconcileTicks = 0;
}

void CollabManager::wipeEditorObjects() {
    if (!m_editor) return;
    m_applyingRemote = true;
    if (m_editor->m_editorUI) m_editor->m_editorUI->deselectAll();
    m_editor->removeAllObjects();
    m_applyingRemote = false;
}

void CollabManager::beginResync() {
    resetEditorState();
    m_snapshotComplete = false;
    m_snapshotReceived = 0;
    if (m_isHost) {
        // Host keeps its freshly reloaded level and re-seeds it; the server
        // clears the room first (via /api/resync) so nothing duplicates.
        m_seeded = true;
        m_seeding = false; // armed by the resync_ready reply
    } else {
        // Peer pulls the current snapshot again and rebuilds from scratch.
        wipeEditorObjects();
        m_seeded = false;
    }
    m_net.requestResync();
}

size_t CollabManager::seedFromEditor() {
    if (!m_editor || !m_editor->m_objects) return 0;
    size_t count = 0;
    for (auto* o : CCArrayExt<GameObject*>(m_editor->m_objects)) {
        if (!o) continue;
        if (m_uidToGid.count(o->m_uniqueID)) continue;
        std::string save = saveObject(o);
        if (save.empty()) continue;
        std::string gid = makeLocalGid();
        mapGid(gid, o);
        m_versionByGid[gid] = 1;
        m_lastSentSave[gid] = save;
        enqueueOp("add", gid, 1, std::move(save));
        ++count;
    }
    if (count > 0) setStatus(fmt::format("Subiendo {} objetos a la sala...", count));
    return count;
}

void CollabManager::sendCreatedObject(GameObject* object) {
    if (!shouldEmit() || !object) return;
    if (m_uidToGid.count(object->m_uniqueID)) return;
    std::string save = saveObject(object);
    if (save.empty()) return;
    std::string gid = makeLocalGid();
    mapGid(gid, object);
    m_versionByGid[gid] = 1;
    m_lastSentSave[gid] = save;
    enqueueOp("add", gid, 1, std::move(save));
}

void CollabManager::sendCreatedObjects(CCArray* objects) {
    if (!objects) return;
    for (auto* o : CCArrayExt<GameObject*>(objects)) sendCreatedObject(o);
}

void CollabManager::sendUpdatedObject(GameObject* object) {
    if (!shouldEmit() || !object) return;
    std::string save = saveObject(object);
    if (save.empty()) return;

    auto it = m_uidToGid.find(object->m_uniqueID);
    if (it == m_uidToGid.end()) {
        // Not tracked yet (created before joining or via an unhooked path):
        // register it as an add.
        std::string gid = makeLocalGid();
        mapGid(gid, object);
        m_versionByGid[gid] = 1;
        m_lastSentSave[gid] = save;
        enqueueOp("add", gid, 1, std::move(save));
        return;
    }
    std::string gid = it->second;

    // No-op updates (drag callbacks and reconcile passes fire a lot more often
    // than objects actually change) are skipped entirely.
    auto last = m_lastSentSave.find(gid);
    if (last != m_lastSentSave.end() && last->second == save) return;

    uint32_t version = ++m_versionByGid[gid];
    m_lastSentSave[gid] = save;
    enqueueOp("update", gid, version, std::move(save));
}

void CollabManager::sendUpdatedObjects(CCArray* objects) {
    if (!objects) return;
    for (auto* o : CCArrayExt<GameObject*>(objects)) sendUpdatedObject(o);
}

void CollabManager::reconcileObjects(CCArray* objects) {
    if (!objects) return;
    for (auto* obj : CCArrayExt<CCObject*>(objects)) {
        if (auto* o = typeinfo_cast<GameObject*>(obj)) sendUpdatedObject(o);
    }
}

void CollabManager::sendDeletedObject(GameObject* object, std::string const& /*beforeSave*/) {
    if (!shouldEmit() || !object) return;
    auto it = m_uidToGid.find(object->m_uniqueID);
    if (it == m_uidToGid.end()) return; // never synced
    std::string gid = it->second;
    uint32_t version = ++m_versionByGid[gid];
    enqueueOp("delete", gid, version, "");
    unmapGid(gid);
}

// ---------------------------------------------------------------------------
// Applying remote edits (Last-Write-Wins per gid)
// ---------------------------------------------------------------------------

void CollabManager::notifyOverlayEdit(ApplyObj const& op, GameObject* object) {
    if (!m_overlay || op.origin <= 0 || op.origin == m_clientId) return;
    std::string name = !op.by.empty() ? op.by : peerName(op.origin);
    CCPoint pos = object ? object->getPosition() : CCPoint{0.f, 0.f};
    if (!object) return;
    m_overlay->onRemoteEdit(op.origin, name, pos, op.kind == "delete");
}

void CollabManager::applyRemoteAdd(ApplyObj const& op) {
    if (op.save.empty() || op.gid.empty()) return;

    // If we already know this gid, route through update so we don't duplicate.
    if (m_gidToObj.count(op.gid)) {
        applyRemoteUpdate(op);
        return;
    }

    auto known = m_versionByGid.find(op.gid);
    if (known != m_versionByGid.end() && op.version < known->second) return; // stale

    if (!m_editor) {
        m_versionByGid[op.gid] = op.version;
        return;
    }

    m_applyingRemote = true;
    CCArray* created = m_editor->createObjectsFromString(gd::string(op.save), true, true);
    GameObject* mapped = nullptr;
    if (created && created->count() > 0) {
        if (auto* obj = typeinfo_cast<GameObject*>(created->objectAtIndex(created->count() - 1))) {
            mapGid(op.gid, obj);
            mapped = obj;
        }
        m_editor->updateObjectColors(created);
    }
    m_applyingRemote = false;

    m_versionByGid[op.gid] = op.version;
    notifyOverlayEdit(op, mapped);
}

void CollabManager::applyRemoteUpdate(ApplyObj const& op) {
    if (op.save.empty() || op.gid.empty()) return;

    auto known = m_versionByGid.find(op.gid);
    if (known != m_versionByGid.end() && op.version < known->second) return; // stale

    if (!m_editor) {
        m_versionByGid[op.gid] = op.version;
        return;
    }

    GameObject* existing = findTrackedObject(op.gid);

    m_applyingRemote = true;
    if (existing && existing->getParent()) {
        // Drop it from the local selection first, otherwise EditorUI keeps a
        // freed pointer and crashes on the next edit (common when peers edit
        // the same object at once).
        if (m_editor->m_editorUI) m_editor->m_editorUI->deselectObject(existing);
        m_editor->removeObject(existing, true);
    }
    if (existing) unmapGid(op.gid);
    CCArray* created = m_editor->createObjectsFromString(gd::string(op.save), true, true);
    GameObject* mapped = nullptr;
    if (created && created->count() > 0) {
        if (auto* obj = typeinfo_cast<GameObject*>(created->objectAtIndex(created->count() - 1))) {
            mapGid(op.gid, obj);
            mapped = obj;
        }
        m_editor->updateObjectColors(created);
    }
    m_applyingRemote = false;

    m_versionByGid[op.gid] = op.version;
    notifyOverlayEdit(op, mapped);
}

void CollabManager::applyRemoteDelete(ApplyObj const& op) {
    if (op.gid.empty()) return;

    auto known = m_versionByGid.find(op.gid);
    if (known != m_versionByGid.end() && op.version < known->second) return; // stale

    GameObject* existing = findTrackedObject(op.gid);

    if (m_editor && existing && existing->getParent()) {
        notifyOverlayEdit(op, existing);
        m_applyingRemote = true;
        if (m_editor->m_editorUI) m_editor->m_editorUI->deselectObject(existing);
        m_editor->removeObject(existing, true);
        m_applyingRemote = false;
    }

    unmapGid(op.gid);
    m_versionByGid[op.gid] = op.version; // remember so stale re-adds are rejected
}

// ---------------------------------------------------------------------------
// Host permissions
// ---------------------------------------------------------------------------

void CollabManager::setHostPermissions(HostPermissions permissions) {
    if (!m_isHost) return;
    m_permissions = permissions;
    m_net.sendJson(matjson::makeObject({
        {"t", "set_perms"},
        {"permissions", permissions.toJson()},
    }));
}

bool CollabManager::clientCanOpenSong() const {
    if (!connected() || m_isHost) return true;
    return m_permissions.allowSong;
}

bool CollabManager::clientCanOpenOptions() const {
    if (!connected() || m_isHost) return true;
    return m_permissions.allowOptions;
}

bool CollabManager::clientCanOpenLevelSettings() const {
    if (!connected() || m_isHost) return true;
    return m_permissions.allowLevelSettings;
}

} // namespace paimon::collab
