#pragma once

// CacheModels.hpp — Modelos canonicos de cache para thumbnails.
// Define CacheKey (clave unica por recurso+calidad+formato),
// DiskManifestEntry (metadata persistente por archivo) y
// CacheStats (contadores de instrumentacion).

#include <string>
#include <string_view>
#include <cstdint>
#include <chrono>
#include <unordered_map>
#include <mutex>
#include <filesystem>
#include <functional>

namespace paimon::cache {

// ── Tipos de recurso en cache ───────────────────────────────────────

enum class ResourceType : uint8_t {
    LevelThumb   = 0, // thumbnail principal de nivel (PNG)
    LevelGif     = 1, // thumbnail GIF animado de nivel
    GalleryThumb = 2, // thumbnail de galeria (por URL)
};

// ── Cache Key ───────────────────────────────────────────────────────

// Clave canonica que identifica un recurso en cache de forma unica.
// Reemplaza el antiguo `int key` (levelID negativo para GIF).
struct CacheKey {
    ResourceType type = ResourceType::LevelThumb;
    int levelID = 0;           // para LevelThumb/LevelGif
    std::string url;           // para GalleryThumb (URL canonica sin query de version)
    std::string qualityTag;    // "low", "med", "high"

    bool operator==(CacheKey const& o) const {
        return type == o.type && levelID == o.levelID && url == o.url && qualityTag == o.qualityTag;
    }

    // conversion de/hacia el legacy int key para compatibilidad
    static CacheKey fromLegacy(int key, std::string const& quality = "") {
        CacheKey k;
        k.qualityTag = quality;
        if (key < 0) {
            k.type = ResourceType::LevelGif;
            k.levelID = -key;
        } else {
            k.type = ResourceType::LevelThumb;
            k.levelID = key;
        }
        return k;
    }

    // devuelve el int legacy equivalente (negativo para GIF)
    int toLegacy() const {
        if (type == ResourceType::LevelGif) return -levelID;
        return levelID;
    }

    // string unico para usar como key de mapa o nombre de manifest
    std::string toString() const {
        switch (type) {
            case ResourceType::LevelThumb:
                return "t_" + std::to_string(levelID) + "_" + qualityTag;
            case ResourceType::LevelGif:
                return "g_" + std::to_string(levelID) + "_" + qualityTag;
            case ResourceType::GalleryThumb:
                return "u_" + url + "_" + qualityTag;
        }
        return {};
    }
};

struct CacheKeyHash {
    size_t operator()(CacheKey const& k) const {
        size_t h = std::hash<uint8_t>{}(static_cast<uint8_t>(k.type));
        h ^= std::hash<int>{}(k.levelID) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<std::string>{}(k.url) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<std::string>{}(k.qualityTag) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

// ── Disk Manifest Entry ─────────────────────────────────────────────

// Metadata persistente por archivo en disco. Se serializa a JSON como
// un diccionario dentro del manifest file.
struct DiskManifestEntry {
    std::string filename;          // nombre en disco, e.g. <levelID>.png
    int levelID = 0;               // levelID original (0 para gallery)
    std::string sourceUrl;         // URL de descarga si aplica
    std::string revisionToken;     // hash de revision: thumbnailId o fallback date|format|url
    std::string format;            // "png", "gif", "jpg", "webp"
    int width = 0;
    int height = 0;
    size_t byteSize = 0;
    int64_t lastAccessEpoch = 0;   // epoch seconds de ultimo acceso
    int64_t lastValidatedEpoch = 0;// epoch seconds when revision was last confirmed fresh
    bool isGif = false;
    std::string qualityTag;        // quality tier cuando se guardo

    // genera un revision token a partir de ThumbnailInfo del transport
    static std::string makeRevisionToken(std::string const& thumbnailId,
                                         std::string const& date,
                                         std::string const& format,
                                         std::string const& url) {
        // si tenemos thumbnailId, lo usamos directamente (mas estable)
        if (!thumbnailId.empty()) return thumbnailId;
        // fallback: concatenar campos que cambian al re-subir
        return date + "|" + format + "|" + url;
    }

    int64_t nowEpoch() const {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }

    void touchAccess() { lastAccessEpoch = nowEpoch(); }
    void touchValidated() { lastValidatedEpoch = nowEpoch(); }
};

// ── Cache Stats (instrumentacion) ───────────────────────────────────

struct CacheStats {
    std::atomic<uint64_t> ramHits{0};
    std::atomic<uint64_t> ramMisses{0};
    std::atomic<uint64_t> diskHits{0};
    std::atomic<uint64_t> diskMisses{0};
    std::atomic<uint64_t> staleHits{0};     // RAM hit pero version stale
    std::atomic<uint64_t> downloads{0};
    std::atomic<uint64_t> downloadErrors{0};
    std::atomic<uint64_t> ramEvictions{0};
    std::atomic<uint64_t> diskEvictions{0};
    std::atomic<uint64_t> decodeTimeUsTotal{0}; // microsegundos acumulados de decode
    std::atomic<uint64_t> diskReadTimeUsTotal{0}; // microsegundos acumulados de lectura de disco

    void reset() {
        ramHits = 0; ramMisses = 0;
        diskHits = 0; diskMisses = 0;
        staleHits = 0;
        downloads = 0; downloadErrors = 0;
        ramEvictions = 0; diskEvictions = 0;
        decodeTimeUsTotal = 0;
        diskReadTimeUsTotal = 0;
    }
};

} // namespace paimon::cache
