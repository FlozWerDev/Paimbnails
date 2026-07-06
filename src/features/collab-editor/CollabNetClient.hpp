#pragma once

#include "CollabTypes.hpp"

#include <cstdint>
#include <functional>
#include <matjson.hpp>
#include <string>

namespace paimon::collab {

// HTTP long-poll transport for the Render collab server.
//
// cocos2d's WebSocket header is not usable from Geode mods (it pulls in
// libwebsockets which is not shipped), so we use the same proven HTTP path the
// rest of the mod uses (web::WebRequest via WebHelper). A long-poll loop
// receives server messages; ops/perms are POSTed. All callbacks run on the
// main thread.
class CollabNetClient {
public:
    using MessageCb = std::function<void(matjson::Value const&)>;
    using StateCb = std::function<void(ConnState, std::string const&)>;

    void setCallbacks(MessageCb onMessage, StateCb onState);

    // baseUrl is the https base (e.g. https://host). Tries /api/join first;
    // if the room doesn't exist (404), automatically falls back to
    // /api/create-room to become the host of that code.
    void start(std::string baseUrl, std::string roomCode, std::string username, ConnectMode mode);
    void stop();

    // Re-runs the join/create handshake for the current room keeping the same
    // base/room/user. Used for session recovery: the host re-creates (and the
    // server lets it reclaim its room), a peer re-joins.
    void restart(ConnectMode mode);

    // Host-only: POST /api/close-room. The server destroys the room and
    // notifies every joiner with { t: "room_closed" }. Local state is also
    // torn down so any further ops are dropped.
    void closeRoom();

    bool isOpen() const; // joined and active

    // Ask the server to re-sync us: a host clears the room and re-seeds it, a
    // peer just gets the current snapshot re-sent. Used when re-entering the
    // editor with the room still open.
    void requestResync();

    // value.t is "op_batch" or "set_perms"; routed to the matching endpoint.
    void sendJson(matjson::Value const& value);

    // Host-only: POST /api/invite. cb(ok, online, message) reports whether the
    // invite was accepted by the server and whether the target was online.
    using InviteCb = std::function<void(bool ok, bool online, std::string const& message)>;
    void sendInvite(int accountId, std::string const& fromName, InviteCb cb);

private:
    void doJoin();
    void doCreate();
    void onJoinLikeSuccess(matjson::Value value); // shared by join/create success
    void poll();
    void scheduleRetry(uint64_t gen, int ms);
    void scheduleJoinRetry(uint64_t gen, int ms);
    void emitError(std::string const& code, std::string const& message);
    std::string apiUrl(std::string const& suffix) const;

    std::string m_base;
    std::string m_room;
    std::string m_user;
    int m_clientId = 0;
    bool m_joined = false;
    bool m_active = false;
    ConnectMode m_mode = ConnectMode::Join;
    uint64_t m_gen = 0;
    // Join 404s are retried a few times before failing: covers the host still
    // creating the room and the server waking from a cold start.
    int m_joinRetries = 0;

    MessageCb m_onMessage;
    StateCb m_onState;
};

} // namespace paimon::collab
