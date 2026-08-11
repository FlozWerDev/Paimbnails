#include "TwitchRequestParser.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <climits>

namespace paimon::twitch {

namespace {

std::string lowerCopy(std::string_view text) {
    std::string result(text);
    std::ranges::transform(result, result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return result;
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

bool looksLikeUrl(std::string_view token) {
    auto scheme = lowerCopy(token.substr(0, std::min<size_t>(token.size(), 8)));
    return scheme.starts_with("http://") || scheme.starts_with("https://");
}

// Los links del chat suelen venir con puntuacion pegada al final.
std::string cleanUrl(std::string_view token) {
    constexpr std::string_view junk = ",.;:!?)]}>\"'";
    while (!token.empty() && junk.find(token.back()) != std::string_view::npos) {
        token.remove_suffix(1);
    }
    if (token.size() < 12 || token.size() > 300) return {};
    return std::string(token);
}

std::optional<int> firstNumber(std::string_view token) {
    size_t start = 0;
    while (start < token.size() && !std::isdigit(static_cast<unsigned char>(token[start]))) {
        ++start;
    }
    size_t end = start;
    while (end < token.size() && std::isdigit(static_cast<unsigned char>(token[end]))) {
        ++end;
    }
    if (start == end) return std::nullopt;

    long long id = 0;
    auto [stop, error] = std::from_chars(token.data() + start, token.data() + end, id);
    if (error != std::errc{} || stop != token.data() + end || id <= 0 || id > INT_MAX) {
        return std::nullopt;
    }
    return static_cast<int>(id);
}

template <class Fn>
void forEachToken(std::string_view text, Fn&& handle) {
    size_t start = 0;
    for (size_t i = 0; i <= text.size(); ++i) {
        bool const separator = i == text.size()
            || std::isspace(static_cast<unsigned char>(text[i]));
        if (!separator) continue;
        if (i > start) handle(text.substr(start, i - start));
        start = i + 1;
    }
}

} // namespace

std::vector<std::string> parseCommands(std::string_view configured) {
    std::vector<std::string> commands;
    size_t start = 0;

    auto add = [&commands](std::string_view token) {
        token = trim(token);
        if (token.empty()) return;

        std::string command = lowerCopy(token);
        if (command.front() != '!') command.insert(command.begin(), '!');
        if (std::ranges::find(commands, command) == commands.end()) {
            commands.push_back(std::move(command));
        }
    };

    for (size_t i = 0; i <= configured.size(); ++i) {
        bool separator = i == configured.size();
        if (!separator) {
            unsigned char ch = static_cast<unsigned char>(configured[i]);
            separator = configured[i] == ',' || configured[i] == ';' || std::isspace(ch);
        }
        if (!separator) continue;
        add(configured.substr(start, i - start));
        start = i + 1;
    }

    if (commands.empty()) commands.emplace_back("!req");
    return commands;
}

std::optional<ParsedRequest> parseRequest(
    std::string_view message,
    std::string_view configuredCommands
) {
    message = trim(message);
    std::string lower = lowerCopy(message);

    for (auto const& command : parseCommands(configuredCommands)) {
        if (!lower.starts_with(command)) continue;
        if (lower.size() > command.size()) {
            char delimiter = lower[command.size()];
            if (!std::isspace(static_cast<unsigned char>(delimiter)) && delimiter != ':') {
                continue;
            }
        }

        std::string_view rest = message.substr(command.size());
        ParsedRequest parsed;
        parsed.command = command;

        // Por tokens: asi un link con numeros dentro no se cuela como ID.
        forEachToken(rest, [&parsed](std::string_view token) {
            if (looksLikeUrl(token)) {
                if (parsed.url.empty()) parsed.url = cleanUrl(token);
                return;
            }
            if (parsed.levelID == 0) {
                if (auto id = firstNumber(token)) parsed.levelID = *id;
            }
        });

        if (parsed.levelID == 0) return std::nullopt;
        return parsed;
    }

    return std::nullopt;
}

bool isYouTubeUrl(std::string_view url) {
    auto lower = lowerCopy(url);
    std::string_view rest = lower;
    if (auto scheme = rest.find("://"); scheme != std::string_view::npos) {
        rest.remove_prefix(scheme + 3);
    }

    auto host = rest.substr(0, rest.find_first_of("/?#"));
    if (host.starts_with("www.")) host.remove_prefix(4);
    if (host.starts_with("m.")) host.remove_prefix(2);

    return host == "youtu.be"
        || host == "youtube.com"
        || host == "music.youtube.com"
        || host == "youtube-nocookie.com";
}

} // namespace paimon::twitch
