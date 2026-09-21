#pragma once

namespace paimon::ban {

// Startup ban gate: true when the local .paimon cache marks the user banned
// (mod must NOT init). Without cache, one async server check writes it.
bool runStartupBanGate();

} // namespace paimon::ban
