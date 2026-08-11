#include "CollabManager.hpp"

#include "CollabOverlay.hpp"
#include "CollabPopups.hpp"
#include "CollabVoice.hpp"

#include "../cursor/services/CursorManager.hpp"
#include "../../utils/AccountVerifier.hpp"

#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/LevelSettingsObject.hpp>
#include <Geode/binding/GJEffectManager.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/ui/PopupManager.hpp>

#include "../editor-suite/EditorHelpers.hpp"
#include "../editor-suite/EditorModule.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

using namespace geode::prelude;

namespace paimon::collab {

namespace {

constexpr size_t kRemoteApplyMaxPerTick = 1000;
constexpr auto kRemoteApplyBudget = std::chrono::milliseconds(4);
constexpr size_t kSeedMaxPerTick = 512;
constexpr auto kSeedBudget = std::chrono::milliseconds(4);
constexpr size_t kSeedChunkMaxObjects = 800;
constexpr size_t kSeedChunkMaxBytes = 1'200'000;
constexpr auto kLocalEditBudget = std::chrono::milliseconds(3);

constexpr float kTickInterval = 0.05f;

constexpr float kUpdateFlushInterval = 0.2f;

constexpr int kReconcileEveryTicks = 10;
constexpr size_t kReconcileMaxObjects = 500;

constexpr int kSweepEveryTicks = 40;
constexpr size_t kSweepMaxAddsPerPass = 1500;
constexpr size_t kSweepMaxChecksPerPass = 2000;
constexpr auto kSweepBudget = std::chrono::milliseconds(2);

constexpr size_t kChatLogCap = 100;

constexpr float kSelectionFlushInterval = 0.12f;
constexpr float kPeerSelectionMaxAge = 8.f;
constexpr size_t kMaxSelectionRects = 64;

constexpr float kCameraFlushInterval = 0.2f;
constexpr float kPeerCameraMaxAge = 6.f;
constexpr float kCameraMoveEpsilon = 6.f;
constexpr float kCameraZoomEpsilon = 0.02f;
constexpr float kCursorMoveEpsilon = 3.f;

constexpr float kWorkZoneFlushInterval = 0.75f;
constexpr float kPeerWorkZoneMaxAge = 8.f;
constexpr float kWorkZoneMoveEpsilon = 18.f;

constexpr float kHeatCellSize = 90.f;
constexpr float kHeatDecayPerSec = 0.7f;
constexpr float kHeatGain = 1.15f;
constexpr float kMaxCellHeat = 3.f;
constexpr size_t kMaxHeatCells = 400;

constexpr int kMetaReconcileEveryTicks = 40;

bool isCheapEditKind(std::string const& kind) {
    return kind == "move" || kind == "rotate" || kind == "scale" || kind == "flip";
}

uint64_t remoteOrderKey(uint32_t version, int origin) {
    return (static_cast<uint64_t>(version) << 32) |
           static_cast<uint32_t>(std::max(origin, 0));
}

LocalEditKind mergeEditKind(LocalEditKind a, LocalEditKind b) {
    if (a == LocalEditKind::Full || b == LocalEditKind::Full) return LocalEditKind::Full;
    if (a == b) return a;
    return LocalEditKind::Full;
}

char const* kindName(LocalEditKind kind) {
    switch (kind) {
        case LocalEditKind::Move: return "move";
        case LocalEditKind::Rotate: return "rotate";
        case LocalEditKind::Scale: return "scale";
        case LocalEditKind::Flip: return "flip";
        default: return "update";
    }
}

cocos2d::CCRect objectWorldRect(GameObject* object) {
    if (!object) return {};
    auto pos = object->getPosition();
    auto size = object->getContentSize();
    float sx = std::abs(object->getScaleX());
    float sy = std::abs(object->getScaleY());
    float w = std::max(4.f, size.width * sx);
    float h = std::max(4.f, size.height * sy);
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

std::string encodeBase64(std::vector<uint8_t> const& data) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    for (size_t i = 0; i < data.size(); i += 3) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < data.size()) n |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < data.size()) n |= static_cast<uint32_t>(data[i + 2]);
        out.push_back(kAlphabet[(n >> 18) & 0x3f]);
        out.push_back(kAlphabet[(n >> 12) & 0x3f]);
        out.push_back(i + 1 < data.size() ? kAlphabet[(n >> 6) & 0x3f] : '=');
        out.push_back(i + 2 < data.size() ? kAlphabet[n & 0x3f] : '=');
    }
    return out;
}

void addLocalCursorAppearance(PeerAppearance& appearance) {
    if (!paimon::editor::featureEnabled("collab-custom-cursors")) return;

    auto& cursor = CursorManager::get();
    auto const& config = cursor.config();
    if (!config.enabled || config.idleImage.empty()) return;

    auto path = cursor.galleryDir() / config.idleImage;
    std::error_code ec;
    auto fileSize = std::filesystem::file_size(path, ec);
    if (ec || fileSize == 0 || fileSize > kMaxCursorAssetBytes) return;

    auto data = file::readBinary(path);
    if (!data || data.unwrap().empty() || data.unwrap().size() > kMaxCursorAssetBytes) return;

    appearance.cursorData = encodeBase64(data.unwrap());
    appearance.cursorScale = std::clamp(config.scale, CURSOR_SCALE_MIN, CURSOR_SCALE_MAX);
    appearance.cursorOpacity = std::clamp(config.opacity, 0, 255);
    appearance.hasCustomCursor = true;
}

PeerAppearance localPeerAppearance() {
    PeerAppearance appearance;
    appearance.accountID = AccountVerifier::get().getAccountID();

    auto* gm = GameManager::get();
    if (gm) {
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
    }

    addLocalCursorAppearance(appearance);
    return appearance;
}

}

PeerAppearance CollabManager::localAppearance() {
    return localPeerAppearance();
}

CollabManager& CollabManager::get() {
    static CollabManager instance;
    return instance;
}

CollabManager::CollabManager() {
    listenForSettingChanges<bool>("collab-enabled", +[](bool enabled) {
        if (!enabled) CollabManager::get().disconnect();
    });
}

CollabManager::~CollabManager() {
    disconnect();
}

void CollabManager::connect(std::string const& roomCode, std::string const& username, ConnectMode mode, GJGameLevel* hostLevel) {
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
    m_deferredCreates.clear();
    m_deferredEditOrder.clear();
    m_deferredEdits.clear();
    m_pendingOps.clear();
    m_pendingIndexByGid.clear();
    m_sinceFlush = 0.f;
    m_pendingStructural = false;
    m_outbox.clear();
    m_outboxByGid.clear();
    m_inflight.clear();
    ++m_sendEpoch;
    m_retryTimer = 0.f;
    m_sendFailures = 0;
    m_opTokens = kDefaultOpsPerSecond;
    m_opsPerSec = kDefaultOpsPerSecond;
    m_maxOpsPerRequest = kDefaultOpsPerRequest;
    m_syncTotal = 0;
    m_wireHash.clear();
    m_wireDigest = 0;
    m_digestStrikes = 0;
    m_digestCooldown = 0.f;
    m_reconcileCursor = 0;
    m_sweepTicks = 0;
    m_sweepObjectCursor = 0;
    m_sweepBucketCursor = 0;
    m_applyQueue.clear();
    m_queuedRemoteByGid.clear();
    m_snapshotReceived = 0;
    m_snapshotComplete = false;
    m_seeded = false;
    m_seeding = false;
    m_seedCursor = 0;
    m_seedTotal = 0;
    m_seedUploaded = 0;
    m_seedChunk = matjson::Value::array();
    m_seedChunkBytes = 0;
    m_seedInflight = false;
    m_seedSerializeDone = false;
    ++m_seedEpoch;
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
    m_peerCameras.clear();
    m_pendingCameraJson = matjson::Value();
    m_cameraDirty = false;
    m_sinceCameraFlush = 0.f;
    m_lastCamX = 0.f;
    m_lastCamY = 0.f;
    m_lastCamZoom = 0.f;
    m_lastCursorX = 0.f;
    m_lastCursorY = 0.f;
    m_lastCursorVisible = false;
    m_peerWorkZones.clear();
    m_pendingWorkZoneJson = matjson::Value();
    m_workZoneDirty = false;
    m_sinceWorkZoneFlush = 0.f;
    m_lastZoneX = m_lastZoneY = m_lastZoneW = m_lastZoneH = 0.f;
    m_peerPings.clear();
    m_followClientId = 0;
    m_heatCells.clear();
    m_layerOwners.clear();
    m_lastMetaSig.clear();
    m_metaReconcileTicks = 0;
    m_pendingLevelMeta = matjson::Value();
    m_hasPendingLevelMeta = false;
    m_wasPlaytesting = false;

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
    m_deferredCreates.clear();
    m_deferredEditOrder.clear();
    m_deferredEdits.clear();
    m_outbox.clear();
    m_outboxByGid.clear();
    m_inflight.clear();
    ++m_sendEpoch;
    m_applyQueue.clear();
    m_queuedRemoteByGid.clear();
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
    m_deferredCreates.clear();
    m_deferredEditOrder.clear();
    m_deferredEdits.clear();
    m_outbox.clear();
    m_outboxByGid.clear();
    m_inflight.clear();
    ++m_sendEpoch;
    m_applyQueue.clear();
    m_queuedRemoteByGid.clear();
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
    if (editor && connected() && m_needsResyncOnEntry) {
        m_needsResyncOnEntry = false;
        if (m_isHost && editor->m_level) m_hostLevel = editor->m_level;
        beginResync();
    }
    if (editor && m_hasPendingLevelMeta) {
        m_hasPendingLevelMeta = false;
        handleLevelSettings(m_pendingLevelMeta);
        m_pendingLevelMeta = matjson::Value();
    }
    if (editor && connected() && m_isHost && m_snapshotComplete && !m_seeding) {
        sendLevelSettings(true);
    }
}

