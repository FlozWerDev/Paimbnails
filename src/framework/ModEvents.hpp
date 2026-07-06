#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace cocos2d { class CCTexture2D; }

namespace paimon {

struct ThumbnailLoadedEvent {
    int levelID = 0;
    std::string source;   // "network", "disk-cache", "ram-cache"
    bool isGif = false;
};

struct CacheEvictedEvent {
    std::string key;
    std::string reason;   // "lru", "ttl", "manual"
    size_t freedBytes = 0;
};

struct UploadCompletedEvent {
    int levelID = 0;
    std::string format;   // "png", "gif", "mp4"
    std::string username;
    bool success = false;
    std::string message;
};

struct FeatureToggledEvent {
    std::string featureName;
    bool enabled = false;
};

struct PermissionDeniedEvent {
    std::string featureName;
    std::string action;
    std::string reason;
};

struct UploadStartedEvent {
    int levelID = 0;
    std::string format;
    std::string username;
    size_t dataSize = 0;
};

struct AudioOwnerChangedEvent {
    std::string previous;  // "none", "menu", "dynamic", "profile", "preview"
    std::string current;
    int sessionToken = 0;
};

// Emitted by LevelInfoLayer when the thumbnail background changes.
// CustomSongWidget and InfoLayer subscribe to sync their backgrounds.
struct ThumbnailBackgroundChangedEvent {
    int levelID = 0;
    geode::Ref<cocos2d::CCTexture2D> texture = nullptr;

    // Cache of the last active thumbnail per level so InfoLayer can read the
    // current texture on open without waiting for the next cycle.
    //
    // We don't use geode::Ref<> here: a static with a non-trivial destructor
    // would call release() on invalid memory at atexit (CCPoolManager gone) ->
    // crash. Instead use a raw pointer with manual retain; RuntimeLifecycle.cpp
    // ($on_game(Exiting)) calls setLastTexture(nullptr) to free it. If Exiting
    // never fires (kill -9, crash on another thread), the OS reclaims the handle
    // at process exit, which beats an atexit crash.
    static inline int s_lastLevelID = 0;
    static inline cocos2d::CCTexture2D* s_lastTextureRaw = nullptr;

    // Thread-safe helpers to mutate the cache (call from the main thread).
    static void setLastTexture(cocos2d::CCTexture2D* tex) {
        if (s_lastTextureRaw == tex) return;
        if (tex) tex->retain();
        if (s_lastTextureRaw) s_lastTextureRaw->release();
        s_lastTextureRaw = tex;
    }

    static cocos2d::CCTexture2D* getLastTexture() {
        return s_lastTextureRaw;
    }
};

} // namespace paimon
