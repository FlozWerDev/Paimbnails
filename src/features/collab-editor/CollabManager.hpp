#pragma once

#include "CollabNetClient.hpp"
#include "CollabTypes.hpp"

#include <Geode/Geode.hpp>
#include <chrono>
#include <cstdint>
#include <deque>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

class GameObject;
class GJGameLevel;
class LevelEditorLayer;

namespace paimon::collab {

class CollabEditorOverlay;

class CollabManager {
public:
    static CollabManager& get();

    void connect(std::string const& roomCode, std::string const& username, ConnectMode mode, GJGameLevel* hostLevel = nullptr);
    void disconnect();
    void closeRoom();

    ConnState state() const;
    bool connected() const;
    bool isHost() const;
    bool isApplyingRemote() const;
    bool isViewOnly() const;
    bool canEditObjects() const;
    HostPermissions permissions() const;
    std::string status() const;
    std::string roomCode() const;
    int peerCount() const;
    int clientId() const { return m_clientId; }
    std::vector<PeerInfo> peers() const;
    std::string peerName(int clientId) const;
    static PeerAppearance localAppearance();

    void setHostPermissions(HostPermissions permissions);
    void kickPeer(int targetClientId);
    void setEditor(LevelEditorLayer* editor);
    void clearEditor(LevelEditorLayer* editor);
    LevelEditorLayer* editor() const { return m_editor; }
    GJGameLevel* hostLevel() const { return m_hostLevel.data(); }

    // Non-owning overlay for the current editor scene.
    void setOverlay(CollabEditorOverlay* overlay);
    void clearOverlay(CollabEditorOverlay* overlay);

    void sendCreatedObject(GameObject* object);
    void sendCreatedObjects(cocos2d::CCArray* objects);
    void sendUpdatedObject(GameObject* object);
    void sendUpdatedObjects(cocos2d::CCArray* objects);
    // Full saves feed digests; remote transforms apply in place to avoid flicker.
    void sendMovedObject(GameObject* object);
    void sendMovedObjects(cocos2d::CCArray* objects);
    void sendRotatedObject(GameObject* object);
    void sendRotatedObjects(cocos2d::CCArray* objects);
    void sendScaledObject(GameObject* object);
    void sendScaledObjects(cocos2d::CCArray* objects);
    void sendFlippedObject(GameObject* object);
    void sendFlippedObjects(cocos2d::CCArray* objects);
    void sendDeletedObject(GameObject* object, std::string const& beforeSave = {});
    void reconcileObjects(cocos2d::CCArray* objects);

    void sendSelection(cocos2d::CCArray* selected);
    std::unordered_map<int, PeerSelection> const& peerSelections() const { return m_peerSelections; }

    void sendCameraPresence();
    std::unordered_map<int, PeerCamera> const& peerCameras() const { return m_peerCameras; }
    std::unordered_map<int, PeerWorkZone> const& peerWorkZones() const { return m_peerWorkZones; }
    std::vector<PeerPing> const& peerPings() const { return m_peerPings; }

    void sendWorkZone();
    void sendPing(float x, float y);
    std::string cycleFollowPeer();
    void clearFollow();
    int followClientId() const { return m_followClientId; }
    struct HeatSample { float x = 0.f; float y = 0.f; float intensity = 0.f; };
    std::vector<HeatSample> heatmapSamples(size_t maxCount = 120) const;
    void recordHeat(float x, float y, float amount = 1.f);

    void sendLevelSettings(bool force = false);
    void reconcileLevelMeta();

    bool canEditObjectLayer(GameObject* object) const;
    std::unordered_map<int, int> const& layerOwners() const { return m_layerOwners; }
    bool isRecovering() const { return m_recovering; }

    using InviteCb = std::function<void(bool ok, bool online, std::string const& message)>;
    void inviteUser(int accountId, std::string const& targetName, InviteCb cb = {});

    void sendChat(std::string const& text);
    std::vector<ChatMessage> recentChat(size_t maxCount = 40) const;
    uint64_t chatRevision() const { return m_chatRevision; }

    void sendVoiceFrame(uint32_t seq, std::string const& b64);

    void tick();

    bool clientCanOpenSong() const;
    bool clientCanOpenOptions() const;
    bool clientCanOpenLevelSettings() const;
    bool clientCanEditColors() const;

private:
    struct OutOp {
        std::string kind;
        std::string gid;
        uint32_t version = 0;
        std::string save;
        float x = 0.f;
        float y = 0.f;
        bool hasPos = false;
        float rot = 0.f;
        bool hasRot = false;
        float scaleX = 1.f;
        float scaleY = 1.f;
        bool hasScale = false;
        bool flipX = false;
        bool flipY = false;
        bool hasFlip = false;
    };

