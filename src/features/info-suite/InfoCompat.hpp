#pragma once

// BetterInfo (cvolton.betterinfo) covers part of the same ground as the Info
// Suite. Rather than drawing two ids on every cell and two buttons on every
// level screen, the modules that would collide report themselves as ceded and
// stay off while BetterInfo is installed. The user can override that with the
// `info-compat-force` setting.

#include <string_view>
#include <vector>

namespace paimon::info::compat {

// BetterInfo is installed and the user has not forced our modules back on.
bool cedingToBetterInfo();

// This module draws UI BetterInfo already draws.
bool overlapsBetterInfo(std::string_view key);

// Final verdict used by the gate: the module has to stay off right now.
bool isCeded(std::string_view key);

// Setting keys of the overlapping modules, for the warning banner.
std::vector<std::string_view> const& overlappingKeys();

// How many overlapping modules are currently switched on but ceded.
int cededModuleCount();

} // namespace paimon::info::compat
