#pragma once

#include <Geode/Geode.hpp>
#include <string>
#include <vector>

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

// Typed results for async APIs (arc::Future).
struct ThumbnailGalleryResult {
    bool success = false;
    std::vector<ThumbnailInfo> thumbnails;
};

struct ThumbnailApiMessageResult {
    bool success = false;
    std::string message;
};

struct ThumbnailTextureResult {
    bool success = false;
    geode::Ref<cocos2d::CCTexture2D> texture;
};

struct ThumbnailDataResult {
    bool success = false;
    std::vector<uint8_t> data;
};

struct ThumbnailModeratorResult {
    bool isModerator = false;
    bool isAdmin = false;
};

struct ThumbnailRatingResult {
    bool success = false;
    float average = 0.f;
    int count = 0;
    int userVote = 0;
};