void CollabManager::clearEditor(LevelEditorLayer* editor) {
    if (m_editor != editor) return;
    m_editor = nullptr;
    m_overlay = nullptr;
    CollabVoice::get().stopAll();

    // Hosts keep the room alive outside the editor; joiners disconnect.
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
    if (m_overlay == overlay) m_overlay = nullptr;
}

bool CollabManager::shouldEmit() const {
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
    eraseWireHash(gid);
}

void CollabManager::setWireHash(std::string const& gid, uint64_t hash) {
    auto [it, inserted] = m_wireHash.try_emplace(gid, hash);
    if (!inserted) {
        m_wireDigest ^= it->second;
        it->second = hash;
    }
    m_wireDigest ^= hash;
}

void CollabManager::eraseWireHash(std::string const& gid) {
    auto it = m_wireHash.find(gid);
    if (it == m_wireHash.end()) return;
    m_wireDigest ^= it->second;
    m_wireHash.erase(it);
}

std::string CollabManager::saveObject(GameObject* object) const {
    if (!m_editor || !object) return {};
    return std::string(object->getSaveString(m_editor));
}

GameObject* CollabManager::findTrackedObject(std::string const& gid) const {
    auto it = m_gidToObj.find(gid);
    return it != m_gidToObj.end() ? it->second.data() : nullptr;
}

}

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
    handleMessage(msg);
}

void CollabManager::tick() {
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
                m_retryTimer = 0.5f;
            }
        }
    }

    // Apply remote ops only with an editor and within the frame budget.
    if (m_editor) {
        if (m_seeding) {
            seedFromEditor();
        }
        drainDeferredLocalEdits();

        auto applyDeadline = std::chrono::steady_clock::now() + kRemoteApplyBudget;
        size_t applied = 0;
        while (!m_applyQueue.empty() && applied < kRemoteApplyMaxPerTick &&
               (applied == 0 || std::chrono::steady_clock::now() < applyDeadline)) {
            auto queued = m_applyQueue.begin();
            auto op = std::move(*queued);
            m_queuedRemoteByGid.erase(op.gid);
            m_applyQueue.erase(queued);
            ++applied;
            if (op.kind == "delete") {
                applyRemoteDelete(op);
            } else if (op.kind == "move") {
                applyRemoteMove(op);
            } else if (op.kind == "rotate" || op.kind == "scale" || op.kind == "flip") {
                applyRemoteTransform(op);
            } else if (op.kind == "update") {
                applyRemoteUpdate(op);
            } else {
                applyRemoteAdd(op);
            }
        }

        for (auto it = m_peerSelections.begin(); it != m_peerSelections.end();) {
            it->second.age += kTickInterval;
            if (it->second.age > kPeerSelectionMaxAge) {
                if (m_overlay) m_overlay->onPeerSelectionCleared(it->first);
                it = m_peerSelections.erase(it);
            } else {
                ++it;
            }
        }

        for (auto it = m_peerCameras.begin(); it != m_peerCameras.end();) {
            it->second.age += kTickInterval;
            if (it->second.age > kPeerCameraMaxAge) {
                if (m_overlay) m_overlay->onPeerCameraCleared(it->first);
                it = m_peerCameras.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = m_peerWorkZones.begin(); it != m_peerWorkZones.end();) {
            it->second.age += kTickInterval;
            if (it->second.age > kPeerWorkZoneMaxAge) {
                if (m_overlay) m_overlay->onPeerWorkZoneCleared(it->first);
                it = m_peerWorkZones.erase(it);
            } else {
                ++it;
            }
        }
        tickPings(kTickInterval);
        tickHeatmap(kTickInterval);
        tickFollow();

        bool playtesting = m_editor->m_playbackMode == PlaybackMode::Playing;

        if (connected() && !playtesting) {
            sendCameraPresence();
            sendWorkZone();
        }

        if (m_wasPlaytesting && !playtesting && isViewOnly() && connected()) {
            pushChatMessage({0, "", "Modo solo lectura: cambios locales descartados"});
            setStatus("Solo lectura - resincronizando el nivel del host...");
            beginResync();
        }
        m_wasPlaytesting = playtesting;

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

        if (connected() && !playtesting && m_snapshotComplete && !m_seeding &&
            m_applyQueue.empty() && ++m_sweepTicks >= kSweepEveryTicks) {
            m_sweepTicks = 0;
            sweepEditor();
        }

        if (connected() && !playtesting && m_snapshotComplete && !m_seeding &&
            ++m_metaReconcileTicks >= kMetaReconcileEveryTicks) {
            m_metaReconcileTicks = 0;
            reconcileLevelMeta();
        }
    }

    CollabVoice::get().update(kTickInterval);

    if (connected() && m_selectionDirty) {
        m_sinceSelectionFlush += kTickInterval;
        if (m_sinceSelectionFlush >= kSelectionFlushInterval) {
            flushSelectionIfNeeded();
        }
    }
    if (connected() && m_cameraDirty) {
        m_sinceCameraFlush += kTickInterval;
        if (m_sinceCameraFlush >= kCameraFlushInterval) {
            flushCameraIfNeeded();
        }
    }
    if (connected() && m_workZoneDirty) {
        m_sinceWorkZoneFlush += kTickInterval;
        if (m_sinceWorkZoneFlush >= kWorkZoneFlushInterval) {
            flushWorkZoneIfNeeded();
        }
    }

    if (connected() && !m_pendingOps.empty()) {
        m_sinceFlush += kTickInterval;
        if (m_pendingStructural || m_sinceFlush >= kUpdateFlushInterval) {
            flushOutgoing();
        }
    }

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
        if (msg.contains("layerOwners")) handleLayerOwners(msg["layerOwners"]);
        if (msg.contains("levelMeta") && msg["levelMeta"].isObject()) {
            handleLevelSettings(msg["levelMeta"]);
        }
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
        m_seedCursor = 0;
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
            if (!m_isHost && m_permissions.strictLayers) {
                pushChatMessage({0, "", "Layers exclusivas: cada editor reclama la layer al editar"});
            }
        }
        m_recoverAttempts = 0;

        if (m_isHost) {
            m_seeded = true;
            m_seeding = true;
            m_seedCursor = 0;
            m_seedTotal = 0;
            m_seedUploaded = 0;
            m_seedChunk = matjson::Value::array();
            m_seedChunkBytes = 0;
            m_seedInflight = false;
            m_seedSerializeDone = false;
            ++m_seedEpoch;
            m_snapshotComplete = true;
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
                    queueRemote(std::move(op));
                    ++m_snapshotReceived;
                }
            }
        }
        if (idx >= cnt - 1) {
            m_snapshotComplete = true;
            if (!m_isHost && m_snapshotReceived > 0) {
                setStatus(fmt::format("Nivel recibido: {} objetos", m_snapshotReceived));
            }
            if (m_isHost && m_snapshotReceived == 0 && !m_seeded) {
                m_seeded = true;
                m_seeding = true;
                m_seedCursor = 0;
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
                fillApplyTransform(op, item);
                if (op.gid.empty() || op.kind.empty()) continue;
                queueRemote(std::move(op));
            }
        }
        return;
    }

    if (t == "select") {
        handlePeerSelection(msg);
        return;
    }

    if (t == "camera") {
        handlePeerCamera(msg);
        return;
    }

    if (t == "workzone") {
        handlePeerWorkZone(msg);
        return;
    }

    if (t == "ping") {
        handlePeerPing(msg);
        return;
    }

    if (t == "layer_owners") {
        handleLayerOwners(msg);
        return;
    }

    if (t == "level_settings") {
        handleLevelSettings(msg);
        return;
    }

    if (t == "resync_ready") {
        if (m_isHost) {
            m_seeded = true;
            m_seeding = true;
            m_snapshotComplete = true;
            m_seedCursor = 0;
            m_seedTotal = 0;
            m_seedUploaded = 0;
            m_seedChunk = matjson::Value::array();
            m_seedChunkBytes = 0;
            m_seedInflight = false;
            m_seedSerializeDone = false;
            ++m_seedEpoch;
        }
        return;
    }

    if (t == "resync") {
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
        if (!m_permissions.strictLayers) m_layerOwners.clear();
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

        // Recover before tearing down; hosts reseed and peers rebuild.
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

}

