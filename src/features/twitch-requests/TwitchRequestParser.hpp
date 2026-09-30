#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace paimon::twitch {

struct ParsedRequest {
    int levelID = 0;
    std::string command;
    std::string url;
    std::string description;
};

std::vector<std::string> parseCommands(std::string_view configured);
std::optional<ParsedRequest> parseRequest(
    std::string_view message,
    std::string_view configuredCommands
);
std::optional<ParsedRequest> parseRequestBody(std::string_view body);

// a video field only counts when it is a bounded http(s) url without spaces.
// the host is intentionally unrestricted so links from any video provider work.
bool isValidVideoUrl(std::string_view url);
bool isYouTubeUrl(std::string_view url);

} // namespace paimon::twitch
