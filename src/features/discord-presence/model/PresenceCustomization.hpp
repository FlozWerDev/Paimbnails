#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace paimon::discord {

inline constexpr char kDefaultApplicationID[] = "1503220958910218260";

struct BooleanOption { char const* key; bool value; };
struct TextOption { char const* key; char const* value; };
inline constexpr std::array kBooleanOptions{
    BooleanOption{"discord-rpc-private-mode", false},
    BooleanOption{"discord-rpc-idle-when-unfocused", true},
    BooleanOption{"discord-rpc-show-progress", true},
    BooleanOption{"discord-rpc-include-paimbnails-features", true},
    BooleanOption{"discord-rpc-show-timestamp", true},
    BooleanOption{"discord-rpc-override-details", false},
    BooleanOption{"discord-rpc-override-state", false},
    BooleanOption{"discord-rpc-show-level-name", true},
    BooleanOption{"discord-rpc-show-creator", true},
    BooleanOption{"discord-rpc-show-level-id", false},
    BooleanOption{"discord-rpc-show-attempts", false},
    BooleanOption{"discord-rpc-show-best", true},
    BooleanOption{"discord-rpc-show-difficulty", true},
    BooleanOption{"discord-rpc-show-editor-stats", false},
    BooleanOption{"discord-rpc-show-profile-names", true},
    BooleanOption{"discord-rpc-show-music", true},
    BooleanOption{"discord-rpc-show-room-names", false},
    BooleanOption{"discord-rpc-show-party-size", false},
    BooleanOption{"discord-rpc-share-private-levels", false},
    BooleanOption{"discord-rpc-show-large-image", true},
    BooleanOption{"discord-rpc-show-small-image", true},
};
inline constexpr std::array kTextOptions{
    TextOption{"discord-rpc-large-text", ""},
    TextOption{"discord-rpc-large-image-key", ""},
    TextOption{"discord-rpc-small-image-key", ""},
    TextOption{"discord-rpc-small-text", ""},
    TextOption{"discord-rpc-activity-type", "Playing"},
    TextOption{"discord-rpc-custom-details", ""},
    TextOption{"discord-rpc-custom-state", ""},
    TextOption{"discord-rpc-application-id", ""},
    TextOption{"discord-rpc-timer-mode", "Session"},
    TextOption{"discord-rpc-refresh-mode", "Balanced"},
    TextOption{"discord-rpc-sharing-scope", "Everywhere"},
    TextOption{"discord-rpc-gameplay-details", ""},
    TextOption{"discord-rpc-gameplay-state", ""},
    TextOption{"discord-rpc-editor-details", ""},
    TextOption{"discord-rpc-editor-state", ""},
    TextOption{"discord-rpc-menu-details", ""},
    TextOption{"discord-rpc-menu-state", ""},
};

// shared by migration, reset, preview and publishing. All text is value-owned.
struct PresenceConfiguration {
    std::array<bool, kBooleanOptions.size()> flags{};
    std::array<std::string, kTextOptions.size()> texts;
    PresenceConfiguration() {
        for (std::size_t i = 0; i < flags.size(); ++i) flags[i] = kBooleanOptions[i].value;
        for (std::size_t i = 0; i < texts.size(); ++i) texts[i] = kTextOptions[i].value;
    }
    bool flag(std::string_view key) const {
        for (std::size_t i = 0; i < flags.size(); ++i) if (key == kBooleanOptions[i].key) return flags[i];
        return false;
    }
    std::string text(std::string_view key) const {
        for (std::size_t i = 0; i < texts.size(); ++i) if (key == kTextOptions[i].key) return texts[i];
        return {};
    }
};

inline bool validChoice(std::string_view key, std::string_view value) {
    auto oneOf = [&](std::initializer_list<std::string_view> choices) {
        return std::find(choices.begin(), choices.end(), value) != choices.end();
    };
    if (key == "discord-rpc-activity-type") return oneOf({"Playing", "Listening", "Watching", "Competing"});
    if (key == "discord-rpc-refresh-mode") return oneOf({"Fast", "Balanced", "Low power"});
    if (key == "discord-rpc-timer-mode") return oneOf({"Session", "Activity", "Level", "Attempt", "Off"});
    if (key == "discord-rpc-sharing-scope") return oneOf({"Everywhere", "Gameplay only", "Gameplay & editor", "Menus only"});
    return true;
}