void CollabManager::queueRemote(ApplyObj op) {
    if (op.gid.empty()) return;
    auto order = remoteOrderKey(op.version, op.origin);
    auto queued = m_queuedRemoteByGid.find(op.gid);
    if (queued != m_queuedRemoteByGid.end()) {
        auto& current = *queued->second;
        if (order <= remoteOrderKey(current.version, current.origin)) return;
        current = std::move(op);
        m_applyQueue.splice(m_applyQueue.end(), m_applyQueue, queued->second);
        return;
    }
    m_applyQueue.push_back(std::move(op));
    m_queuedRemoteByGid.emplace(m_applyQueue.back().gid, std::prev(m_applyQueue.end()));
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
        auto cursorData = item.contains("cursorData")
            ? item["cursorData"].asString().unwrapOr("") : std::string();
        if (cursorData.size() <= kMaxCursorDataLength) {
            info.appearance.cursorData = std::move(cursorData);
        }
        info.appearance.cursorScale = std::clamp(
            static_cast<float>(item["cursorScale"].asDouble().unwrapOr(0.3)),
            CURSOR_SCALE_MIN, CURSOR_SCALE_MAX);
        info.appearance.cursorOpacity = std::clamp(
            static_cast<int>(item["cursorOpacity"].asInt().unwrapOr(255)), 0, 255);
        info.appearance.hasCustomCursor = !info.appearance.cursorData.empty();
        if (info.clientId > 0) next[info.clientId] = info;
    }

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
                clearPeerCamera(id);
                clearPeerWorkZone(id);
                if (m_followClientId == id) clearFollow();
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
    // Hosts can recover outside the editor; peers need one to rebuild.
    if (!m_isHost && !m_editor) return false;

    auto nowTp = std::chrono::steady_clock::now();
    if (m_lastRecoverAt.time_since_epoch().count() != 0 &&
        nowTp - m_lastRecoverAt > std::chrono::minutes(2)) {
        m_recoverAttempts = 0;
    }
    if (m_recoverAttempts >= 3) return false;
    ++m_recoverAttempts;
    m_lastRecoverAt = nowTp;

    bool asHost = m_isHost;
    m_recovering = true;

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

    m_state = ConnState::Disconnected;
    m_clientId = 0;
    m_isHost = false;
    m_peers.clear();
    m_pendingOps.clear();
    m_pendingIndexByGid.clear();
    m_pendingStructural = false;
    m_deferredCreates.clear();
    m_deferredEditOrder.clear();
    m_deferredEdits.clear();
    m_outbox.clear();
    m_outboxByGid.clear();
    m_inflight.clear();
    ++m_sendEpoch;
    m_applyQueue.clear();
    m_queuedRemoteByGid.clear();
    CollabVoice::get().stopAll();
    m_net.stop();

    queueInMainThread([text, wasJoiner]() {
        if (wasJoiner && LevelEditorLayer::get()) {
            CCDirector::sharedDirector()->popScene();
        }
        queueInMainThread([text, wasJoiner]() {
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
    queueInMainThread([level]() {
        queueInMainThread([level]() {
            if (LevelEditorLayer::get()) return;
            if (auto* glm = GameLevelManager::get()) glm->deleteLevel(level);
        });
    });
}

}

namespace paimon::collab {

bool CollabManager::isCheapKind(std::string const& kind) {
    return isCheapEditKind(kind);
}

void CollabManager::enqueueOp(std::string kind, std::string const& gid, uint32_t version, std::string save,
                              float x, float y, bool hasPos) {
    OutOp op;
    op.kind = std::move(kind);
    op.gid = gid;
    op.version = version;
    op.save = std::move(save);
    op.x = x;
    op.y = y;
    op.hasPos = hasPos;
    enqueueOp(std::move(op));
}

void CollabManager::enqueueOp(OutOp op) {
    if (m_pendingOps.size() >= kMaxOpsPerFlush) flushOutgoing();

    auto pending = m_pendingIndexByGid.find(op.gid);
    if (pending != m_pendingIndexByGid.end()) {
        auto& existing = m_pendingOps[pending->second];
        if (existing.kind == "add" && op.kind == "delete") {
            existing.kind.clear();
            m_pendingIndexByGid.erase(pending);
            return;
        }
        if ((existing.kind == "add" || existing.kind == "delete") &&
            (op.kind == "update" || isCheapEditKind(op.kind))) {
            op.kind = "add";
        }
        if (op.kind == "add" || op.kind == "delete") m_pendingStructural = true;
        existing = std::move(op);
        return;
    }

    if (op.kind == "add" || op.kind == "delete") m_pendingStructural = true;
    m_pendingIndexByGid[op.gid] = m_pendingOps.size();
    m_pendingOps.push_back(std::move(op));
}

void CollabManager::writeOutTransform(matjson::Value& obj, OutOp const& op) {
    if (op.hasPos) {
        obj["x"] = static_cast<double>(op.x);
        obj["y"] = static_cast<double>(op.y);
    }
    if (op.hasRot) obj["rot"] = static_cast<double>(op.rot);
    if (op.hasScale) {
        obj["sx"] = static_cast<double>(op.scaleX);
        obj["sy"] = static_cast<double>(op.scaleY);
    }
    if (op.hasFlip) {
        obj["fx"] = op.flipX;
        obj["fy"] = op.flipY;
    }
}

void CollabManager::fillApplyTransform(ApplyObj& op, matjson::Value const& item) {
    if (item.contains("x") && item.contains("y")) {
        op.x = static_cast<float>(item["x"].asDouble().unwrapOr(0.0));
        op.y = static_cast<float>(item["y"].asDouble().unwrapOr(0.0));
        op.hasPos = true;
    }
    if (item.contains("rot")) {
        op.rot = static_cast<float>(item["rot"].asDouble().unwrapOr(0.0));
        op.hasRot = true;
    }
    if (item.contains("sx") && item.contains("sy")) {
        op.scaleX = static_cast<float>(item["sx"].asDouble().unwrapOr(1.0));
        op.scaleY = static_cast<float>(item["sy"].asDouble().unwrapOr(1.0));
        op.hasScale = true;
    }
    if (item.contains("fx") || item.contains("fy")) {
        op.flipX = item.contains("fx") && item["fx"].asBool().unwrapOr(false);
        op.flipY = item.contains("fy") && item["fy"].asBool().unwrapOr(false);
        op.hasFlip = true;
    }
}

void CollabManager::flushOutgoing() {
    m_sinceFlush = 0.f;
    m_pendingStructural = false;
    if (m_pendingOps.empty()) return;
    for (auto& op : m_pendingOps) {
        if (op.kind.empty()) continue;

        auto queued = m_outboxByGid.find(op.gid);
        if (queued == m_outboxByGid.end()) {
            m_outbox.push_back(std::move(op));
            m_outboxByGid.emplace(m_outbox.back().gid, std::prev(m_outbox.end()));
            continue;
        }

        auto& current = *queued->second;
        if (current.kind == "add" && op.kind == "delete") {
            m_outbox.erase(queued->second);
            m_outboxByGid.erase(queued);
            continue;
        }
        if (current.kind == "add" && (op.kind == "update" || isCheapEditKind(op.kind))) {
            op.kind = "add";
        }
        current = std::move(op);
        m_outbox.splice(m_outbox.end(), m_outbox, queued->second);
    }
    m_pendingOps.clear();
    m_pendingIndexByGid.clear();
    m_syncTotal = std::max(m_syncTotal, m_outbox.size() + m_inflight.size());
    pumpOutbox();
}

void CollabManager::pumpOutbox() {
    if (m_state != ConnState::Connected || !m_net.isOpen()) return;
    // One in-flight chunk preserves operation order.
    if (!m_inflight.empty() || m_outbox.empty()) return;

    size_t budget = static_cast<size_t>(m_opTokens);
    if (budget == 0) return;

    size_t limit = std::min({budget, m_maxOpsPerRequest, m_outbox.size()});
    size_t taken = 0;
    size_t bytes = 0;
    for (auto it = m_outbox.begin(); it != m_outbox.end() && taken < limit; ++it) {
        auto const& op = *it;
        size_t opBytes = op.save.size() + op.gid.size() + 48;
        if (taken > 0 && bytes + opBytes > kMaxSaveBytesPerRequest) break;
        bytes += opBytes;
        ++taken;
    }

    m_inflight.clear();
    m_inflight.reserve(taken);
    for (size_t i = 0; i < taken; ++i) {
        auto queued = m_outbox.begin();
        m_outboxByGid.erase(queued->gid);
        m_inflight.push_back(std::move(*queued));
        m_outbox.erase(queued);
    }
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
        writeOutTransform(obj, op);
        ops.push(std::move(obj));
    }

    log::info("[Collab] enviando {} op(s) ({} en cola)", m_inflight.size(), m_outbox.size());
    uint64_t epoch = m_sendEpoch;
    m_net.sendOps(ops, [this, epoch](bool ok, int status, int /*accepted*/) {
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
            pumpOutbox();
        }
        return;
    }

    // Keep the chunk in flight while retrying with backoff.
    ++m_sendFailures;
    float delay = 0.5f * static_cast<float>(1 << std::min(m_sendFailures - 1, 4));
    if (status == 429) delay = std::max(delay, 2.f);
    m_retryTimer = std::min(delay, 8.f);
    log::warn("[Collab] envio de ops fallo (HTTP {}), reintento en {:.1f}s ({} op(s) pendientes)",
              status, m_retryTimer, m_inflight.size() + m_outbox.size());
}

void CollabManager::openJoinerEditor() {
    if (m_isHost || m_joinerEditorOpened) {
        log::info("[Collab] openJoinerEditor skipped (isHost={} alreadyOpened={})", m_isHost, m_joinerEditorOpened);
        return;
    }
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
        closeSessionPopups();
        CCDirector::get()->pushScene(CCTransitionFade::create(0.5f, scene));
    });
}

void CollabManager::openHostEditor() {
    if (!m_isHost || LevelEditorLayer::get()) return;
    if (!m_hostLevel) {
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
        closeSessionPopups();
        CCDirector::get()->pushScene(CCTransitionFade::create(0.5f, scene));
    });
}

