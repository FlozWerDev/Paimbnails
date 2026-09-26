#pragma once

#include <string>

namespace cocos2d { class CCTexture2D; }

namespace paimon {

struct AudioOwnerChangedEvent {
    std::string previous;  // "none", "menu", "dynamic", "profile", "preview"
    std::string current;
    int sessionToken = 0;
};

struct ThumbnailBackgroundChangedEvent {
    int levelID = 0;
    geode::Ref<cocos2d::CCTexture2D> texture = nullptr;

// Read by InfoLayer on open, no wait for next cycle.
// Raw pointer with manual retain: a static with destructor would break at exit.
    static inline int s_lastLevelID = 0;
    static inline cocos2d::CCTexture2D* s_lastTextureRaw = nullptr;

    // Main thread only.
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
