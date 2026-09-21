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

} // namespace paimon::emotes
