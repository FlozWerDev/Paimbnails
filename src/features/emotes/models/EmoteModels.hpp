#pragma once

#include <string>
#include <vector>

namespace paimon::emotes {

enum class EmoteType {
    Static,
    Gif
};

struct EmoteInfo {
    std::string name;
    std::string filename;
    EmoteType type = EmoteType::Static;
    std::string category;
    int size = 0;
    std::string url;
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
