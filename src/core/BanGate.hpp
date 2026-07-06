#pragma once

namespace paimon::ban {

// Run at the very start of bootstrap. Returns true when the local .paimon cache
// marks the user as banned, in which case the mod must NOT initialize.
// When no .paimon cache exists yet, this schedules a single async server check
// that writes the cache and, if banned, tears down the mod and shows the popup.
// No server request is made when the .paimon cache is present.
bool runStartupBanGate();

} // namespace paimon::ban
