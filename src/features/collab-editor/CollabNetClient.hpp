#pragma once

#include "CollabTypes.hpp"

#include <cstdint>
#include <functional>
#include <matjson.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace paimon::collab {

// http long-poll (no websockets in geode); callbacks land on main thread.
class CollabNetClient {
public:
    using MessageCb = std::function<void(matjson::Value const&)>;
    using StateCb = std::function<void(ConnState, std::string const&)>;

    CollabNetClient() = default;
    ~CollabNetClient();
    CollabNetClient(CollabNetClient const&) = delete;
    CollabNetClient& operator=(CollabNetClient const&) = delete;
    CollabNetClient(CollabNetClient&&) = delete;
    CollabNetClient& operator=(CollabNetClient&&) = delete;

    void setCallbacks(MessageCb onMessage, StateCb onState);

    // baseurl is the http(s) origin; mode explicitly chooses join or create.
    void start(std::string baseUrl, std::string roomCode, std::string username,
               PeerAppearance appearance, ConnectMode mode);
    void stop();

    // re-run the handshake for the current room during session recovery.
    void restart(ConnectMode mode);

    // host-only close; the server notifies joiners and local ops are dropped.
    void closeRoom();

    bool isOpen() const;

    // re-send the current snapshot when re-entering an open room.
    void requestResync();

    // route value.t ("op_batch" or "set_perms") to its endpoint.
    void sendJson(matjson::Value const& value);

    // acked delivery; callers retain a chunk until cb reports ok=true.
    using OpsCb = std::function<void(bool ok, int status, int accepted)>;
    void sendOps(matjson::Value const& ops, OpsCb cb);

    // host-only snapshot seed; faster than streaming individual ops.
    using SeedCb = std::function<void(bool ok, int status, int accepted, int roomTotal)>;
    void sendSeed(matjson::Value const& objects, bool finalChunk, SeedCb cb);

    // host-only invite; the callback reports acceptance and target presence.
    using InviteCb = std::function<void(bool ok, bool online, std::string const& message)>;
    void sendInvite(int accountId, std::string const& fromName, InviteCb cb);

private:
    struct PendingStateRequest {
        std::string suffix;
        matjson::Value body;
    };

    void beginStart(std::string baseUrl, std::string roomCode, std::string username,
                    PeerAppearance appearance, ConnectMode mode, bool preserveResumeToken);
    void stopInternal(bool notifyServer);
    void doJoin();
    void doCreate();
    void onJoinLikeSuccess(matjson::Value value);
    void poll();
    enum class RetryAction : std::uint8_t { Join, Poll };
    void scheduleDelayed(uint64_t gen, int ms, RetryAction action);
    void scheduleRetry(uint64_t gen, int ms);
    void scheduleJoinRetry(uint64_t gen, int ms);
    void dispatchStateJson(std::string channel, std::string suffix, matjson::Value body);
    void emitError(std::string const& code, std::string const& message);
    std::string apiUrl(std::string const& suffix) const;

    std::string m_base;
    std::string m_room;
    std::string m_user;
    std::string m_sessionToken;
    std::string m_resumeToken;
    PeerAppearance m_appearance;
    int m_clientId = 0;
    bool m_joined = false;
    bool m_active = false;
    bool m_resyncInFlight = false;
    ConnectMode m_mode = ConnectMode::Join;
    uint64_t m_gen = 0;
    int m_pollFailures = 0;
    // retry join 404s while the host or server is still starting.
    int m_joinRetries = 0;
    int m_hostReconnectRetries = 0;
    std::unordered_set<std::string> m_stateRequestsInFlight;
    std::unordered_map<std::string, PendingStateRequest> m_pendingStateRequests;
    // invalidated in the destructor so delayed callbacks cannot touch `this`.
    std::shared_ptr<uint8_t> m_lifetime = std::make_shared<uint8_t>(0);

    MessageCb m_onMessage;
    StateCb m_onState;
};

}
