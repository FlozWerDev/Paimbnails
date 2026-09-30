#pragma once

// modules colliding with betterinfo (cvolton.betterinfo) stay off while it is
// installed, unless the user forces them back on with `info-compat-force`.

#include <string_view>

namespace paimon::info::compat {

// installed and not forced back on by the user.
bool cedingToBetterInfo();

// this module draws ui betterinfo already draws.
bool overlapsBetterInfo(std::string_view key);

// final verdict used by the gate.
bool isCeded(std::string_view key);

} // namespace paimon::info::compat
