#pragma once

// minimal .gmd reader: parses the handful of plist keys it cares about
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

// reads the display fields of a .gmd. returns nullopt when the file cannot be
// read or does not look like a plist at all.
std::optional<GmdInfo> readGmdInfo(std::filesystem::path const& path);

struct GmdData {
    std::optional<GmdInfo> info;
    std::string levelString;
};

GmdData readGmd(std::filesystem::path const& path);

} // namespace paimon::officialslots
