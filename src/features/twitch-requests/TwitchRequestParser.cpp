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

// chat links often carry trailing punctuation.
std::string cleanUrl(std::string_view token) {
    constexpr std::string_view junk = ",.;:!?)]}>\"'";
    while (!token.empty() && junk.find(token.back()) != std::string_view::npos) {
        token.remove_suffix(1);
    }
    if (!isValidVideoUrl(token)) return {};
    return std::string(token);
}

std::optional<int> levelNumber(std::string_view token) {
    if (lowerCopy(token).starts_with("id:") || lowerCopy(token).starts_with("id=")) {
        token.remove_prefix(3);
    }
    while (!token.empty() && (token.ends_with(':') || token.ends_with(',') || token.ends_with(';'))) token.remove_suffix(1);
    if (token.size() >= 2 && ((token.front() == '<' && token.back() == '>')
        || (token.front() == '[' && token.back() == ']') || (token.front() == '(' && token.back() == ')'))) {
        token = token.substr(1, token.size() - 2);
    }
    if (token.starts_with('#')) token.remove_prefix(1);
    if (token.empty() || !std::ranges::all_of(token, [](unsigned char ch) {
        return ch >= '0' && ch <= '9';
    })) return std::nullopt;

    long long id = 0;
    auto [stop, error] = std::from_chars(token.data(), token.data() + token.size(), id);
    if (error != std::errc{} || stop != token.data() + token.size() || id <= 0 || id > INT_MAX) {
        return std::nullopt;
    }
    return static_cast<int>(id);
}

std::string_view takeToken(std::string_view& text) {
    text = trim(text);
    size_t end = 0;
    while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end]))) ++end;
    auto token = text.substr(0, end);
    text.remove_prefix(end);
    return token;
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
        if (command.size() < 2 || command.size() > 40 || commands.size() >= 256) return;
        if (!std::ranges::all_of(std::string_view(command).substr(1), [](unsigned char ch) {
            return std::isalnum(ch) || ch == '_' || ch == '-' || ch == '+';
        })) return;
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
    if (message.size() > 4096) return std::nullopt;
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

        auto rest = message.substr(command.size());
        if (rest.starts_with(':')) rest.remove_prefix(1);
        auto parsed = parseRequestBody(rest);
        if (parsed) parsed->command = command;
        return parsed;
    }

    return std::nullopt;
}

std::optional<ParsedRequest> parseRequestBody(std::string_view body) {
    if (body.size() > 4096) return std::nullopt;
    ParsedRequest parsed;
    auto token = takeToken(body);
    if (looksLikeUrl(token)) {
        parsed.url = cleanUrl(token);
        if (parsed.url.empty()) return std::nullopt;
        token = takeToken(body);
    }
    auto label = lowerCopy(token);
    if (label == "id" || label == "id:" || label == "id=") token = takeToken(body);
    auto id = levelNumber(token);
    if (!id) return std::nullopt;
    parsed.levelID = *id;

    body = trim(body);
    size_t start = 0;
    while (start < body.size()) {
        size_t end = start;
        while (end < body.size() && !std::isspace(static_cast<unsigned char>(body[end]))) ++end;
        auto url = cleanUrl(body.substr(start, end - start));
        if (parsed.url.empty() && !url.empty()) {
            parsed.url = std::move(url);
            parsed.description = std::string(trim(body.substr(0, start)));
            auto tail = trim(body.substr(end));
            if (!tail.empty()) {
                if (!parsed.description.empty()) parsed.description += ' ';
                parsed.description += tail;
            }
            return parsed;
        }
        start = end;
        while (start < body.size() && std::isspace(static_cast<unsigned char>(body[start]))) ++start;
    }
    parsed.description = std::string(body);
    return parsed;
}

bool isValidVideoUrl(std::string_view url) {
    url = trim(url);
    if (url.size() < 12 || url.size() > 300 || !looksLikeUrl(url)) return false;
    if (std::ranges::any_of(url, [](unsigned char ch) { return std::isspace(ch); })) {
        return false;
    }

    auto const scheme = url.find("://");
    if (scheme == std::string_view::npos) return false;
    auto const hostStart = scheme + 3;
    auto const hostEnd = url.find_first_of("/?#", hostStart);
    return hostStart < (hostEnd == std::string_view::npos ? url.size() : hostEnd);
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
