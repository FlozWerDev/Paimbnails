#pragma once

#include "CollabNetClient.hpp"
#include "CollabTypes.hpp"

#include <Geode/Geode.hpp>
#include <chrono>
#include <cstdint>
#include <deque>
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

    // Connect to the Render server and join a room. roomCode identifies the
    // shared level; username is the display name shown to peers. Falls back to
    // create-room automatically when the room doesn't exist (that client
    // becomes host).
    void connect(std::string const& roomCode, std::string const& username, ConnectMode mode, GJGameLevel* hostLevel = nullptr);
    void disconnect();
    // Host-only: end the session for everyone. Peers receive room_closed.
    void closeRoom();

    ConnState state() const;
    bool connected() const;
    bool isHost() const;
    bool isApplyingRemote() const;
    HostPermissions permissions() const;
    std::string status() const;
    std::string roomCode() const;
    int peerCount() const;
    int clientId() const { return m_clientId; }
    // Peers currently in the room (including us), with display names.
    std::vector<PeerInfo> peers() const;
    std::string peerName(int clientId) const;

    void setHostPermissions(HostPermissions permissions);
    void setEditor(LevelEditorLayer* editor);
    void clearEditor(LevelEditorLayer* editor);
    LevelEditorLayer* editor() const { return m_editor; }
    // Level backing the active room while we host it (for UI indicators like
    // the "live" dot on its level cell). Null when not hosting.
    GJGameLevel* hostLevel() const { return m_hostLevel.data(); }

    // Editor overlay (attribution tags + chat feed). Non-owning; the overlay
    // registers itself while it lives inside the editor scene and unregisters
    // on destruction so the manager never calls into a freed overlay.
    void setOverlay(CollabEditorOverlay* overlay);
    void clearOverlay(CollabEditorOverlay* overlay);

    // Editor hooks feed local edits here.
    void sendCreatedObject(GameObject* object);
    void sendCreatedObjects(cocos2d::CCArray* objects);
    void sendUpdatedObject(GameObject* object);
    void sendUpdatedObjects(cocos2d::CCArray* objects);
    void sendDeletedObject(GameObject* object, std::string const& beforeSave = {});
    // Catch-all for edits made through popups/undo: re-send any object whose
    // save string changed since we last sent it. Objects may be nullptrs.
    void reconcileObjects(cocos2d::CCArray* objects);

    // Host-only: invite a GD account to the current room. cb reports the
    // outcome (delivered / offline / error) so the UI can give feedback.
    using InviteCb = std::function<void(bool ok, bool online, std::string const& message)>;
    void inviteUser(int accountId, std::string const& targetName, InviteCb cb = {});

    // Chat.
    void sendChat(std::string const& text);
    std::vector<ChatMessage> recentChat(size_t maxCount = 40) const;
    uint64_t chatRevision() const { return m_chatRevision; }

    // Voice relay (frames produced by CollabVoice).
    void sendVoiceFrame(uint32_t seq, std::string const& b64);

    // Called every editor frame: flushes outgoing ops and applies remote ones.
    void tick();

    bool clientCanOpenSong() const;
    bool clientCanOpenOptions() const;
    bool clientCanOpenLevelSettings() const;

