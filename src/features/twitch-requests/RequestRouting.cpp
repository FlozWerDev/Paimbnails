#include "RequestRouting.hpp"

#include "TwitchRequestParser.hpp"

#include <algorithm>
#include <cctype>

namespace paimon::twitch {

std::string normalizeQueueName(std::string_view value) {
    std::string name;
    bool space = false;
    for (unsigned char ch : value) {
        if (std::isspace(ch)) {
            space = !name.empty();
            continue;
        }
        if (ch < 32 || ch == 127 || ch == '<' || ch == '>') continue;
        if (space) name += ' ';
        space = false;
        name += static_cast<char>(ch);
    }
    if (name.size() > 48) {
        size_t cut = 48;
        while (cut > 0 && (static_cast<unsigned char>(name[cut]) & 0xC0) == 0x80) --cut;
        name.resize(cut);
    }
    while (!name.empty() && name.back() == ' ') name.pop_back();
    return name.empty() ? "General" : name;
}

std::string normalizeRewardID(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.remove_prefix(1);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.remove_suffix(1);
    if (value.size() != 36) return {};
    std::string id(value);
    for (size_t i = 0; i < id.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (id[i] != '-') return {};
        } else {
            auto ch = static_cast<unsigned char>(id[i]);
            if (!std::isxdigit(ch)) return {};
            id[i] = static_cast<char>(std::tolower(ch));
        }
    }
    return id;
}

bool normalizeRoute(RequestRoute& route) {
    route.queue = normalizeQueueName(route.queue);
    route.name = route.name.empty() ? "" : normalizeQueueName(route.name);
    if (route.reward) {
        route.platform = "twitch";
        route.key = normalizeRewardID(route.key);
        return !route.key.empty();
    }
    if (route.platform != "*" && route.platform != "twitch" && route.platform != "youtube"
        && route.platform != "kick" && route.platform != "tiktok") return false;
    while (!route.key.empty() && std::isspace(static_cast<unsigned char>(route.key.front()))) route.key.erase(0, 1);
    while (!route.key.empty() && std::isspace(static_cast<unsigned char>(route.key.back()))) route.key.pop_back();
    if (route.key.empty()) return false;
    auto commands = parseCommands(route.key);
    if (commands.size() != 1) return false;
    auto key = route.key;
    if (!key.starts_with('!')) key.insert(key.begin(), '!');
    std::ranges::transform(key, key.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (key != commands.front()) return false;
    route.key = commands.front();
    return true;
}

RequestRoute const* findRequestRoute(RequestRoutingConfig const& config,
    Platform platform, std::string_view key, bool reward) {
    RequestRoute const* shared = nullptr;
    for (auto const& route : config.routes) {
        if (route.reward != reward || route.key != key) continue;
        if (route.platform == platformKey(platform)) return &route;
        if (!reward && route.platform == "*") shared = &route;
    }
    return shared;
}

std::string routedCommands(RequestRoutingConfig const& config,
    Platform platform, std::string_view fallback) {
    auto const& local = config.platforms[static_cast<size_t>(platform)].commands;
    std::string commands(local.empty() ? fallback : std::string_view(local));
    if (commands.empty()) commands = "!req";
    for (auto const& route : config.routes) {
        if (route.reward || (route.platform != "*" && route.platform != platformKey(platform))) continue;
        if (!commands.empty()) commands += ',';
        commands += route.key;
    }
    return commands;
}

} // namespace paimon::twitch