void CollabManager::resetEditorState() {
    m_uidToGid.clear();
    m_gidToObj.clear();
    m_versionByGid.clear();
    m_lastSentSave.clear();
    m_deferredCreates.clear();
    m_deferredEditOrder.clear();
    m_deferredEdits.clear();
    m_pendingOps.clear();
    m_pendingIndexByGid.clear();
    m_pendingStructural = false;
    m_outbox.clear();
    m_outboxByGid.clear();
    m_inflight.clear();
    ++m_sendEpoch;
    m_retryTimer = 0.f;
    m_sendFailures = 0;
    m_syncTotal = 0;
    m_wireHash.clear();
    m_wireDigest = 0;
    m_digestStrikes = 0;
    m_applyQueue.clear();
    m_queuedRemoteByGid.clear();
    m_sinceFlush = 0.f;
    m_seeding = false;
    m_seedCursor = 0;
    m_seedTotal = 0;
    m_seedUploaded = 0;
    m_seedChunk = matjson::Value::array();
    m_seedChunkBytes = 0;
    m_seedInflight = false;
    m_seedSerializeDone = false;
    ++m_seedEpoch;
    m_reconcileTicks = 0;
    m_reconcileCursor = 0;
    m_sweepTicks = 0;
    m_sweepObjectCursor = 0;
    m_sweepBucketCursor = 0;
    m_selectionDirty = false;
    m_sinceSelectionFlush = 0.f;
    m_pendingSelectionJson = matjson::Value();
    m_cameraDirty = false;
    m_sinceCameraFlush = 0.f;
    m_pendingCameraJson = matjson::Value();
    m_lastCamX = 0.f;
    m_lastCamY = 0.f;
    m_lastCamZoom = 0.f;
    m_lastCursorX = 0.f;
    m_lastCursorY = 0.f;
    m_lastCursorVisible = false;
    if (m_overlay) {
        for (auto const& [id, _] : m_peerSelections) {
            m_overlay->onPeerSelectionCleared(id);
        }
        for (auto const& [id, _] : m_peerCameras) {
            m_overlay->onPeerCameraCleared(id);
        }
        for (auto const& [id, _] : m_peerWorkZones) {
            m_overlay->onPeerWorkZoneCleared(id);
        }
    }
    m_peerSelections.clear();
    m_peerCameras.clear();
    m_peerWorkZones.clear();
    m_peerPings.clear();
    m_followClientId = 0;
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
        m_seeded = true;
        m_seeding = false;
    } else {
        wipeEditorObjects();
        m_seeded = false;
    }
    m_net.requestResync();
}

size_t CollabManager::seedFromEditor() {
    if (m_seedInflight) return 0;

    if (!m_editor || !m_editor->m_objects) {
        if (!m_seedSerializeDone) {
            m_seedSerializeDone = true;
            flushSeedChunk(true);
        } else {
            m_seeding = false;
        }
        return 0;
    }

    auto* objects = m_editor->m_objects;
    size_t total = objects->count();
    if (m_seedTotal == 0) m_seedTotal = total;

    if (m_seedSerializeDone) {
        if (m_seedChunk.isArray() && m_seedChunk.size() > 0) {
            flushSeedChunk(true);
        } else if (!m_seedInflight) {
            m_seeding = false;
        }
        return 0;
    }

    auto deadline = std::chrono::steady_clock::now() + kSeedBudget;
    size_t added = 0;
    size_t processed = 0;
    if (!m_seedChunk.isArray()) m_seedChunk = matjson::Value::array();

    while (m_seedCursor < total && processed < kSeedMaxPerTick &&
           (processed == 0 || std::chrono::steady_clock::now() < deadline)) {
        auto* o = typeinfo_cast<GameObject*>(
            objects->objectAtIndex(static_cast<unsigned int>(m_seedCursor++))
        );
        ++processed;
        if (!o) continue;
        if (m_uidToGid.count(o->m_uniqueID)) continue;
        std::string save = saveObject(o);
        if (save.empty()) continue;
        std::string gid = makeLocalGid();
        mapGid(gid, o);
        m_versionByGid[gid] = 1;
        m_lastSentSave[gid] = save;
        setWireHash(gid, objectSyncHash(gid, 1, save));

        m_seedChunkBytes += save.size() + gid.size() + 48;
        m_seedChunk.push(matjson::makeObject({
            {"gid", gid},
            {"save", std::move(save)},
            {"version", static_cast<int64_t>(1)},
        }));
        ++added;

        if (m_seedChunk.size() >= kSeedChunkMaxObjects || m_seedChunkBytes >= kSeedChunkMaxBytes) {
            break;
        }
    }

    bool serializeDone = m_seedCursor >= total;
    if (serializeDone) m_seedSerializeDone = true;

    size_t remaining = total > m_seedCursor ? total - m_seedCursor : 0;
    if (added > 0 || serializeDone) {
        setStatus(fmt::format(
            "Subiendo nivel a la sala... {}/{} (faltan {})",
            m_seedUploaded + m_seedChunk.size(),
            m_seedTotal,
            remaining
        ));
    }

    bool chunkReady = m_seedChunk.size() >= kSeedChunkMaxObjects ||
                      m_seedChunkBytes >= kSeedChunkMaxBytes ||
                      (serializeDone && (m_seedChunk.size() > 0 || m_seedUploaded == 0));
    if (chunkReady && !m_seedInflight) {
        flushSeedChunk(serializeDone);
    }
    return added;
}

void CollabManager::flushSeedChunk(bool finalChunk) {
    if (m_seedInflight) return;
    if (!m_net.isOpen()) {
        m_seeding = false;
        return;
    }

    if (!m_seedChunk.isArray()) m_seedChunk = matjson::Value::array();
    if (m_seedChunk.size() == 0 && !finalChunk) return;

    m_seedInflight = true;
    uint64_t epoch = m_seedEpoch;
    size_t chunkCount = m_seedChunk.size();
    auto objects = std::move(m_seedChunk);
    m_seedChunk = matjson::Value::array();
    m_seedChunkBytes = 0;

    log::info("[Collab] seed chunk: {} objetos (final={} subidos={}/{})",
              chunkCount, finalChunk, m_seedUploaded, m_seedTotal);
    m_net.sendSeed(objects, finalChunk,
        [this, epoch, finalChunk, chunkCount](bool ok, int status, int accepted, int roomTotal) {
            onSeedAck(ok, status, accepted, roomTotal, finalChunk, epoch);
            (void)chunkCount;
        });
}

void CollabManager::onSeedAck(bool ok, int status, int accepted, int roomTotal, bool wasFinal, uint64_t epoch) {
    if (epoch != m_seedEpoch) return;
    m_seedInflight = false;

    if (!ok) {
        log::warn("[Collab] seed chunk fallo HTTP {}; resincronizando desde cero", status);
        setStatus("Error subiendo el nivel; resincronizando desde el principio...");
        beginResync();
        return;
    }

    m_seedUploaded += static_cast<size_t>(std::max(0, accepted));
    if (wasFinal) {
        m_seeding = false;
        m_seedCursor = 0;
        setStatus(fmt::format("Nivel cargado en la sala ({} objetos)", roomTotal > 0 ? roomTotal : static_cast<int>(m_seedUploaded)));
        log::info("[Collab] seed completo: roomTotal={} localMaps={}", roomTotal, m_gidToObj.size());
        sendLevelSettings(true);
        return;
    }

    setStatus(fmt::format(
        "Subiendo nivel a la sala... {}/{}",
        m_seedUploaded,
        m_seedTotal > 0 ? m_seedTotal : m_seedUploaded
    ));
}

void CollabManager::sendCreatedObject(GameObject* object) {
    if (!shouldEmit() || !object) return;
    if (!canEditObjectLayer(object)) return;
    if (m_uidToGid.count(object->m_uniqueID)) return;
    std::string save = saveObject(object);
    if (save.empty()) return;
    claimObjectLayer(object);
    std::string gid = makeLocalGid();
    mapGid(gid, object);
    m_versionByGid[gid] = 1;
    m_lastSentSave[gid] = save;
    setWireHash(gid, objectSyncHash(gid, 1, save));
    enqueueOp("add", gid, 1, std::move(save));
}

void CollabManager::sendCreatedObjects(CCArray* objects) {
    if (!objects || !shouldEmit()) return;
    for (auto* o : CCArrayExt<GameObject*>(objects)) {
        if (o && !m_uidToGid.count(o->m_uniqueID)) m_deferredCreates.emplace_back(o);
    }
}

void CollabManager::sendUpdatedObject(GameObject* object) {
    queueObjectEdit(object, LocalEditKind::Full);
}

void CollabManager::sendMovedObject(GameObject* object) {
    queueObjectEdit(object, LocalEditKind::Move);
}

void CollabManager::sendRotatedObject(GameObject* object) {
    queueObjectEdit(object, LocalEditKind::Rotate);
}

void CollabManager::sendScaledObject(GameObject* object) {
    queueObjectEdit(object, LocalEditKind::Scale);
}

void CollabManager::sendFlippedObject(GameObject* object) {
    queueObjectEdit(object, LocalEditKind::Flip);
}

void CollabManager::queueObjectEdit(GameObject* object, LocalEditKind kind) {
    if (!shouldEmit() || !object) return;
    if (!canEditObjectLayer(object)) return;
    int uid = object->m_uniqueID;
    auto [it, inserted] = m_deferredEdits.try_emplace(uid, DeferredEdit{object, kind});
    if (!inserted) {
        it->second.object = object;
        it->second.kind = mergeEditKind(it->second.kind, kind);
        return;
    }
    m_deferredEditOrder.push_back(uid);
}

