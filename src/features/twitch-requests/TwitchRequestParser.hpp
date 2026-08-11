#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace paimon::twitch {

struct ParsedRequest {
    int levelID = 0;
    std::string command;
    std::string url;  // video que venia en el mensaje, si habia alguno
};

std::vector<std::string> parseCommands(std::string_view configured);
std::optional<ParsedRequest> parseRequest(
    std::string_view message,
    std::string_view configuredCommands
);

bool isYouTubeUrl(std::string_view url);

} // namespace paimon::twitch