    struct ApplyObj {
        std::string kind = "add";
        std::string gid;
        std::string save;
        uint32_t version = 0;
        int origin = 0;
        std::string by;
        float x = 0.f;
        float y = 0.f;
        bool hasPos = false;
        float rot = 0.f;
        bool hasRot = false;
        float scaleX = 1.f;
        float scaleY = 1.f;
        bool hasScale = false;
        bool flipX = false;
        bool flipY = false;
        bool hasFlip = false;
    };

    struct DeferredEdit {
        geode::Ref<GameObject> object;
        LocalEditKind kind = LocalEditKind::Full;
    };

    CollabManager();
    ~CollabManager();

    void setStatus(std::string message);
    void onState(ConnState state, std::string const& message);
    void onMessage(matjson::Value const& msg);
    void handleMessage(matjson::Value const& msg);
    void queueRemote(ApplyObj op);
    void handlePeerList(matjson::Value const& peersJson);
    void pushChatMessage(ChatMessage msg);

    void teardownAndNotify(std::string const& text);
    bool tryRecoverSession();
    void discardJoinerLevel();

    void enqueueOp(OutOp op);
    void enqueueOp(std::string kind, std::string const& gid, uint32_t version, std::string save,
                   float x = 0.f, float y = 0.f, bool hasPos = false);
    void flushSelectionIfNeeded();
    void handlePeerSelection(matjson::Value const& msg);
    void clearPeerSelection(int clientId);
    void flushCameraIfNeeded();
    void handlePeerCamera(matjson::Value const& msg);
    void clearPeerCamera(int clientId);
    void flushWorkZoneIfNeeded();
    void handlePeerWorkZone(matjson::Value const& msg);
    void clearPeerWorkZone(int clientId);
    void handlePeerPing(matjson::Value const& msg);
    void tickFollow();
    void tickHeatmap(float dt);
    void tickPings(float dt);
    void handleLayerOwners(matjson::Value const& msg);
    void claimObjectLayer(GameObject* object);
    void handleLevelSettings(matjson::Value const& msg);
    std::string captureLevelMetaSignature() const;
    matjson::Value buildLevelMetaPayload() const;
    void applyLevelSettingsSave(std::string const& save);
    void applyColorsSave(std::string const& save);
    void applySongMeta(matjson::Value const& msg);
    static bool isCheapKind(std::string const& kind);
    void flushOutgoing();
    void pumpOutbox();
    void sendInflightChunk();
    // Acknowledge the in-flight chunk and retry failures in order.
    void onOpsAck(bool ok, int status);
    void handleDigest(matjson::Value const& msg);
    void sweepEditor();
    void queueObjectEdit(GameObject* object, LocalEditKind kind);
    void drainDeferredLocalEdits();
    void sendObjectState(GameObject* object, LocalEditKind kind);

    void openJoinerEditor();
    void openHostEditor();

    void resetEditorState();
    void wipeEditorObjects();
    void beginResync();

    size_t seedFromEditor();
    void flushSeedChunk(bool finalChunk);
    void onSeedAck(bool ok, int status, int accepted, int roomTotal, bool wasFinal, uint64_t epoch);
    void applyRemoteAdd(ApplyObj const& op);
    void applyRemoteUpdate(ApplyObj const& op);
    void applyRemoteMove(ApplyObj const& op);
    void applyRemoteTransform(ApplyObj const& op);
    void applyRemoteDelete(ApplyObj const& op);
    void notifyOverlayEdit(ApplyObj const& op, GameObject* object);
    static void fillApplyTransform(ApplyObj& op, matjson::Value const& item);
    static void writeOutTransform(matjson::Value& obj, OutOp const& op);

    GameObject* findTrackedObject(std::string const& gid) const;
    std::string saveObject(GameObject* object) const;
    void mapGid(std::string const& gid, GameObject* object);
    void unmapGid(std::string const& gid);
    void setWireHash(std::string const& gid, uint64_t hash);
    void eraseWireHash(std::string const& gid);
    std::string makeLocalGid();
    bool shouldEmit() const;

    CollabNetClient m_net;

    ConnState m_state = ConnState::Disconnected;
    std::string m_status = "Collab apagado";
    std::string m_roomCode;
    std::string m_username;
    int m_clientId = 0;
    bool m_isHost = false;
    uint64_t m_serverSeq = 0;

    std::unordered_map<int, PeerInfo> m_peers;

    LevelEditorLayer* m_editor = nullptr;
    CollabEditorOverlay* m_overlay = nullptr;
    bool m_applyingRemote = false;

    HostPermissions m_permissions;

