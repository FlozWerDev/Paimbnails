#pragma once

#include <string>
#include <vector>

namespace paimon {

enum class PermissionTier : int {
    Viewer      = 0,   // read-only (view thumbs, emotes)
    User        = 1,   // basic interaction (download, search)
    Contributor = 2,   // content upload
    Moderator   = 3,   // moderation (approve/reject, ban)
    Admin       = 4    // full access (server config, force actions)
};

struct FeatureSpec {
    std::string name;                       // e.g. "thumbnails", "emotes"
    std::string version;                    // e.g. "2.3.5"
    std::vector<std::string> dependencies;  // required features
    PermissionTier requiredTier = PermissionTier::Viewer;
    bool enabledByDefault = true;
};

} // namespace paimon
