#pragma once

#include "sources/ChatSource.hpp"

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace paimon::twitch {

struct PlatformRequestConfig {
    bool commandsEnabled = true;
    std::string commands;
    std::string queue = "General";
};

struct RequestRoute {
    std::string platform = "*";
    bool reward = false;
    std::string key;
    std::string name;
    std::string queue = "General";
};

struct RequestRoutingConfig {
    std::array<PlatformRequestConfig, kSelectableCount> platforms;
    bool pointsEnabled = false;
    std::vector<RequestRoute> routes;
};

std::string normalizeQueueName(std::string_view value);
std::string normalizeRewardID(std::string_view value);
bool normalizeRoute(RequestRoute& route);
RequestRoute const* findRequestRoute(RequestRoutingConfig const& config,
    Platform platform, std::string_view key, bool reward);
std::string routedCommands(RequestRoutingConfig const& config,
    Platform platform, std::string_view fallback);

} // namespace paimon::twitch