    // GID mapping keeps remote lookups alive and O(1).
    std::unordered_map<int, std::string> m_uidToGid;
    std::unordered_map<std::string, geode::Ref<GameObject>> m_gidToObj;
    std::unordered_map<std::string, uint32_t> m_versionByGid;
    // Last wire save per GID, used to skip no-op resends.
    std::unordered_map<std::string, std::string> m_lastSentSave;
    uint64_t m_localSeq = 1;

    // Bulk hooks stage refs; saves are generated within the frame budget.
    std::deque<geode::Ref<GameObject>> m_deferredCreates;
    std::deque<int> m_deferredEditOrder;
    std::unordered_map<int, DeferredEdit> m_deferredEdits;

    std::vector<OutOp> m_pendingOps;
    std::unordered_map<std::string, size_t> m_pendingIndexByGid;
    // Throttle updates; structural edits flush on the next tick.
    float m_sinceFlush = 0.f;
    bool m_pendingStructural = false;

    // Ordered acknowledged outbox with token-bucket pacing.
    std::list<OutOp> m_outbox;
    std::unordered_map<std::string, std::list<OutOp>::iterator> m_outboxByGid;
    std::vector<OutOp> m_inflight;
    // Invalidates stale callbacks after a reset.
    uint64_t m_sendEpoch = 0;
    float m_retryTimer = 0.f;
    int m_sendFailures = 0;
    float m_opTokens = kDefaultOpsPerSecond;
    float m_opsPerSec = kDefaultOpsPerSecond;
    size_t m_maxOpsPerRequest = kDefaultOpsPerRequest;
    size_t m_syncTotal = 0;

    // Local GID/version/save digest for periodic server comparison.
    std::unordered_map<std::string, uint64_t> m_wireHash;
    uint64_t m_wireDigest = 0;
    int m_digestStrikes = 0;
    float m_digestCooldown = 0.f;

    // Rotating cursors keep large updates within the frame budget.
    size_t m_reconcileCursor = 0;
    int m_sweepTicks = 0;
    size_t m_sweepObjectCursor = 0;
    size_t m_sweepBucketCursor = 0;

    // Ordered remote queue, drained once the editor exists.
    std::list<ApplyObj> m_applyQueue;
    std::unordered_map<std::string, std::list<ApplyObj>::iterator> m_queuedRemoteByGid;
    size_t m_snapshotReceived = 0;
    bool m_snapshotComplete = false;
    bool m_seeded = false;

    // Host snapshot seed, serialized in slices with one request in flight.
    bool m_seeding = false;
    size_t m_seedCursor = 0;
    size_t m_seedTotal = 0;
    size_t m_seedUploaded = 0;
    matjson::Value m_seedChunk = matjson::Value::array();
    size_t m_seedChunkBytes = 0;
    bool m_seedInflight = false;
    bool m_seedSerializeDone = false;
    uint64_t m_seedEpoch = 0;

    int m_reconcileTicks = 0;

    std::deque<ChatMessage> m_chat;
    uint64_t m_chatRevision = 0;

    bool m_joinerEditorOpened = false;
    geode::Ref<GJGameLevel> m_joinerLevel;
    geode::Ref<GJGameLevel> m_hostLevel;
    bool m_needsResyncOnEntry = false;

    bool m_recovering = false;
    int m_recoverAttempts = 0;
    std::chrono::steady_clock::time_point m_lastRecoverAt{};

    std::unordered_map<int, PeerSelection> m_peerSelections;
    matjson::Value m_pendingSelectionJson;
    bool m_selectionDirty = false;
    float m_sinceSelectionFlush = 0.f;

    std::unordered_map<int, PeerCamera> m_peerCameras;
    matjson::Value m_pendingCameraJson;
    bool m_cameraDirty = false;
    float m_sinceCameraFlush = 0.f;
    float m_lastCamX = 0.f;
    float m_lastCamY = 0.f;
    float m_lastCamZoom = 0.f;
    float m_lastCursorX = 0.f;
    float m_lastCursorY = 0.f;
    bool m_lastCursorVisible = false;

    std::unordered_map<int, PeerWorkZone> m_peerWorkZones;
    matjson::Value m_pendingWorkZoneJson;
    bool m_workZoneDirty = false;
    float m_sinceWorkZoneFlush = 0.f;
    float m_lastZoneX = 0.f, m_lastZoneY = 0.f, m_lastZoneW = 0.f, m_lastZoneH = 0.f;
    std::vector<PeerPing> m_peerPings;
    int m_followClientId = 0;
    struct HeatCell {
        int gx = 0;
        int gy = 0;
        float heat = 0.f;
    };
    std::vector<HeatCell> m_heatCells;

    std::unordered_map<int, int> m_layerOwners;

    std::string m_lastMetaSig;
    int m_metaReconcileTicks = 0;
    matjson::Value m_pendingLevelMeta;
    bool m_hasPendingLevelMeta = false;

    bool m_wasPlaytesting = false;
};

}
