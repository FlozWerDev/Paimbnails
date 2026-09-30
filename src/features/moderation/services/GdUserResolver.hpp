#pragma once

#include <Geode/Geode.hpp>
#include <string>

namespace paimon::moderation {

// callback always runs on the main thread;
// ok=false means the user wasn't found or the request failed.
void resolveUsername(
    std::string const& username,
    geode::CopyableFunction<void(bool ok, int accountID, std::string const& exactName)> cb
);

} // namespace paimon::moderation