void CollabManager::drainDeferredLocalEdits() {
    auto deadline = std::chrono::steady_clock::now() + kLocalEditBudget;
    size_t processed = 0;

    while (!m_deferredCreates.empty() &&
           (processed == 0 || std::chrono::steady_clock::now() < deadline)) {
        auto object = std::move(m_deferredCreates.front());
        m_deferredCreates.pop_front();
        if (object && object->getParent()) sendCreatedObject(object.data());
        ++processed;
    }

    while (!m_deferredEditOrder.empty() &&
           (processed == 0 || std::chrono::steady_clock::now() < deadline)) {
        int uid = m_deferredEditOrder.front();
        m_deferredEditOrder.pop_front();
        auto it = m_deferredEdits.find(uid);
        if (it == m_deferredEdits.end()) continue;
        auto edit = std::move(it->second);
        m_deferredEdits.erase(it);
        if (edit.object && edit.object->getParent()) {
            sendObjectState(edit.object.data(), edit.kind);
        }
        ++processed;
    }
}

void CollabManager::sendObjectState(GameObject* object, LocalEditKind kind) {
    if (!shouldEmit() || !object) return;
    if (!canEditObjectLayer(object)) return;
    std::string save = saveObject(object);
    if (save.empty()) return;

    auto it = m_uidToGid.find(object->m_uniqueID);
    if (it == m_uidToGid.end()) {
    // Untracked objects are registered as adds.
        claimObjectLayer(object);
        std::string gid = makeLocalGid();
        mapGid(gid, object);
        m_versionByGid[gid] = 1;
        m_lastSentSave[gid] = save;
        setWireHash(gid, objectSyncHash(gid, 1, save));
        enqueueOp("add", gid, 1, std::move(save));
        return;
    }
    std::string gid = it->second;

    // Reconcile callbacks often repeat unchanged updates; skip no-ops.
    auto last = m_lastSentSave.find(gid);
    if (last != m_lastSentSave.end() && last->second == save) return;

    claimObjectLayer(object);
    uint32_t version = ++m_versionByGid[gid];
    m_lastSentSave[gid] = save;
    setWireHash(gid, objectSyncHash(gid, version, save));

    OutOp op;
    op.kind = kindName(kind);
    op.gid = gid;
    op.version = version;
    op.save = std::move(save);
    auto pos = object->getPosition();
    op.x = pos.x;
    op.y = pos.y;
    op.hasPos = true;
    if (kind == LocalEditKind::Rotate) {
        op.rot = object->getRotation();
        op.hasRot = true;
    } else if (kind == LocalEditKind::Scale) {
        op.scaleX = object->getScaleX();
        op.scaleY = object->getScaleY();
        op.hasScale = true;
    } else if (kind == LocalEditKind::Flip) {
        op.flipX = object->isFlipX();
        op.flipY = object->isFlipY();
        op.hasFlip = true;
    } else if (kind == LocalEditKind::Full) {
    op.hasPos = false;
    }
    enqueueOp(std::move(op));
}

void CollabManager::sendUpdatedObjects(CCArray* objects) {
    if (!objects) return;
    for (auto* o : CCArrayExt<GameObject*>(objects)) sendUpdatedObject(o);
}

void CollabManager::sendMovedObjects(CCArray* objects) {
    if (!objects) return;
    for (auto* o : CCArrayExt<GameObject*>(objects)) sendMovedObject(o);
}

void CollabManager::sendRotatedObjects(CCArray* objects) {
    if (!objects) return;
    for (auto* o : CCArrayExt<GameObject*>(objects)) sendRotatedObject(o);
}

void CollabManager::sendScaledObjects(CCArray* objects) {
    if (!objects) return;
    for (auto* o : CCArrayExt<GameObject*>(objects)) sendScaledObject(o);
}

void CollabManager::sendFlippedObjects(CCArray* objects) {
    if (!objects) return;
    for (auto* o : CCArrayExt<GameObject*>(objects)) sendFlippedObject(o);
}

void CollabManager::reconcileObjects(CCArray* objects) {
    if (!objects) return;
    for (auto* obj : CCArrayExt<CCObject*>(objects)) {
        if (auto* o = typeinfo_cast<GameObject*>(obj)) sendUpdatedObject(o);
    }
}

void CollabManager::sendSelection(CCArray* selected) {
    // Selection presence is allowed in view-only, but not during remote apply.
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
    // Large selections use one union AABB.
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
    m_deferredEdits.erase(object->m_uniqueID);
    auto it = m_uidToGid.find(object->m_uniqueID);
    if (it == m_uidToGid.end()) return; // never synced
    std::string gid = it->second;
    uint32_t version = ++m_versionByGid[gid];
    enqueueOp("delete", gid, version, "");
    unmapGid(gid);
}

void CollabManager::sweepEditor() {
    if (!m_editor || !m_editor->m_objects || !shouldEmit()) return;

    auto started = std::chrono::steady_clock::now();
    auto objectDeadline = started + kSweepBudget / 2;
    auto deadline = started + kSweepBudget;
    auto* objects = m_editor->m_objects;
    size_t total = objects->count();
    if (total == 0) m_sweepObjectCursor = 0;
    else if (m_sweepObjectCursor >= total) m_sweepObjectCursor %= total;

    size_t adds = 0;
    size_t checked = 0;
    while (total > 0 && checked < total && checked < kSweepMaxChecksPerPass &&
           (checked == 0 || std::chrono::steady_clock::now() < objectDeadline)) {
        size_t index = m_sweepObjectCursor;
        m_sweepObjectCursor = (m_sweepObjectCursor + 1) % total;
        ++checked;
        auto* o = typeinfo_cast<GameObject*>(
            objects->objectAtIndex(static_cast<unsigned int>(index))
        );
        if (!o || m_uidToGid.count(o->m_uniqueID)) continue;
        sendCreatedObject(o);
        if (++adds >= kSweepMaxAddsPerPass) break;
    }

    std::vector<std::string> gone;
    size_t bucketCount = m_gidToObj.bucket_count();
    size_t bucketsChecked = 0;
    size_t trackedChecked = 0;
    while (bucketCount > 0 && bucketsChecked < bucketCount &&
           trackedChecked < kSweepMaxChecksPerPass &&
           std::chrono::steady_clock::now() < deadline) {
        size_t bucket = m_sweepBucketCursor % bucketCount;
        m_sweepBucketCursor = (bucket + 1) % bucketCount;
        ++bucketsChecked;
        for (auto it = m_gidToObj.begin(bucket); it != m_gidToObj.end(bucket); ++it) {
            ++trackedChecked;
            if (!it->second || !it->second->getParent()) gone.push_back(it->first);
            if (trackedChecked >= kSweepMaxChecksPerPass ||
                std::chrono::steady_clock::now() >= deadline) break;
        }
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
    // Compare digests only when both sides are quiescent.
    if (!m_editor || !connected()) return;
    if (m_seeding || !m_snapshotComplete) return;
    if (!m_applyQueue.empty() || !m_deferredCreates.empty() || !m_deferredEdits.empty() ||
        !m_pendingOps.empty() || !m_outbox.empty() || !m_inflight.empty()) return;
    if (m_digestCooldown > 0.f) return;

    int64_t count = msg["count"].asInt().unwrapOr(-1);
    std::string hash = msg["hash"].asString().unwrapOr("");
    if (count < 0 || hash.empty()) return;

    std::string local = fmt::format("{:016x}", m_wireDigest);

    if (count == static_cast<int64_t>(m_wireHash.size()) && hash == local) {
        m_digestStrikes = 0;
        return;
    }

    // Two quiet mismatches indicate divergence; rebuild automatically.
    if (++m_digestStrikes < 2) return;
    m_digestStrikes = 0;
    m_digestCooldown = m_isHost ? 60.f : 20.f;
    log::warn("[Collab] desync detectado (server: {} obj hash={} | local: {} obj hash={}); auto-resync",
              count, hash, m_wireHash.size(), local);
    setStatus("Desync detectado; resincronizando...");
    beginResync();
}

void CollabManager::notifyOverlayEdit(ApplyObj const& op, GameObject* object) {
    std::string name = !op.by.empty() ? op.by : peerName(op.origin);
    CCPoint pos = object ? object->getPosition() : CCPoint{0.f, 0.f};

    if (object) recordHeat(pos.x, pos.y, op.kind == "delete" ? 0.55f : 1.f);

    if (!m_overlay || op.origin <= 0 || op.origin == m_clientId) return;
    if (!object) return;
    m_overlay->onRemoteEdit(op.origin, name, pos, op.kind == "delete");
}

void CollabManager::applyRemoteAdd(ApplyObj const& op) {
    if (op.save.empty() || op.gid.empty()) return;

    if (m_gidToObj.count(op.gid)) {
        applyRemoteUpdate(op);
        return;
    }

    auto known = m_versionByGid.find(op.gid);
    if (known != m_versionByGid.end() && op.version < known->second) return;

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
    // Keep the local form so reconcile does not echo remote edits.
        m_lastSentSave[op.gid] = saveObject(mapped);
        setWireHash(op.gid, objectSyncHash(op.gid, op.version, op.save));
    } else {
        log::warn("[Collab] no se pudo materializar objeto remoto gid={} ({} bytes)", op.gid, op.save.size());
    }
    notifyOverlayEdit(op, mapped);
}

