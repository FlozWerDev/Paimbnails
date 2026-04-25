#pragma once
#include <string>

struct ThumbnailInfo {
    std::string id;
    std::string url;
    std::string type; // "static", "gif", or "video"
    std::string format;
    std::string creator;
    std::string date;
    int position = 1;

    bool isVideo() const { return type == "video" || format == "mp4"; }
    bool isGif() const { return type == "gif" || format == "gif"; }
    bool isStatic() const { return !isVideo() && !isGif(); }
};