using PresenceTokens = std::vector<std::pair<std::string, std::string>>;
inline constexpr std::array<std::string_view, 16> kPresenceTokens{
    "activity", "screen", "level", "creator", "level_id", "mode", "progress", "best",
    "attempts", "objects", "song", "artist", "profile", "room", "players", "difficulty"
};

inline std::string cleanText(std::string_view value, std::size_t limit = 128) {
    std::string result;
    result.reserve(std::min(value.size(), limit));
    bool space = false;
    for (std::size_t i = 0; i < value.size();) {
        auto byte = static_cast<unsigned char>(value[i]);
        if (byte <= 0x20 || byte == 0x7f) { space = !result.empty(); ++i; continue; }
        std::size_t length = byte < 0x80 ? 1 : byte >= 0xc2 && byte <= 0xdf ? 2
            : byte >= 0xe0 && byte <= 0xef ? 3 : byte >= 0xf0 && byte <= 0xf4 ? 4 : 0;
        bool valid = length && i + length <= value.size();
        for (std::size_t j = 1; valid && j < length; ++j) {
            auto next = static_cast<unsigned char>(value[i + j]);
            valid = (next & 0xc0) == 0x80;
        }
        if (valid && length >= 3) {
            auto second = static_cast<unsigned char>(value[i + 1]);
            valid = !(byte == 0xe0 && second < 0xa0) && !(byte == 0xed && second >= 0xa0)
                && !(byte == 0xf0 && second < 0x90) && !(byte == 0xf4 && second >= 0x90);
        }
        if (!valid) { ++i; continue; }
        if (result.size() + length + (space ? 1 : 0) > limit) break;
        if (space) result += ' ';
        space = false;
        result.append(value.substr(i, length));
        i += length;
    }
    return result;
}

inline std::string trimText(std::string value) {
    auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

inline bool validLink(std::string_view value) {
    auto starts = [&](std::string_view prefix) {
        return value.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), value.begin(),
            [](char a, unsigned char b) { return a == std::tolower(b); });
    };
    std::size_t start = starts("https://") ? 8 : starts("http://") ? 7 : 0;
    if (!start || value.size() > 256 || value.size() <= start) return false;
    auto hostEnd = value.find_first_of("/?#", start);
    auto host = value.substr(start, hostEnd == std::string_view::npos ? value.size() - start : hostEnd - start);
    if (host.empty() || host.find('@') != std::string_view::npos || host.front() == '.' || host.front() == ':') return false;
    return std::none_of(value.begin(), value.end(), [](unsigned char c) { return c <= 0x20 || c == 0x7f || c == '\\'; });
}

inline bool validApplicationID(std::string_view value) {
    return value.size() >= 17 && value.size() <= 20 &&
        std::all_of(value.begin(), value.end(), [](unsigned char c) { return c >= '0' && c <= '9'; });
}

inline std::string imageReference(std::string value) {
    value = trimText(std::move(value));
    if (validLink(value)) return value;
    if (value.starts_with("mp:") && value.size() > 3 && value.size() <= 256 &&
        value.find_first_of(" \t\r\n\\") == std::string::npos) return value;
    if (value.empty() || value.size() > 32) return {};
    for (auto& c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return {};
    }
    return value;
}

inline std::string expandTemplate(std::string_view text, PresenceTokens const& tokens) {
    std::string result;
    for (std::size_t i = 0; i < text.size() && result.size() < 1024;) {
        if (text.substr(i, 2) == "{{") { result += '{'; i += 2; continue; }
        if (text[i] == '{') {
            auto end = text.find('}', i + 1);
            if (end != std::string_view::npos) {
                auto key = text.substr(i + 1, end - i - 1);
                auto found = std::find_if(tokens.begin(), tokens.end(), [&](auto const& token) { return token.first == key; });
                bool known = std::find(kPresenceTokens.begin(), kPresenceTokens.end(), key) != kPresenceTokens.end();
                if (found != tokens.end()) result += found->second;
                else if (!known) result.append(text.substr(i, end - i + 1));
                i = end + 1;
                continue;
            }
        }
        result += text[i++];
    }
    return cleanText(result);
}

} // namespace paimon::discord
