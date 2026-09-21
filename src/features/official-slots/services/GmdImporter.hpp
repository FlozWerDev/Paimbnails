#pragma once

// Minimal .gmd reader: parses the handful of plist keys it cares about
// instead of pulling in a full plist dependency.

#include <filesystem>
#include <optional>
#include <string>

namespace paimon::officialslots {

struct GmdInfo {
    std::string name;
    std::string author;
    int songId = 0;
};

// Reads the display fields of a .gmd. Returns nullopt when the file cannot be
// read or does not look like a plist at all.
std::optional<GmdInfo> readGmdInfo(std::filesystem::path const& path);

// Level string (k4) as the game keeps it in GJGameLevel::m_levelString.
// Empty when the file has none: a cosmetic-only slot.
std::string readGmdLevelString(std::filesystem::path const& path);

} // namespace paimon::officialslots