void CollabManager::applyRemoteUpdate(ApplyObj const& op) {
    if (op.save.empty() || op.gid.empty()) return;

    auto known = m_versionByGid.find(op.gid);
    if (known != m_versionByGid.end() && op.version < known->second) return;

    if (!m_editor) {
        m_versionByGid[op.gid] = op.version;
        return;
    }

    GameObject* existing = findTrackedObject(op.gid);

    GameObject* mapped = nullptr;
    {
        TrackerGuard guard(m_applyingRemote);
        if (existing && existing->getParent()) {
    // Drop the EditorUI pointer before freeing the object.
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
        setWireHash(op.gid, objectSyncHash(op.gid, op.version, op.save));
    } else {
        log::warn("[Collab] no se pudo materializar update remoto gid={} ({} bytes)", op.gid, op.save.size());
    }
    notifyOverlayEdit(op, mapped);
}

void CollabManager::applyRemoteMove(ApplyObj const& op) {
    if (op.gid.empty()) return;

    auto known = m_versionByGid.find(op.gid);
    if (known != m_versionByGid.end() && op.version < known->second) return;

    if (!m_editor) {
        m_versionByGid[op.gid] = op.version;
        return;
    }

    GameObject* existing = findTrackedObject(op.gid);
    if (!existing || !existing->getParent()) {
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
            ApplyObj asUpdate = op;
            asUpdate.kind = "update";
            applyRemoteUpdate(asUpdate);
            return;
        }
    }

    m_versionByGid[op.gid] = op.version;
    if (!op.save.empty()) {
    // Hash the wire save for reconcile no-op checks.
        m_lastSentSave[op.gid] = saveObject(existing);
        setWireHash(op.gid, objectSyncHash(op.gid, op.version, op.save));
    } else {
        std::string save = saveObject(existing);
        m_lastSentSave[op.gid] = save;
        setWireHash(op.gid, objectSyncHash(op.gid, op.version, save));
    }
    notifyOverlayEdit(op, existing);
}

void CollabManager::applyRemoteTransform(ApplyObj const& op) {
    if (op.gid.empty()) return;

    auto known = m_versionByGid.find(op.gid);
    if (known != m_versionByGid.end() && op.version < known->second) return;

    if (!m_editor) {
        m_versionByGid[op.gid] = op.version;
        return;
    }

    GameObject* existing = findTrackedObject(op.gid);
    if (!existing || !existing->getParent()) {
        if (!op.save.empty()) {
            ApplyObj asUpdate = op;
            asUpdate.kind = "update";
            applyRemoteUpdate(asUpdate);
        }
        return;
    }

    bool applied = false;
    {
        TrackerGuard guard(m_applyingRemote);
        if (op.hasPos) existing->setPosition({op.x, op.y});
        if (op.kind == "rotate" && op.hasRot) {
            existing->setRotation(op.rot);
            applied = true;
        } else if (op.kind == "scale" && op.hasScale) {
            existing->setScaleX(op.scaleX);
            existing->setScaleY(op.scaleY);
            applied = true;
        } else if (op.kind == "flip" && op.hasFlip) {
            existing->setFlipX(op.flipX);
            existing->setFlipY(op.flipY);
            applied = true;
        }
        if (!applied && !op.save.empty()) {
            ApplyObj asUpdate = op;
            asUpdate.kind = "update";
            applyRemoteUpdate(asUpdate);
            return;
        }
    }

    m_versionByGid[op.gid] = op.version;
    if (!op.save.empty()) {
        m_lastSentSave[op.gid] = saveObject(existing);
        setWireHash(op.gid, objectSyncHash(op.gid, op.version, op.save));
    } else {
        std::string save = saveObject(existing);
        m_lastSentSave[op.gid] = save;
        setWireHash(op.gid, objectSyncHash(op.gid, op.version, save));
    }
    notifyOverlayEdit(op, existing);
}

void CollabManager::applyRemoteDelete(ApplyObj const& op) {
    if (op.gid.empty()) return;

    auto known = m_versionByGid.find(op.gid);
    if (known != m_versionByGid.end() && op.version < known->second) return;

    GameObject* existing = findTrackedObject(op.gid);

    if (m_editor && existing && existing->getParent()) {
        notifyOverlayEdit(op, existing);
        TrackerGuard guard(m_applyingRemote);
        if (m_editor->m_editorUI) m_editor->m_editorUI->deselectObject(existing);
        m_editor->removeObject(existing, true);
    }

    unmapGid(op.gid);
    m_versionByGid[op.gid] = op.version; // Reject stale re-adds.
}

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

bool CollabManager::clientCanEditColors() const {
    if (!connected() || m_isHost) return true;
    if (m_permissions.viewOnly) return false;
    return m_permissions.allowColors || m_permissions.allowLevelSettings;
}

bool CollabManager::canEditObjectLayer(GameObject* object) const {
    if (!object) return true;
    if (!m_permissions.strictLayers || m_isHost) return true;
    auto it = m_layerOwners.find(static_cast<int>(object->m_editorLayer));
    if (it == m_layerOwners.end()) return true;
    return it->second == m_clientId;
}

void CollabManager::claimObjectLayer(GameObject* object) {
    if (!object || !m_permissions.strictLayers || m_clientId <= 0) return;
    int layer = static_cast<int>(object->m_editorLayer);
    auto it = m_layerOwners.find(layer);
    if (it != m_layerOwners.end() && it->second != 0 && it->second != m_clientId) return;
    if (it != m_layerOwners.end() && it->second == m_clientId) return;
    m_layerOwners[layer] = m_clientId;
    m_net.sendJson(matjson::makeObject({
        {"t", "claim_layer"},
        {"layer", static_cast<int64_t>(layer)},
    }));
}

void CollabManager::handleLayerOwners(matjson::Value const& msg) {
    matjson::Value const* src = &msg;
    if (msg.isObject() && msg.contains("owners") && msg["owners"].isObject()) {
        src = &msg["owners"];
    }
    if (!src->isObject()) return;

    m_layerOwners.clear();
    for (auto const& value : *src) {
        auto key = value.getKey();
        if (!key) continue;
        int layer = 0;
        try { layer = std::stoi(*key); } catch (...) { continue; }
        int owner = static_cast<int>(value.asInt().unwrapOr(0));
        if (owner > 0) m_layerOwners[layer] = owner;
    }
}

void CollabManager::sendCameraPresence() {
    if (m_state != ConnState::Connected || m_clientId <= 0 || !m_editor || !m_editor->m_objectLayer) return;
    auto* layer = m_editor->m_objectLayer;
    auto win = CCDirector::sharedDirector()->getWinSize();
    auto cam = layer->convertToNodeSpace(win / 2.f);
    auto mouse = geode::cocos::getMousePos();
    bool cursorVisible = mouse.x >= 0.f && mouse.y >= 0.f &&
        mouse.x <= win.width && mouse.y <= win.height;
    auto cursor = layer->convertToNodeSpace(mouse);
    float zoom = layer->getScale();
    if (std::abs(cam.x - m_lastCamX) < kCameraMoveEpsilon &&
        std::abs(cam.y - m_lastCamY) < kCameraMoveEpsilon &&
        std::abs(zoom - m_lastCamZoom) < kCameraZoomEpsilon &&
        std::abs(cursor.x - m_lastCursorX) < kCursorMoveEpsilon &&
        std::abs(cursor.y - m_lastCursorY) < kCursorMoveEpsilon &&
        cursorVisible == m_lastCursorVisible &&
        !m_cameraDirty) {
        return;
    }
    m_lastCamX = cam.x;
    m_lastCamY = cam.y;
    m_lastCamZoom = zoom;
    m_lastCursorX = cursor.x;
    m_lastCursorY = cursor.y;
    m_lastCursorVisible = cursorVisible;
    m_pendingCameraJson = matjson::makeObject({
        {"t", "camera"},
        {"x", static_cast<double>(cam.x)},
        {"y", static_cast<double>(cam.y)},
        {"z", static_cast<double>(zoom)},
        {"mx", static_cast<double>(cursor.x)},
        {"my", static_cast<double>(cursor.y)},
        {"mv", cursorVisible},
    });
    m_cameraDirty = true;
}

void CollabManager::flushCameraIfNeeded() {
    if (!m_cameraDirty || !connected() || !m_net.isOpen()) return;
    m_cameraDirty = false;
    m_sinceCameraFlush = 0.f;
    if (m_pendingCameraJson.isObject()) {
        m_net.sendJson(m_pendingCameraJson);
    }
}

void CollabManager::handlePeerCamera(matjson::Value const& msg) {
    int from = static_cast<int>(msg["from"].asInt().unwrapOr(0));
    if (from <= 0 || from == m_clientId) return;

    PeerCamera cam;
    cam.clientId = from;
    cam.name = msg.contains("name") ? msg["name"].asString().unwrapOr("") : peerName(from);
    cam.x = static_cast<float>(msg["x"].asDouble().unwrapOr(0.0));
    cam.y = static_cast<float>(msg["y"].asDouble().unwrapOr(0.0));
    cam.zoom = static_cast<float>(msg["z"].asDouble().unwrapOr(1.0));
    cam.cursorX = static_cast<float>(msg["mx"].asDouble().unwrapOr(cam.x));
    cam.cursorY = static_cast<float>(msg["my"].asDouble().unwrapOr(cam.y));
    cam.cursorVisible = !msg.contains("mv") || msg["mv"].asBool().unwrapOr(true);
    cam.age = 0.f;
    m_peerCameras[from] = cam;

    PeerAppearance appearance;
    if (auto it = m_peers.find(from); it != m_peers.end()) appearance = it->second.appearance;
    if (m_overlay) {
        m_overlay->onPeerCamera(from, cam.name, cam.cursorX, cam.cursorY,
                                cam.cursorVisible, appearance);
    }
}

void CollabManager::clearPeerCamera(int clientId) {
    m_peerCameras.erase(clientId);
    if (m_overlay) m_overlay->onPeerCameraCleared(clientId);
}

