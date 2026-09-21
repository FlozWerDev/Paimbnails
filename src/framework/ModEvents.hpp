#pragma once

#include <string>

namespace cocos2d { class CCTexture2D; }

namespace paimon {

struct AudioOwnerChangedEvent {
    std::string previous;  // "none", "menu", "dynamic", "profile", "preview"
    std::string current;
    int sessionToken = 0;
};

// Cambia el fondo y avisa a los suscritos.
struct ThumbnailBackgroundChangedEvent {
    int levelID = 0;
    geode::Ref<cocos2d::CCTexture2D> texture = nullptr;

// Lo lee InfoLayer al abrir, sin esperar al siguiente ciclo.
// Puntero crudo con retain manual: un estatico con destructor reventaria en atexit.
    static inline int s_lastLevelID = 0;
    static inline cocos2d::CCTexture2D* s_lastTextureRaw = nullptr;

    // Solo hilo principal.
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
