#pragma once

#include <string>
#include <vector>

namespace paimon::emotes {

enum class EmoteType {
    Static, // png, webp
    Gif
};

struct EmoteInfo {
    std::string name;     // unique emote identifier (e.g. "paimon_happy")
    std::string filename; // server filename (e.g. "paimon_happy.gif")
    EmoteType type = EmoteType::Static;
    std::string category; // server category (e.g. "reaction")
    int size = 0;         // bytes
    std::string url;      // full download URL
};

struct EmotePage {
    std::vector<EmoteInfo> emotes;
    int page = 1;
    int limit = 20;
    int total = 0;
    int totalPages = 0;
    bool hasNext = false;
    bool hasPrev = false;
};

} // namespace paimon::emotes