void CollabManager::sendWorkZone() {
    if (m_state != ConnState::Connected || m_clientId <= 0 || !m_editor || !m_editor->m_objectLayer) return;
    auto* layer = m_editor->m_objectLayer;
    auto win = CCDirector::sharedDirector()->getWinSize();
    auto bl = layer->convertToNodeSpace({0.f, 0.f});
    auto tr = layer->convertToNodeSpace({win.width, win.height});
    float x = std::min(bl.x, tr.x);
    float y = std::min(bl.y, tr.y);
    float w = std::abs(tr.x - bl.x);
    float h = std::abs(tr.y - bl.y);
    if (w < 8.f || h < 8.f) return;

    if (std::abs(x - m_lastZoneX) < kWorkZoneMoveEpsilon &&
        std::abs(y - m_lastZoneY) < kWorkZoneMoveEpsilon &&
        std::abs(w - m_lastZoneW) < kWorkZoneMoveEpsilon * 2.f &&
        std::abs(h - m_lastZoneH) < kWorkZoneMoveEpsilon * 2.f &&
        !m_workZoneDirty) {
        return;
    }
    m_lastZoneX = x;
    m_lastZoneY = y;
    m_lastZoneW = w;
    m_lastZoneH = h;
    m_pendingWorkZoneJson = matjson::makeObject({
        {"t", "workzone"},
        {"x", static_cast<double>(x)},
        {"y", static_cast<double>(y)},
        {"w", static_cast<double>(w)},
        {"h", static_cast<double>(h)},
    });
    m_workZoneDirty = true;
}

void CollabManager::flushWorkZoneIfNeeded() {
    if (!m_workZoneDirty || !connected() || !m_net.isOpen()) return;
    m_workZoneDirty = false;
    m_sinceWorkZoneFlush = 0.f;
    if (m_pendingWorkZoneJson.isObject()) {
        m_net.sendJson(m_pendingWorkZoneJson);
    }
}

void CollabManager::handlePeerWorkZone(matjson::Value const& msg) {
    int from = static_cast<int>(msg["from"].asInt().unwrapOr(0));
    if (from <= 0 || from == m_clientId) return;

    PeerWorkZone z;
    z.clientId = from;
    z.name = msg.contains("name") ? msg["name"].asString().unwrapOr("") : peerName(from);
    z.x = static_cast<float>(msg["x"].asDouble().unwrapOr(0.0));
    z.y = static_cast<float>(msg["y"].asDouble().unwrapOr(0.0));
    z.w = static_cast<float>(msg["w"].asDouble().unwrapOr(0.0));
    z.h = static_cast<float>(msg["h"].asDouble().unwrapOr(0.0));
    z.age = 0.f;
    if (z.w <= 0.f || z.h <= 0.f) {
        clearPeerWorkZone(from);
        return;
    }
    m_peerWorkZones[from] = z;
    if (m_overlay) m_overlay->onPeerWorkZone(from, z.name, z.x, z.y, z.w, z.h);
}

void CollabManager::clearPeerWorkZone(int clientId) {
    m_peerWorkZones.erase(clientId);
    if (m_overlay) m_overlay->onPeerWorkZoneCleared(clientId);
}

void CollabManager::sendPing(float x, float y) {
    if (m_state != ConnState::Connected || m_clientId <= 0 || !m_net.isOpen()) return;
    m_net.sendJson(matjson::makeObject({
        {"t", "ping"},
        {"x", static_cast<double>(x)},
        {"y", static_cast<double>(y)},
    }));
    PeerPing local;
    local.clientId = m_clientId;
    local.name = m_username.empty() ? "Tu" : m_username;
    local.x = x;
    local.y = y;
    local.age = 0.f;
    m_peerPings.push_back(local);
    if (m_overlay) m_overlay->onPeerPing(local.clientId, local.name, x, y);
    pushChatMessage({0, "", fmt::format("Ping en ({:.0f}, {:.0f})", x, y)});
    recordHeat(x, y, 1.4f);
}

void CollabManager::handlePeerPing(matjson::Value const& msg) {
    int from = static_cast<int>(msg["from"].asInt().unwrapOr(0));
    if (from <= 0 || from == m_clientId) return;
    PeerPing p;
    p.clientId = from;
    p.name = msg.contains("name") ? msg["name"].asString().unwrapOr("") : peerName(from);
    p.x = static_cast<float>(msg["x"].asDouble().unwrapOr(0.0));
    p.y = static_cast<float>(msg["y"].asDouble().unwrapOr(0.0));
    p.age = 0.f;
    m_peerPings.push_back(p);
    if (m_overlay) m_overlay->onPeerPing(p.clientId, p.name, p.x, p.y);
    pushChatMessage({0, "", fmt::format("{}: mira aqui ({:.0f}, {:.0f})", p.name, p.x, p.y)});
    recordHeat(p.x, p.y, 1.2f);
}

void CollabManager::tickPings(float dt) {
    for (size_t i = 0; i < m_peerPings.size();) {
        m_peerPings[i].age += dt;
        if (m_peerPings[i].age >= m_peerPings[i].life) {
            m_peerPings.erase(m_peerPings.begin() + static_cast<long>(i));
        } else {
            ++i;
        }
    }
    while (m_peerPings.size() > 24) m_peerPings.erase(m_peerPings.begin());
}