private:
    struct OutOp {
        std::string kind; // add | update | delete
        std::string gid;
        uint32_t version = 0;
        std::string save;
    };

    struct ApplyObj {
        std::string kind = "add"; // add | update | delete
        std::string gid;
        std::string save;
        uint32_t version = 0;
        int origin = 0;      // clientId that made the edit (0 = snapshot)
        std::string by;      // username of the editor (may be empty)
    };

    CollabManager();
    ~CollabManager();

    void setStatus(std::string message);
    void onState(ConnState state, std::string const& message);
    void onMessage(matjson::Value const& msg);
    void handleMessage(matjson::Value const& msg);
    void handlePeerList(matjson::Value const& peersJson);
    void pushChatMessage(ChatMessage msg);

    // Tears the session down locally and shows a notice. Pops the joiner's
    // editor scene first so the alert lands on the destination scene. Used for
    // both room_closed and a lost session (server restart / room purge).
    void teardownAndNotify(std::string const& text);
    // Tries to transparently recover a session the server dropped (restart /
    // timeout): the host re-creates its room with the same code (the server
    // lets it reclaim it), a peer re-joins and rebuilds from a fresh snapshot.
    // Rate limited; returns false when the caller should tear down instead.
    bool tryRecoverSession();
    // Deletes the temporary level auto-created for a joiner (the canvas is the
    // host's level; the joiner copy is just a preview).
    void discardJoinerLevel();

    void enqueueOp(std::string kind, std::string const& gid, uint32_t version, std::string save);
    void flushOutgoing();

    // Non-hosts are dropped straight into a fresh editor on join so the host's
    // snapshot/ops populate a clean level (instead of merging onto whatever the
    // user happened to have open).
    void openJoinerEditor();
    // Drops the host into their own level's editor when they created the room
    // from outside the editor (e.g. from the level info screen).
    void openHostEditor();

    // Editor object state (gid maps, versions, pending/apply queues) is tied to
    // a specific editor scene. Cleared when leaving the editor so re-entering a
    // still-open room starts fresh, and rebuilt via a server resync.
    void resetEditorState();
    void wipeEditorObjects();
    void beginResync();

    size_t seedFromEditor();
    void applyRemoteAdd(ApplyObj const& op);
    void applyRemoteUpdate(ApplyObj const& op);
    void applyRemoteDelete(ApplyObj const& op);
    void notifyOverlayEdit(ApplyObj const& op, GameObject* object);

    GameObject* findTrackedObject(std::string const& gid) const;
    std::string saveObject(GameObject* object) const;
    void mapGid(std::string const& gid, GameObject* object);
    void unmapGid(std::string const& gid);
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

    // gid <-> local object mapping. m_uniqueID is per-session (not global), so
    // objects are addressed by gid on the wire. The Ref keeps remote-applied
    // lookups O(1) instead of scanning m_objects.
    std::unordered_map<int, std::string> m_uidToGid;
    std::unordered_map<std::string, geode::Ref<GameObject>> m_gidToObj;
    std::unordered_map<std::string, uint32_t> m_versionByGid;
    // Last save string we sent per gid: identical re-sends are skipped, which
    // keeps drags/reconciles from spamming no-op updates at the server.
    std::unordered_map<std::string, std::string> m_lastSentSave;
    uint64_t m_localSeq = 1;

    // Outgoing batch (coalesces repeated updates per gid).
    std::vector<OutOp> m_pendingOps;
    std::unordered_map<std::string, size_t> m_pendingIndexByGid;
    // Update-only batches are throttled to ~5 flushes/s; adds and deletes
    // flush on the next tick so placements feel instant.
    float m_sinceFlush = 0.f;
    bool m_pendingStructural = false;

    // Remote operations to apply (snapshot + op_batch), applied in arrival
    // order and budgeted per tick. Only drained once the editor exists, so
    // connecting before entering the editor never drops objects.
    std::deque<ApplyObj> m_applyQueue;
    size_t m_snapshotReceived = 0;
    bool m_snapshotComplete = false;
    bool m_seeded = false;

    // Seeding window: when we host an empty room we upload the level already
    // open in the editor. Objects can stream in over several frames, so we keep
    // seeding (skipping ones already tracked) until none appear for a while.
    bool m_seeding = false;
    int m_seedIdleTicks = 0;
    int m_seedTotalTicks = 0;

    // Periodic selected-object reconcile (catches popup edits, undo, etc.).
    int m_reconcileTicks = 0;

    // Chat log (newest last), capped.
    std::deque<ChatMessage> m_chat;
    uint64_t m_chatRevision = 0;

    bool m_joinerEditorOpened = false;
    geode::Ref<GJGameLevel> m_joinerLevel;
    // Level the host wants to collab on, captured at connect time so we can
    // open its editor once we know we're the host.
    geode::Ref<GJGameLevel> m_hostLevel;
    // Set when we leave the editor with the room still open, so the next editor
    // entry triggers a resync instead of a fresh seed/snapshot.
    bool m_needsResyncOnEntry = false;

    // Session recovery bookkeeping (see tryRecoverSession).
    bool m_recovering = false;
    int m_recoverAttempts = 0;
    std::chrono::steady_clock::time_point m_lastRecoverAt{};
};

} // namespace paimon::collab
