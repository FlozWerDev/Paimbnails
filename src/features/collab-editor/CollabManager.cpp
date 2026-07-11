#include "CollabManager.hpp"

#include "CollabOverlay.hpp"
#include "CollabPopups.hpp"
#include "CollabVoice.hpp"


#include "../../utils/AccountVerifier.hpp"

#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/ui/PopupManager.hpp>

#include <algorithm>
#include <cmath>

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

// Selected-object reconcile: period in ticks and objects per pass. Catches
// edits made through popups (colors, groups, z-order) and undo shuffles that
// no direct hook covers, at the cost of a save-string per selected object.
// Selections bigger than the window are covered in rotating slices.
constexpr int kReconcileEveryTicks = 10;
constexpr size_t kReconcileMaxObjects = 500;

// Background sweep: every N ticks, register editor objects that no hook
// caught (as adds) and tracked objects that silently left the scene (as
// deletes). Adds are capped per pass to bound the serialization cost.
constexpr int kSweepEveryTicks = 40; // 2s at kTickInterval
constexpr size_t kSweepMaxAddsPerPass = 1500;

constexpr size_t kChatLogCap = 100;

// Peer selection presence: flush local selection at most this often, drop a
// peer's rects if they go silent this long (they left or deselected long ago).
constexpr float kSelectionFlushInterval = 0.12f;
constexpr float kPeerSelectionMaxAge = 8.f;
// Cap rects per peer so a 2k-object selection doesn't flood the wire; beyond
// this we send a single union AABB instead.
constexpr size_t kMaxSelectionRects = 64;

// World-space AABB for a GameObject (object-layer coordinates).
cocos2d::CCRect objectWorldRect(GameObject* object) {
    if (!object) return {};
    auto pos = object->getPosition();
    auto size = object->getContentSize();
    float sx = std::abs(object->getScaleX());
    float sy = std::abs(object->getScaleY());
    float w = std::max(4.f, size.width * sx);
    float h = std::max(4.f, size.height * sy);
    // Anchor is typically center for GameObjects.
    return cocos2d::CCRect{pos.x - w * 0.5f, pos.y - h * 0.5f, w, h};
}

std::string normalizeBaseUrl(std::string base) {
    geode::utils::string::trimIP(base);
    while (!base.empty() && base.back() == '/') base.pop_back();
    if (!base.starts_with("http://") && !base.starts_with("https://")) {
        base = "https://" + base;
    }
    return base;
}

PeerAppearance localPeerAppearance() {
    PeerAppearance appearance;
    appearance.accountID = AccountVerifier::get().getAccountID();

    auto* gm = GameManager::get();
    if (!gm) return appearance;

    IconType iconType = gm->m_playerIconType;
    int iconID = gm->activeIconForType(iconType);
    if (iconID <= 0) {
        iconType = IconType::Cube;
        iconID = gm->m_playerFrame;
    }

    appearance.iconID = std::max(1, iconID);
    appearance.iconType = static_cast<int>(iconType);
    appearance.color1 = gm->getPlayerColor();
    appearance.color2 = gm->getPlayerColor2();
    appearance.glowColor = gm->m_playerGlowColor;
    appearance.glowEnabled = gm->m_playerGlow;
    appearance.hasIcon = true;
    return appearance;
}

} // namespace

PeerAppearance CollabManager::localAppearance() {
    return localPeerAppearance();
}

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
    m_outbox.clear();
    m_inflight.clear();
    ++m_sendEpoch;
    m_retryTimer = 0.f;
    m_sendFailures = 0;
    m_opTokens = kDefaultOpsPerSecond;
    m_opsPerSec = kDefaultOpsPerSecond;
    m_maxOpsPerRequest = kDefaultOpsPerRequest;
    m_syncTotal = 0;
    m_wireHash.clear();
    m_digestStrikes = 0;
    m_digestCooldown = 0.f;
    m_reconcileCursor = 0;
    m_sweepTicks = 0;
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
    m_peerSelections.clear();
    m_pendingSelectionJson = matjson::Value();
    m_selectionDirty = false;
    m_sinceSelectionFlush = 0.f;
    m_wasPlaytesting = false;

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
    m_net.start(base, m_roomCode, m_username, localPeerAppearance(), mode);
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
    m_outbox.clear();
    m_inflight.clear();
    ++m_sendEpoch;
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
    m_outbox.clear();
    m_inflight.clear();
    ++m_sendEpoch;
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
    // View-only guests can still tinker locally; nothing is shared (alk mode).
    return m_state == ConnState::Connected && m_clientId > 0 && !m_applyingRemote && m_editor && canEditObjects();
}

