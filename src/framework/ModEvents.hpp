#pragma once

#include <Geode/Geode.hpp>
#include <string>
#include <cstdint>

namespace cocos2d { class CCTexture2D; }

namespace paimon {

// ── Eventos tipados del mod — heredan geode::Event ──────────────────
// Publicar:  MyEvent{...}.post();
// Suscribir: geode::EventListener<MyEvent> m_listener;
//            m_listener.bind([](MyEvent* e) { ... });

struct ThumbnailLoadedEvent : public geode::Event {
    int levelID = 0;
    std::string source;   // "network", "disk-cache", "ram-cache"
    bool isGif = false;
};

struct CacheEvictedEvent : public geode::Event {
    std::string key;
    std::string reason;   // "lru", "ttl", "manual"
    size_t freedBytes = 0;
};

struct UploadCompletedEvent : public geode::Event {
    int levelID = 0;
    std::string format;   // "png", "gif", "mp4"
    std::string username;
    bool success = false;
    std::string message;
};

struct FeatureToggledEvent : public geode::Event {
    std::string featureName;
    bool enabled = false;
};

struct PermissionDeniedEvent : public geode::Event {
    std::string featureName;
    std::string action;
    std::string reason;
};

struct UploadStartedEvent : public geode::Event {
    int levelID = 0;
    std::string format;
    std::string username;
    size_t dataSize = 0;
};

struct AudioOwnerChangedEvent : public geode::Event {
    std::string previous;  // "none", "menu", "dynamic", "profile", "preview"
    std::string current;
    int sessionToken = 0;
};

// Emitido por LevelInfoLayer cuando cambia el fondo de thumbnail.
// CustomSongWidget e InfoLayer se suscriben para sincronizar sus fondos.
struct ThumbnailBackgroundChangedEvent : public geode::Event {
    int levelID = 0;
    geode::Ref<cocos2d::CCTexture2D> texture = nullptr;
};

// Cache del ultimo thumbnail activo por nivel.
// Permite que InfoLayer/LevelLeaderboard consulten la textura actual al
// abrirse, sin esperar al proximo ciclo del event bus.
// Escrito por LevelInfoLayer::applyThumbnailBackground; leido por InfoLayer
// y LevelLeaderboard durante su inicializacion.
struct ThumbnailBgCache {
    static inline int lastLevelID = 0;
    static inline geode::Ref<cocos2d::CCTexture2D> lastTexture = nullptr;
};

} // namespace paimon
