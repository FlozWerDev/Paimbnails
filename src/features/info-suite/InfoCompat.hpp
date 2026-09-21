#pragma once

// Modules colliding with BetterInfo (cvolton.betterinfo) stay off while it is
// installed, unless the user forces them back on with `info-compat-force`.

#include <string_view>

namespace paimon::info::compat {

// Installed and not forced back on by the user.
bool cedingToBetterInfo();

// This module draws UI BetterInfo already draws.
bool overlapsBetterInfo(std::string_view key);

// Final verdict used by the gate.
bool isCeded(std::string_view key);

} // namespace paimon::info::compat
