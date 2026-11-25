#pragma once

#include <cstdint>
#include <string_view>

namespace paimon::ban {

// applies known bans immediately and starts the session monitor. online access
// stays locked until a live server check succeeds for the current account.
bool runStartupBanGate();

// dedicated ban flag (unlike the one-way runtime shutdown latch): set once enforceBan runs,
// lets bootstrap() refuse re-init while the game keeps running.
bool isBanned();

// zero denies the request. a nonzero permit must also match at completion so
// in-flight responses cannot survive a ban, failed check, or account change.
// only get requests to the fixed https verification endpoint bypass the lock.
std::uint64_t authorizeWebRequest(std::string_view method, std::string_view url);
std::uint64_t networkAuthorization();

} // namespace paimon::ban
