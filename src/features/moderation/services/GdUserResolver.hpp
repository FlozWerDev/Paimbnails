#pragma once

#include <Geode/Geode.hpp>
#include <string>

namespace paimon::moderation {

// Resolves a username to its GD accountID by querying RobTop's servers
// (getGJUsers20.php). The callback always runs on the main thread.
// ok=false means the user wasn't found or the request failed.
void resolveUsername(
    std::string const& username,
    geode::CopyableFunction<void(bool ok, int accountID, std::string const& exactName)> cb
);

} // namespace paimon::moderation