bool CollabManager::isViewOnly() const {
    return connected() && !m_isHost && m_permissions.viewOnly;
}

bool CollabManager::canEditObjects() const {
    if (!connected() || m_isHost) return true;
    return !m_permissions.viewOnly;
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
    m_wireHash.erase(gid);
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
    // 0) Reliable-sender timers: refill the pacing bucket, count down the
    //    digest cooldown, and retry a failed in-flight chunk in order.
    m_opTokens = std::min(m_opTokens + m_opsPerSec * kTickInterval,
                          std::max(m_opsPerSec, static_cast<float>(m_maxOpsPerRequest)));
    if (m_digestCooldown > 0.f) m_digestCooldown -= kTickInterval;
    if (!m_inflight.empty() && m_retryTimer > 0.f) {
        m_retryTimer -= kTickInterval;
        if (m_retryTimer <= 0.f) {
            if (connected() && m_net.isOpen()) {
                log::info("[Collab] reintentando envio de {} op(s)", m_inflight.size());
                sendInflightChunk();
            } else {
                m_retryTimer = 0.5f; // session recovering; check again shortly
            }
        }
    }

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
            } else if (op.kind == "move") {
                applyRemoteMove(op);
            } else if (op.kind == "update") {
                applyRemoteUpdate(op);
            } else {
                applyRemoteAdd(op);
            }
            ++applied;
        }

        // Age peer selection overlays; drop stale ones so ghost rects don't linger.
        for (auto it = m_peerSelections.begin(); it != m_peerSelections.end();) {
            it->second.age += kTickInterval;
            if (it->second.age > kPeerSelectionMaxAge) {
                if (m_overlay) m_overlay->onPeerSelectionCleared(it->first);
                it = m_peerSelections.erase(it);
            } else {
                ++it;
            }
        }

        // Playtesting moves objects around; serializing them then would sync
        // garbage positions, so reconcile/sweep pause while playing.
        bool playtesting = m_editor->m_playbackMode == PlaybackMode::Playing;

        // View-only sandbox: after playtest, discard local divergences and pull
        // a fresh snapshot (alk: "local changes will get reset").
        if (m_wasPlaytesting && !playtesting && isViewOnly() && connected()) {
            pushChatMessage({0, "", "Modo solo lectura: cambios locales descartados"});
            setStatus("Solo lectura — resincronizando el nivel del host...");
            beginResync();
        }
        m_wasPlaytesting = playtesting;

        // 2) Catch edits with no direct hook (edit popups, color/group changes,
        //    undo shuffles): re-send selected objects whose save changed.
        //    Selections larger than the window are covered in rotating slices
        //    (they used to be skipped entirely, so transforming a freshly
        //    pasted mega-asset never synced).
        if (connected() && !playtesting && ++m_reconcileTicks >= kReconcileEveryTicks) {
            m_reconcileTicks = 0;
            if (m_editor->m_editorUI) {
                auto* selected = m_editor->m_editorUI->getSelectedObjects();
                size_t total = selected ? selected->count() : 0;
                if (total > 0 && total <= kReconcileMaxObjects) {
                    reconcileObjects(selected);
                    m_reconcileCursor = 0;
                } else if (total > kReconcileMaxObjects) {
                    if (m_reconcileCursor >= total) m_reconcileCursor = 0;
                    size_t end = std::min(m_reconcileCursor + kReconcileMaxObjects, total);
                    for (size_t i = m_reconcileCursor; i < end; ++i) {
                        if (auto* o = typeinfo_cast<GameObject*>(selected->objectAtIndex(static_cast<unsigned int>(i)))) {
                            sendUpdatedObject(o);
                        }
                    }
                    m_reconcileCursor = end < total ? end : 0;
                }
            }
        }

        // 2b) Background sweep: registers objects created through unhooked
        //     paths and deletes for tracked objects that left the scene.
        if (connected() && !playtesting && m_snapshotComplete && !m_seeding &&
            m_applyQueue.empty() && ++m_sweepTicks >= kSweepEveryTicks) {
            m_sweepTicks = 0;
            sweepEditor();
        }
    }

    // 3) Voice capture (frames are produced/relayed from here).
    CollabVoice::get().update(kTickInterval);

    // 3b) Peer selection presence (ephemeral, throttled).
    if (connected() && m_selectionDirty) {
        m_sinceSelectionFlush += kTickInterval;
        if (m_sinceSelectionFlush >= kSelectionFlushInterval) {
            flushSelectionIfNeeded();
        }
    }

    // 4) Move pending edits into the outbox: instantly when something
    //    structural (add or delete) is pending, throttled for pure update
    //    streams (drags).
    if (connected() && !m_pendingOps.empty()) {
        m_sinceFlush += kTickInterval;
        if (m_pendingStructural || m_sinceFlush >= kUpdateFlushInterval) {
            flushOutgoing();
        }
    }

    // 5) Keep the outbox draining: acks chain the next chunk, but this
    //    restarts the pump after token starvation or a reconnect.
    if (connected() && m_inflight.empty() && !m_outbox.empty()) {
        pumpOutbox();
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
        // v3 servers advertise their real throughput limits; adopt them with
        // some headroom. Legacy servers get the conservative defaults.
        if (msg.contains("limits")) {
            auto const& lim = msg["limits"];
            auto perSec = lim["maxOpsPerSec"].asInt().unwrapOr(0);
            auto perReq = lim["maxOpsPerRequest"].asInt().unwrapOr(0);
            if (perSec > 0) m_opsPerSec = std::max(50.f, static_cast<float>(perSec) * 0.8f);
            if (perReq > 0) m_maxOpsPerRequest = std::min<size_t>(static_cast<size_t>(perReq), 2048);
            log::info("[Collab] limites del servidor: {} ops/s, {} ops/req", m_opsPerSec, m_maxOpsPerRequest);
        }
        m_snapshotReceived = 0;
        m_snapshotComplete = false;
        m_seeded = false;
        m_seeding = false;
        m_seedIdleTicks = 0;
        m_seedTotalTicks = 0;
        if (m_isHost) {
            setStatus(fmt::format("En sala '{}' como host #{}", m_roomCode, m_clientId));
        } else if (m_permissions.viewOnly) {
            setStatus(fmt::format("En sala '{}' en solo lectura #{}", m_roomCode, m_clientId));
        } else {
            setStatus(fmt::format("En sala '{}' como editor #{}", m_roomCode, m_clientId));
        }
        if (m_recovering) {
            m_recovering = false;
            m_recoverAttempts = 0;
            pushChatMessage({0, "", "Conexion restablecida"});
        } else {
            pushChatMessage({0, "", fmt::format("Conectado a la sala {}", m_roomCode)});
            if (!m_isHost && m_permissions.viewOnly) {
                pushChatMessage({0, "", "Sala en solo lectura: tus edits no se comparten"});
            }
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
            if (!m_isHost && m_snapshotReceived > 0) {
                setStatus(fmt::format("Nivel recibido: {} objetos", m_snapshotReceived));
            }
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
                if (item.contains("x") && item.contains("y")) {
                    op.x = static_cast<float>(item["x"].asDouble().unwrapOr(0.0));
                    op.y = static_cast<float>(item["y"].asDouble().unwrapOr(0.0));
                    op.hasPos = true;
                }
                if (op.gid.empty() || op.kind.empty()) continue;
                m_applyQueue.push_back(std::move(op));
            }
        }
        return;
    }

    if (t == "select") {
        handlePeerSelection(msg);
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
        // The room's object state was rebuilt (host re-seed / server
        // self-heal): drop every local assumption — including unsent pending
        // ops, which target state that no longer exists — and rebuild from
        // the ops/snapshot that follow.
        resetEditorState();
        wipeEditorObjects();
        return;
    }

    if (t == "digest") {
        handleDigest(msg);
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
        bool wasViewOnly = isViewOnly();
        if (msg.contains("permissions")) m_permissions = HostPermissions::fromJson(msg["permissions"]);
        bool nowViewOnly = isViewOnly();
        if (nowViewOnly != wasViewOnly) {
            if (nowViewOnly) {
                pushChatMessage({0, "", "El host te puso en solo lectura. Tus edits no se comparten."});
                setStatus("Modo solo lectura (view only)");
                auto popup = PopupManager::get().alert(
                    "Collab Editor",
                    "Estas en <cy>solo lectura</c>. Puedes editar en local pero no se comparte. "
                    "Al salir del playtest se descartan tus cambios locales."
                );
                popup.setPriority(true);
                popup.showQueue();
            } else {
                pushChatMessage({0, "", "El host te dio modo editor. Tus edits se comparten."});
                setStatus(fmt::format("En sala '{}' como editor #{}", m_roomCode, m_clientId));
                auto popup = PopupManager::get().alert(
                    "Collab Editor",
                    "Estas en <cg>modo editor</c>. Todos tus cambios se comparten con la sala."
                );
                popup.setPriority(true);
                popup.showQueue();
            }
        }
        return;
    }

    if (t == "kicked") {
        teardownAndNotify("El host te expulso de la sala.");
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
        if (code == "room_full" || code == "bad_room" ||
            code == "room_not_found" || code == "room_exists" ||
            code == "create_failed" || code == "join_failed" ||
            code == "upgrade_required" || code == "server_full") {
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
        info.appearance.accountID = static_cast<int>(item["accountID"].asInt().unwrapOr(0));
        info.appearance.iconID = static_cast<int>(item["iconID"].asInt().unwrapOr(0));
        info.appearance.iconType = static_cast<int>(item["iconType"].asInt().unwrapOr(0));
        info.appearance.color1 = static_cast<int>(item["color1"].asInt().unwrapOr(0));
        info.appearance.color2 = static_cast<int>(item["color2"].asInt().unwrapOr(0));
        info.appearance.glowColor = static_cast<int>(item["glowColor"].asInt().unwrapOr(0));
        info.appearance.glowEnabled = item.contains("glowEnabled") &&
            item["glowEnabled"].asBool().unwrapOr(false);
        info.appearance.hasIcon = info.appearance.iconID > 0;
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
                clearPeerSelection(id);
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
    m_outbox.clear();
    m_inflight.clear();
    ++m_sendEpoch;
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
            auto popup = PopupManager::get().alert("Collab Editor", text);
            popup.setPriority(true);
            popup.showQueue();
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

void CollabManager::enqueueOp(std::string kind, std::string const& gid, uint32_t version, std::string save,
                              float x, float y, bool hasPos) {
    if (m_pendingOps.size() >= kMaxOpsPerFlush) flushOutgoing();

    // update and move coalesce the same way: only the latest state per gid matters.
    if (kind == "update" || kind == "move") {
        auto it = m_pendingIndexByGid.find(gid);
        if (it != m_pendingIndexByGid.end()) {
            auto& op = m_pendingOps[it->second];
            if (op.kind != "delete") {
                op.kind = kind;
                op.version = version;
                op.save = std::move(save);
                op.x = x;
                op.y = y;
                op.hasPos = hasPos;
                return;
            }
        }
        m_pendingIndexByGid[gid] = m_pendingOps.size();
        m_pendingOps.push_back({std::move(kind), gid, version, std::move(save), x, y, hasPos});
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
    for (auto& op : m_pendingOps) m_outbox.push_back(std::move(op));
    m_pendingOps.clear();
    m_pendingIndexByGid.clear();
    m_syncTotal = std::max(m_syncTotal, m_outbox.size() + m_inflight.size());
    pumpOutbox();
}

void CollabManager::pumpOutbox() {
    if (m_state != ConnState::Connected || !m_net.isOpen()) return;
    // One chunk in flight at a time: guarantees the server receives ops in
    // exactly the order they were produced (parallel POSTs can reorder).
    if (!m_inflight.empty() || m_outbox.empty()) return;

    // Token pacing: mass pastes used to blast thousands of ops in one burst,
    // trip the server's per-second limit and lose the whole batch.
    size_t budget = static_cast<size_t>(m_opTokens);
    if (budget == 0) return; // tokens refill each tick

    size_t limit = std::min({budget, m_maxOpsPerRequest, m_outbox.size()});
    size_t taken = 0;
    size_t bytes = 0;
    while (taken < limit) {
        auto const& op = m_outbox[taken];
        size_t opBytes = op.save.size() + op.gid.size() + 48;
        if (taken > 0 && bytes + opBytes > kMaxSaveBytesPerRequest) break;
        bytes += opBytes;
        ++taken;
    }

    m_inflight.assign(std::make_move_iterator(m_outbox.begin()),
                      std::make_move_iterator(m_outbox.begin() + static_cast<long>(taken)));
    m_outbox.erase(m_outbox.begin(), m_outbox.begin() + static_cast<long>(taken));
    m_opTokens -= static_cast<float>(taken);

    if (m_syncTotal > 800) {
        setStatus(fmt::format("Sincronizando objetos... faltan {}", m_outbox.size() + m_inflight.size()));
    }
    sendInflightChunk();
}

void CollabManager::sendInflightChunk() {
    if (m_inflight.empty()) return;
    m_retryTimer = 0.f;

    auto ops = matjson::Value::array();
    for (auto const& op : m_inflight) {
        auto obj = matjson::makeObject({
            {"kind", op.kind},
            {"gid", op.gid},
            {"version", static_cast<int64_t>(op.version)},
            {"save", op.save},
        });
        if (op.hasPos) {
            obj["x"] = static_cast<double>(op.x);
            obj["y"] = static_cast<double>(op.y);
        }
        ops.push(std::move(obj));
    }

    log::info("[Collab] enviando {} op(s) ({} en cola)", m_inflight.size(), m_outbox.size());
    uint64_t epoch = m_sendEpoch;
    m_net.sendOps(ops, [this, epoch](bool ok, int status, int /*accepted*/) {
        // Epoch mismatch = the outbox was reset (disconnect/resync/recovery)
        // while this chunk was in the air; its fate no longer matters.
        if (epoch != m_sendEpoch) return;
        onOpsAck(ok, status);
    });
}

void CollabManager::onOpsAck(bool ok, int status) {
    if (ok) {
        m_inflight.clear();
        m_sendFailures = 0;
        if (m_outbox.empty()) {
            if (m_syncTotal > 800) {
                setStatus(fmt::format("Sincronizacion completa ({} objetos)", m_syncTotal));
            }
            m_syncTotal = 0;
        } else {
            pumpOutbox(); // chain the next chunk right away
        }
        return;
    }

    // Send failed (timeout / rate limit / server hiccup): the chunk stays in
    // m_inflight and is retried in order with backoff — no op is ever lost.
    ++m_sendFailures;
    float delay = 0.5f * static_cast<float>(1 << std::min(m_sendFailures - 1, 4));
    if (status == 429) delay = std::max(delay, 2.f);
    m_retryTimer = std::min(delay, 8.f);
    log::warn("[Collab] envio de ops fallo (HTTP {}), reintento en {:.1f}s ({} op(s) pendientes)",
              status, m_retryTimer, m_inflight.size() + m_outbox.size());
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
            auto popup = PopupManager::get().alert(
                "Collab Editor",
                "No se pudo abrir el editor de la sala. Vuelve a intentarlo."
            );
            popup.setPriority(true);
            popup.showQueue();
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
    m_outbox.clear();
    m_inflight.clear();
    ++m_sendEpoch; // drops the ack of anything still in the air
    m_retryTimer = 0.f;
    m_sendFailures = 0;
    m_syncTotal = 0;
    m_wireHash.clear();
    m_digestStrikes = 0;
    m_applyQueue.clear();
    m_sinceFlush = 0.f;
    m_seeding = false;
    m_seedIdleTicks = 0;
    m_seedTotalTicks = 0;
    m_reconcileTicks = 0;
    m_reconcileCursor = 0;
    m_sweepTicks = 0;
    m_selectionDirty = false;
    m_sinceSelectionFlush = 0.f;
    m_pendingSelectionJson = matjson::Value();
    // Drop peer selection overlays so they don't linger after a resync wipe.
    if (m_overlay) {
        for (auto const& [id, _] : m_peerSelections) {
            m_overlay->onPeerSelectionCleared(id);
        }
    }
    m_peerSelections.clear();
}

void CollabManager::wipeEditorObjects() {
    if (!m_editor) return;
    TrackerGuard guard(m_applyingRemote);
    if (m_editor->m_editorUI) m_editor->m_editorUI->deselectAll();
    m_editor->removeAllObjects();
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
        m_wireHash[gid] = objectSyncHash(gid, 1, save);
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
    m_wireHash[gid] = objectSyncHash(gid, 1, save);
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
        m_wireHash[gid] = objectSyncHash(gid, 1, save);
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
    m_wireHash[gid] = objectSyncHash(gid, version, save);
    enqueueOp("update", gid, version, std::move(save));
}

void CollabManager::sendUpdatedObjects(CCArray* objects) {
    if (!objects) return;
    for (auto* o : CCArrayExt<GameObject*>(objects)) sendUpdatedObject(o);
}

void CollabManager::sendMovedObject(GameObject* object) {
    if (!shouldEmit() || !object) return;
    auto pos = object->getPosition();
    std::string save = saveObject(object);
    if (save.empty()) return;

    auto it = m_uidToGid.find(object->m_uniqueID);
    if (it == m_uidToGid.end()) {
        // Untracked yet: register as a full add (no cheap move path).
        sendCreatedObject(object);
        return;
    }
    std::string gid = it->second;

    auto last = m_lastSentSave.find(gid);
    if (last != m_lastSentSave.end() && last->second == save) return;

    uint32_t version = ++m_versionByGid[gid];
    m_lastSentSave[gid] = save;
    m_wireHash[gid] = objectSyncHash(gid, version, save);
    enqueueOp("move", gid, version, std::move(save), pos.x, pos.y, true);
}

void CollabManager::sendMovedObjects(CCArray* objects) {
    if (!objects) return;
    for (auto* o : CCArrayExt<GameObject*>(objects)) sendMovedObject(o);
}

void CollabManager::reconcileObjects(CCArray* objects) {
    if (!objects) return;
    for (auto* obj : CCArrayExt<CCObject*>(objects)) {
        if (auto* o = typeinfo_cast<GameObject*>(obj)) sendUpdatedObject(o);
    }
}

void CollabManager::sendSelection(CCArray* selected) {
    // Selection presence is allowed even in view-only (shows where viewers look),
    // but not while applying remote state or disconnected.
    if (m_state != ConnState::Connected || m_clientId <= 0 || m_applyingRemote || !m_editor) return;

    auto rects = matjson::Value::array();
    size_t count = selected ? selected->count() : 0;

    if (count == 0) {
        m_pendingSelectionJson = matjson::makeObject({
            {"t", "select"},
            {"rects", rects},
        });
        m_selectionDirty = true;
        return;
    }

    if (count <= kMaxSelectionRects) {
        for (unsigned int i = 0; i < static_cast<unsigned int>(count); ++i) {
            auto* o = typeinfo_cast<GameObject*>(selected->objectAtIndex(i));
            if (!o) continue;
            auto r = objectWorldRect(o);
            rects.push(matjson::makeObject({
                {"x", static_cast<double>(r.origin.x)},
                {"y", static_cast<double>(r.origin.y)},
                {"w", static_cast<double>(r.size.width)},
                {"h", static_cast<double>(r.size.height)},
            }));
        }
    } else {
        // Huge selection: one union AABB instead of thousands of rects.
        float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
        for (unsigned int i = 0; i < static_cast<unsigned int>(count); ++i) {
            auto* o = typeinfo_cast<GameObject*>(selected->objectAtIndex(i));
            if (!o) continue;
            auto r = objectWorldRect(o);
            minX = std::min(minX, r.origin.x);
            minY = std::min(minY, r.origin.y);
            maxX = std::max(maxX, r.origin.x + r.size.width);
            maxY = std::max(maxY, r.origin.y + r.size.height);
        }
        if (minX < maxX && minY < maxY) {
            rects.push(matjson::makeObject({
                {"x", static_cast<double>(minX)},
                {"y", static_cast<double>(minY)},
                {"w", static_cast<double>(maxX - minX)},
                {"h", static_cast<double>(maxY - minY)},
            }));
        }
    }

    m_pendingSelectionJson = matjson::makeObject({
        {"t", "select"},
        {"rects", std::move(rects)},
    });
    m_selectionDirty = true;
}

void CollabManager::flushSelectionIfNeeded() {
    if (!m_selectionDirty || !connected() || !m_net.isOpen()) return;
    m_selectionDirty = false;
    m_sinceSelectionFlush = 0.f;
    if (m_pendingSelectionJson.isObject()) {
        m_net.sendJson(m_pendingSelectionJson);
    }
}

void CollabManager::handlePeerSelection(matjson::Value const& msg) {
    int from = static_cast<int>(msg["from"].asInt().unwrapOr(0));
    if (from <= 0 || from == m_clientId) return;

    PeerSelection sel;
    sel.clientId = from;
    sel.name = msg.contains("name") ? msg["name"].asString().unwrapOr("") : peerName(from);
    sel.age = 0.f;

    if (auto arr = msg["rects"].asArray()) {
        for (auto const& item : arr.unwrap()) {
            if (!item.isObject()) continue;
            float x = static_cast<float>(item["x"].asDouble().unwrapOr(0.0));
            float y = static_cast<float>(item["y"].asDouble().unwrapOr(0.0));
            float w = static_cast<float>(item["w"].asDouble().unwrapOr(0.0));
            float h = static_cast<float>(item["h"].asDouble().unwrapOr(0.0));
            if (w > 0.f && h > 0.f) {
                sel.rects.push_back(cocos2d::CCRect{x, y, w, h});
            }
        }
    }

    if (sel.rects.empty()) {
        clearPeerSelection(from);
        return;
    }

    m_peerSelections[from] = sel;
    if (m_overlay) m_overlay->onPeerSelection(from, sel.name, sel.rects);
}

void CollabManager::clearPeerSelection(int clientId) {
    m_peerSelections.erase(clientId);
    if (m_overlay) m_overlay->onPeerSelectionCleared(clientId);
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

void CollabManager::sweepEditor() {
    if (!m_editor || !m_editor->m_objects || !shouldEmit()) return;

    // Objects created through a path without a hook (smart objects, other
    // mods, exotic editor tools) get registered as adds.
    size_t adds = 0;
    for (auto* o : CCArrayExt<GameObject*>(m_editor->m_objects)) {
        if (!o || m_uidToGid.count(o->m_uniqueID)) continue;
        sendCreatedObject(o);
        if (++adds >= kSweepMaxAddsPerPass) break;
    }

    // Tracked objects that left the scene without the removeObject hook
    // firing (bulk clears, other mods) become deletes. If one is actually
    // alive it re-registers as an add on the next pass, so this self-heals
    // in both directions.
    std::vector<std::string> gone;
    for (auto const& [gid, obj] : m_gidToObj) {
        if (!obj || !obj->getParent()) gone.push_back(gid);
    }
    for (auto const& gid : gone) {
        uint32_t version = ++m_versionByGid[gid];
        enqueueOp("delete", gid, version, "");
        unmapGid(gid);
    }

    if (adds > 0 || !gone.empty()) {
        log::info("[Collab] sweep: +{} adds, -{} deletes", adds, gone.size());
    }
}

void CollabManager::handleDigest(matjson::Value const& msg) {
    // Only comparable when fully quiescent: nothing pending locally, nothing
    // left to apply, nothing in the air. Otherwise both sides legitimately
    // hold different state for a moment.
    if (!m_editor || !connected()) return;
    if (m_seeding || !m_snapshotComplete) return;
    if (!m_applyQueue.empty() || !m_pendingOps.empty() || !m_outbox.empty() || !m_inflight.empty()) return;
    if (m_digestCooldown > 0.f) return;

    int64_t count = msg["count"].asInt().unwrapOr(-1);
    std::string hash = msg["hash"].asString().unwrapOr("");
    if (count < 0 || hash.empty()) return;

    uint64_t agg = 0;
    for (auto const& [gid, h] : m_wireHash) agg ^= h;
    std::string local = fmt::format("{:016x}", agg);

    if (count == static_cast<int64_t>(m_wireHash.size()) && hash == local) {
        m_digestStrikes = 0;
        return;
    }

    // Two mismatches in a row (with quiet checks in between) means real
    // divergence, not in-transit edits: rebuild automatically.
    if (++m_digestStrikes < 2) return;
    m_digestStrikes = 0;
    m_digestCooldown = m_isHost ? 60.f : 20.f;
    log::warn("[Collab] desync detectado (server: {} obj hash={} | local: {} obj hash={}); auto-resync",
              count, hash, m_wireHash.size(), local);
    setStatus("Desync detectado; resincronizando...");
    beginResync();
}

// ---------------------------------------------------------------------------
// Applying remote edits (Last-Write-Wins per gid)
// ---------------------------------------------------------------------------

void CollabManager::notifyOverlayEdit(ApplyObj const& op, GameObject* object) {
    std::string name = !op.by.empty() ? op.by : peerName(op.origin);
    CCPoint pos = object ? object->getPosition() : CCPoint{0.f, 0.f};

    if (!m_overlay || op.origin <= 0 || op.origin == m_clientId) return;
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

    GameObject* mapped = nullptr;
    {
        TrackerGuard guard(m_applyingRemote);
        CCArray* created = m_editor->createObjectsFromString(gd::string(op.save), true, true);
        if (created && created->count() > 0) {
            if (auto* obj = typeinfo_cast<GameObject*>(created->objectAtIndex(created->count() - 1))) {
                mapGid(op.gid, obj);
                mapped = obj;
            }
            m_editor->updateObjectColors(created);
        }
    }

    m_versionByGid[op.gid] = op.version;
    if (mapped) {
        // Local re-serialization can differ textually from the wire save;
        // storing it keeps the reconcile pass from echoing spurious updates.
        m_lastSentSave[op.gid] = saveObject(mapped);
        m_wireHash[op.gid] = objectSyncHash(op.gid, op.version, op.save);
    } else {
        log::warn("[Collab] no se pudo materializar objeto remoto gid={} ({} bytes)", op.gid, op.save.size());
    }
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

    GameObject* mapped = nullptr;
    {
        TrackerGuard guard(m_applyingRemote);
        if (existing && existing->getParent()) {
            // Drop it from the local selection first, otherwise EditorUI keeps a
            // freed pointer and crashes on the next edit (common when peers edit
            // the same object at once).
            if (m_editor->m_editorUI) m_editor->m_editorUI->deselectObject(existing);
            m_editor->removeObject(existing, true);
        }
        if (existing) unmapGid(op.gid);
        CCArray* created = m_editor->createObjectsFromString(gd::string(op.save), true, true);
        if (created && created->count() > 0) {
            if (auto* obj = typeinfo_cast<GameObject*>(created->objectAtIndex(created->count() - 1))) {
                mapGid(op.gid, obj);
                mapped = obj;
            }
            m_editor->updateObjectColors(created);
        }
    }

    m_versionByGid[op.gid] = op.version;
    if (mapped) {
        m_lastSentSave[op.gid] = saveObject(mapped);
        m_wireHash[op.gid] = objectSyncHash(op.gid, op.version, op.save);
    } else {
        log::warn("[Collab] no se pudo materializar update remoto gid={} ({} bytes)", op.gid, op.save.size());
    }
    notifyOverlayEdit(op, mapped);
}

void CollabManager::applyRemoteMove(ApplyObj const& op) {
    if (op.gid.empty()) return;

    auto known = m_versionByGid.find(op.gid);
    if (known != m_versionByGid.end() && op.version < known->second) return; // stale

    if (!m_editor) {
        m_versionByGid[op.gid] = op.version;
        return;
    }

    GameObject* existing = findTrackedObject(op.gid);
    if (!existing || !existing->getParent()) {
        // Object missing locally: fall back to full recreate if we have a save.
        if (!op.save.empty()) {
            ApplyObj asUpdate = op;
            asUpdate.kind = "update";
            asUpdate.hasPos = false;
            applyRemoteUpdate(asUpdate);
        }
        return;
    }

    {
        TrackerGuard guard(m_applyingRemote);
        if (op.hasPos) {
            existing->setPosition({op.x, op.y});
        } else if (!op.save.empty()) {
            // No coordinates on the wire (legacy peer): recreate from save.
            ApplyObj asUpdate = op;
            asUpdate.kind = "update";
            applyRemoteUpdate(asUpdate);
            return;
        }
    }

    m_versionByGid[op.gid] = op.version;
    if (!op.save.empty()) {
        // Prefer the wire save for digest parity; local re-serialize is fine
        // for reconcile no-op detection.
        m_lastSentSave[op.gid] = saveObject(existing);
        m_wireHash[op.gid] = objectSyncHash(op.gid, op.version, op.save);
    } else {
        std::string save = saveObject(existing);
        m_lastSentSave[op.gid] = save;
        m_wireHash[op.gid] = objectSyncHash(op.gid, op.version, save);
    }
    notifyOverlayEdit(op, existing);
}

void CollabManager::applyRemoteDelete(ApplyObj const& op) {
    if (op.gid.empty()) return;

    auto known = m_versionByGid.find(op.gid);
    if (known != m_versionByGid.end() && op.version < known->second) return; // stale

    GameObject* existing = findTrackedObject(op.gid);

    if (m_editor && existing && existing->getParent()) {
        notifyOverlayEdit(op, existing);
        TrackerGuard guard(m_applyingRemote);
        if (m_editor->m_editorUI) m_editor->m_editorUI->deselectObject(existing);
        m_editor->removeObject(existing, true);
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

void CollabManager::kickPeer(int targetClientId) {
    if (!connected() || !m_isHost) return;
    if (targetClientId <= 0 || targetClientId == m_clientId) return;
    m_net.sendJson(matjson::makeObject({
        {"t", "kick"},
        {"target", static_cast<int64_t>(targetClientId)},
    }));
    // Optimistic local notice; peer list refreshes when the server broadcasts.
    auto name = peerName(targetClientId);
    pushChatMessage({0, "", fmt::format("Expulsaste a {}", name.empty() ? fmt::format("#{}", targetClientId) : name)});
}

bool CollabManager::clientCanOpenSong() const {
    if (!connected() || m_isHost) return true;
    if (m_permissions.viewOnly) return false;
    return m_permissions.allowSong;
}

bool CollabManager::clientCanOpenOptions() const {
    if (!connected() || m_isHost) return true;
    if (m_permissions.viewOnly) return false;
    return m_permissions.allowOptions;
}

bool CollabManager::clientCanOpenLevelSettings() const {
    if (!connected() || m_isHost) return true;
    if (m_permissions.viewOnly) return false;
    return m_permissions.allowLevelSettings;
}

} // namespace paimon::collab
