#pragma once

#include <cstdint>
#include <atomic>
#include <memory>
#include <string>

namespace paimon::collab {

// Account-keyed presence so hosts can invite online friends who aren't in a
// room. Globed-style reachability, only while signed in.
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
    std::string m_token;
    uint64_t m_gen = 0;
    std::shared_ptr<std::atomic<bool>> m_lifetime = std::make_shared<std::atomic<bool>>(true);
};

} // namespace paimon::collab
