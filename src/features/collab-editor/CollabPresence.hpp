#pragma once

#include <cstdint>
#include <string>

namespace paimon::collab {

// Account-keyed presence with the collab server, so a host can invite friends
// who are online (mod running, logged in) but not currently in a room. Runs a
// lightweight long-poll that surfaces incoming invites as a prompt popup.
//
// This mirrors Globed's "always reachable" model but only while the mod is
// loaded and the user is signed in. Gated behind the collab-invites setting.
class CollabPresence {
public:
    static CollabPresence& get();

    // Idempotent. Registers the local GD account and starts the invite poll.
    void start();
    void stop();

private:
    void registerSelf();
    void poll();
    void scheduleRetry(uint64_t gen, int ms);
    void handleInvite(std::string const& room, std::string const& fromName);

    bool m_started = false;
    int m_accountId = 0;
    uint64_t m_gen = 0;
};

} // namespace paimon::collab
