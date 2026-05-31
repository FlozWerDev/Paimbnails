#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace cocos2d { class CCTexture2D; }

namespace paimon {

// ── Eventos tipados del mod ─────────────────────────────────────────

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

// Emitido por LevelInfoLayer cuando cambia el fondo de thumbnail.
// CustomSongWidget e InfoLayer se suscriben para sincronizar sus fondos.
struct ThumbnailBackgroundChangedEvent {
    int levelID = 0;
    geode::Ref<cocos2d::CCTexture2D> texture = nullptr;

    // Cache del ultimo thumbnail activo por nivel (para que InfoLayer pueda
    // consultar la textura actual al abrirse, sin esperar al proximo ciclo).
    //
    // IMPORTANTE: NO usamos `geode::Ref<>` aqui porque seria una variable
    // estatica con destructor no-trivial. En atexit, CCPoolManager ya esta
    // destruido y `Ref::~Ref()` llama `release()` sobre memoria invalida → crash.
    //
    // Usamos raw pointer con retain manual; RuntimeLifecycle.cpp ($on_game(Exiting))
    // hace setLastTexture(nullptr) para liberar correctamente. Si Exiting no
    // dispara (kill -9, crash de otro hilo), el OS recupera el handle al cerrar
    // el proceso — preferible al crash de atexit.
    static inline int s_lastLevelID = 0;
    static inline cocos2d::CCTexture2D* s_lastTextureRaw = nullptr;

    // Helpers thread-safe para mutar el cache (deben llamarse desde main thread).
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
