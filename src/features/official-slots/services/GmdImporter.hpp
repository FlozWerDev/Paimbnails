#pragma once

// Minimal .gmd reader.
//
// A .gmd is an Apple plist holding one level's keys, the same ones GJGameLevel
// serializes. We only need the display fields (name, author, description) to
// prefill the slot form, so this parses the handful of keys it cares about
// instead of pulling in a full plist dependency. The level string itself is
// left untouched on disk: the slot is cosmetic and never has to build geometry.

#include <filesystem>
#include <optional>
#include <string>

namespace paimon::officialslots {

struct GmdInfo {
    std::string name;
    std::string author;
    std::string description;
    int songId = 0;
    int originalLevelId = 0;
};

// Reads the display fields of a .gmd. Returns nullopt when the file cannot be
// read or does not look like a plist at all.
std::optional<GmdInfo> readGmdInfo(std::filesystem::path const& path);

} // namespace paimon::officialslots
