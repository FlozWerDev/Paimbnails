#pragma once

namespace paimon::ban {

// startup ban gate: true when the local .paimon cache marks the user banned
// (mod must not init). without cache, one async server check writes it.
bool runStartupBanGate();

} // namespace paimon::ban