std::string CollabManager::cycleFollowPeer() {
    if (!connected() || m_peers.empty()) {
        clearFollow();
        return {};
    }
    std::vector<int> ids;
    ids.reserve(m_peers.size());
    for (auto const& [id, info] : m_peers) {
        if (id != m_clientId) ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    if (ids.empty()) {
        clearFollow();
        return {};
    }

    int next = 0;
    if (m_followClientId == 0) {
        next = ids.front();
    } else {
        auto it = std::find(ids.begin(), ids.end(), m_followClientId);
        if (it == ids.end() || it + 1 == ids.end()) {
            clearFollow();
            return {};
        }
        next = *(it + 1);
    }
    m_followClientId = next;
    auto name = peerName(next);
    setStatus(fmt::format("Siguiendo a {}", name.empty() ? fmt::format("#{}", next) : name));
    return name;
}

void CollabManager::clearFollow() {
    if (m_followClientId == 0) return;
    m_followClientId = 0;
    if (connected()) setStatus(fmt::format("En sala '{}' #{}", m_roomCode, m_clientId));
}

void CollabManager::tickFollow() {
    if (m_followClientId <= 0 || !m_editor) return;
    auto it = m_peerCameras.find(m_followClientId);
    if (it == m_peerCameras.end()) return;
    paimon::editor::focusCameraOnPoint(m_editor, {it->second.x, it->second.y});
    if (m_editor->m_editorUI) m_editor->m_editorUI->updateSlider();
}

void CollabManager::recordHeat(float x, float y, float amount) {
    int gx = static_cast<int>(std::floor(x / kHeatCellSize));
    int gy = static_cast<int>(std::floor(y / kHeatCellSize));
    for (auto& c : m_heatCells) {
        if (c.gx == gx && c.gy == gy) {
            c.heat = std::min(kMaxCellHeat, c.heat + amount * kHeatGain);
            return;
        }
    }
    if (m_heatCells.size() >= kMaxHeatCells) {
        auto cold = std::min_element(m_heatCells.begin(), m_heatCells.end(),
            [](HeatCell const& a, HeatCell const& b) { return a.heat < b.heat; });
        if (cold != m_heatCells.end()) m_heatCells.erase(cold);
    }
    m_heatCells.push_back({gx, gy, amount * kHeatGain});
}

void CollabManager::tickHeatmap(float dt) {
    for (auto it = m_heatCells.begin(); it != m_heatCells.end();) {
        it->heat -= kHeatDecayPerSec * dt;
        if (it->heat <= 0.05f) it = m_heatCells.erase(it);
        else ++it;
    }
}

std::vector<CollabManager::HeatSample> CollabManager::heatmapSamples(size_t maxCount) const {
    std::vector<HeatSample> out;
    if (m_heatCells.empty() || maxCount == 0) return out;
    out.reserve(std::min(maxCount, m_heatCells.size()));
    float peak = 0.01f;
    for (auto const& c : m_heatCells) peak = std::max(peak, c.heat);
    for (auto const& c : m_heatCells) {
        HeatSample s;
        s.x = (static_cast<float>(c.gx) + 0.5f) * kHeatCellSize;
        s.y = (static_cast<float>(c.gy) + 0.5f) * kHeatCellSize;
        s.intensity = std::clamp(c.heat / peak, 0.f, 1.f);
        out.push_back(s);
        if (out.size() >= maxCount) break;
    }
    return out;
}

std::string CollabManager::captureLevelMetaSignature() const {
    if (!m_editor) return {};
    std::string sig;
    sig.reserve(256);
    if (m_editor->m_levelSettings) {
        sig += std::string(m_editor->m_levelSettings->getSaveString());
    }
    sig += '|';
    GJEffectManager* em = nullptr;
    if (m_editor->m_levelSettings) em = m_editor->m_levelSettings->m_effectManager;
    if (!em) em = m_editor->m_effectManager;
    if (em) sig += std::string(em->getSaveString());
    sig += '|';
    if (auto* level = m_editor->m_level) {
        sig += std::to_string(level->m_audioTrack);
        sig += ':';
        sig += std::to_string(level->m_songID);
        sig += ':';
        sig += std::string(level->m_songIDs);
        sig += ':';
        sig += std::string(level->m_sfxIDs);
    }
    return sig;
}

matjson::Value CollabManager::buildLevelMetaPayload() const {
    auto payload = matjson::makeObject({{"t", "level_settings"}});
    if (!m_editor) return payload;

    if (m_editor->m_levelSettings) {
        payload["settings"] = std::string(m_editor->m_levelSettings->getSaveString());
        auto* s = m_editor->m_levelSettings;
        payload["startMode"] = static_cast<int64_t>(s->m_startMode);
        payload["startSpeed"] = static_cast<int64_t>(static_cast<int>(s->m_startSpeed));
        payload["startMini"] = s->m_startMini;
        payload["startDual"] = s->m_startDual;
        payload["twoPlayer"] = s->m_twoPlayerMode;
        payload["platformer"] = s->m_platformerMode;
        payload["songOffset"] = static_cast<double>(s->m_songOffset);
        payload["bg"] = static_cast<int64_t>(s->m_backgroundIndex);
        payload["gnd"] = static_cast<int64_t>(s->m_groundIndex);
        payload["font"] = static_cast<int64_t>(s->m_fontIndex);
        payload["mg"] = static_cast<int64_t>(s->m_middleGroundIndex);
    }

    GJEffectManager* em = nullptr;
    if (m_editor->m_levelSettings) em = m_editor->m_levelSettings->m_effectManager;
    if (!em) em = m_editor->m_effectManager;
    if (em) payload["colors"] = std::string(em->getSaveString());

    if (auto* level = m_editor->m_level) {
        payload["audioTrack"] = static_cast<int64_t>(level->m_audioTrack);
        payload["songID"] = static_cast<int64_t>(level->m_songID);
        payload["songIDs"] = std::string(level->m_songIDs);
        payload["sfxIDs"] = std::string(level->m_sfxIDs);
    }
    return payload;
}

void CollabManager::sendLevelSettings(bool force) {
    if (m_state != ConnState::Connected || m_clientId <= 0 || m_applyingRemote) return;
    if (!m_editor) return;
    if (!m_isHost && m_permissions.viewOnly) return;
    if (!m_isHost && !m_permissions.allowLevelSettings && !m_permissions.allowSong &&
        !m_permissions.allowColors) {
        return;
    }

    auto sig = captureLevelMetaSignature();
    if (!force && !sig.empty() && sig == m_lastMetaSig) return;
    m_lastMetaSig = sig;

    auto payload = buildLevelMetaPayload();
    m_net.sendJson(payload);
    log::info("[Collab] level meta enviado (settings={} colors={} song={})",
              payload.contains("settings"), payload.contains("colors"),
              payload.contains("songID"));
}

void CollabManager::reconcileLevelMeta() {
    if (!shouldEmit() || !m_editor) return;
    if (!m_isHost && !m_permissions.allowLevelSettings && !m_permissions.allowSong &&
        !m_permissions.allowColors) {
        return;
    }
    sendLevelSettings(false);
}

void CollabManager::applyLevelSettingsSave(std::string const& save) {
    if (save.empty() || !m_editor || !m_editor->m_levelSettings) return;
    auto* live = m_editor->m_levelSettings;
    auto* parsed = LevelSettingsObject::objectFromString(gd::string(save));
    if (!parsed) return;

    live->m_startMode = parsed->m_startMode;
    live->m_startSpeed = parsed->m_startSpeed;
    live->m_startMini = parsed->m_startMini;
    live->m_startDual = parsed->m_startDual;
    live->m_mirrorMode = parsed->m_mirrorMode;
    live->m_rotateGameplay = parsed->m_rotateGameplay;
    live->m_twoPlayerMode = parsed->m_twoPlayerMode;
    live->m_platformerMode = parsed->m_platformerMode;
    live->m_songOffset = parsed->m_songOffset;
    live->m_fadeIn = parsed->m_fadeIn;
    live->m_fadeOut = parsed->m_fadeOut;
    live->m_dontReset = parsed->m_dontReset;
    live->m_backgroundIndex = parsed->m_backgroundIndex;
    live->m_groundIndex = parsed->m_groundIndex;
    live->m_fontIndex = parsed->m_fontIndex;
    live->m_middleGroundIndex = parsed->m_middleGroundIndex;
    live->m_startsWithStartPos = parsed->m_startsWithStartPos;
    live->m_isFlipped = parsed->m_isFlipped;
    live->m_reverseGameplay = parsed->m_reverseGameplay;
    live->m_disableStartPos = parsed->m_disableStartPos;
    live->m_targetOrder = parsed->m_targetOrder;
    live->m_targetChannel = parsed->m_targetChannel;
    live->m_guidelineString = parsed->m_guidelineString;
    live->m_guidelinesUpdated = parsed->m_guidelinesUpdated;
    live->m_colorPage = parsed->m_colorPage;
    live->m_groundLineIndex = parsed->m_groundLineIndex;
    live->m_propertykA23 = parsed->m_propertykA23;
    live->m_propertykA24 = parsed->m_propertykA24;
    live->m_noTimePenalty = parsed->m_noTimePenalty;
    live->m_propertykA44 = parsed->m_propertykA44;
    live->m_nextFreeID = parsed->m_nextFreeID;
    live->m_resetCamera = parsed->m_resetCamera;
    live->m_spawnGroup = parsed->m_spawnGroup;
    live->m_allowMultiRotation = parsed->m_allowMultiRotation;
    live->m_enablePlayerSqueeze = parsed->m_enablePlayerSqueeze;
    live->m_fixGravityBug = parsed->m_fixGravityBug;
    live->m_fixNegativeScale = parsed->m_fixNegativeScale;
    live->m_fixRobotJump = parsed->m_fixRobotJump;
    live->m_dynamicLevelHeight = parsed->m_dynamicLevelHeight;
    live->m_sortGroups = parsed->m_sortGroups;
    live->m_fixRadiusCollision = parsed->m_fixRadiusCollision;
    live->m_enable22Changes = parsed->m_enable22Changes;
    live->m_allowStaticRotate = parsed->m_allowStaticRotate;
    live->m_reverseSync = parsed->m_reverseSync;
    live->m_decreaseBoostSlide = parsed->m_decreaseBoostSlide;
    live->m_enableImpulseFix = parsed->m_enableImpulseFix;
}

void CollabManager::applyColorsSave(std::string const& save) {
    if (save.empty() || !m_editor) return;
    GJEffectManager* em = nullptr;
    if (m_editor->m_levelSettings) em = m_editor->m_levelSettings->m_effectManager;
    if (!em) em = m_editor->m_effectManager;
    if (!em) return;
    em->setupFromString(gd::string(save));
}

void CollabManager::applySongMeta(matjson::Value const& msg) {
    if (!m_editor || !m_editor->m_level) return;
    auto* level = m_editor->m_level;
    if (msg.contains("audioTrack")) {
        level->m_audioTrack = static_cast<int>(msg["audioTrack"].asInt().unwrapOr(level->m_audioTrack));
    }
    if (msg.contains("songID")) {
        level->m_songID = static_cast<int>(msg["songID"].asInt().unwrapOr(level->m_songID));
    }
    if (msg.contains("songIDs")) {
        level->m_songIDs = gd::string(msg["songIDs"].asString().unwrapOr(std::string(level->m_songIDs)));
    }
    if (msg.contains("sfxIDs")) {
        level->m_sfxIDs = gd::string(msg["sfxIDs"].asString().unwrapOr(std::string(level->m_sfxIDs)));
    }
}

void CollabManager::handleLevelSettings(matjson::Value const& msg) {
    int from = static_cast<int>(msg["from"].asInt().unwrapOr(0));
    if (from > 0 && from == m_clientId) return;
    if (!m_editor) {
        m_pendingLevelMeta = msg;
        m_hasPendingLevelMeta = true;
        return;
    }

    TrackerGuard guard(m_applyingRemote);

    if (msg.contains("settings")) {
        auto save = msg["settings"].asString().unwrapOr("");
        if (!save.empty()) applyLevelSettingsSave(save);
    } else if (m_editor->m_levelSettings) {
        auto* s = m_editor->m_levelSettings;
        if (msg.contains("startMode")) s->m_startMode = static_cast<int>(msg["startMode"].asInt().unwrapOr(s->m_startMode));
        if (msg.contains("startSpeed")) s->m_startSpeed = static_cast<Speed>(static_cast<int>(msg["startSpeed"].asInt().unwrapOr(static_cast<int>(s->m_startSpeed))));
        if (msg.contains("startMini")) s->m_startMini = msg["startMini"].asBool().unwrapOr(s->m_startMini);
        if (msg.contains("startDual")) s->m_startDual = msg["startDual"].asBool().unwrapOr(s->m_startDual);
        if (msg.contains("twoPlayer")) s->m_twoPlayerMode = msg["twoPlayer"].asBool().unwrapOr(s->m_twoPlayerMode);
        if (msg.contains("platformer")) s->m_platformerMode = msg["platformer"].asBool().unwrapOr(s->m_platformerMode);
        if (msg.contains("songOffset")) s->m_songOffset = static_cast<float>(msg["songOffset"].asDouble().unwrapOr(s->m_songOffset));
        if (msg.contains("bg")) s->m_backgroundIndex = static_cast<int>(msg["bg"].asInt().unwrapOr(s->m_backgroundIndex));
        if (msg.contains("gnd")) s->m_groundIndex = static_cast<int>(msg["gnd"].asInt().unwrapOr(s->m_groundIndex));
        if (msg.contains("font")) s->m_fontIndex = static_cast<int>(msg["font"].asInt().unwrapOr(s->m_fontIndex));
        if (msg.contains("mg")) s->m_middleGroundIndex = static_cast<int>(msg["mg"].asInt().unwrapOr(s->m_middleGroundIndex));
    }

    if (msg.contains("colors")) {
        auto colors = msg["colors"].asString().unwrapOr("");
        if (!colors.empty()) applyColorsSave(colors);
    }

    applySongMeta(msg);

    if (m_editor->m_levelSettings) {
        m_editor->levelSettingsUpdated();
    }
    m_lastMetaSig = captureLevelMetaSignature();
}

}
