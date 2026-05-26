#pragma once
// BlurDiskCache — cache persistente en disco para texturas blur pre-computadas.
//
// Porque: el blur Dual Kawase (6-14 FBO passes) es el paso mas caro del pipeline
// de thumbnails. Si se guarda el resultado en disco, la segunda entrada al juego
// con cache poblado salta ese paso entero: solo cargamos raw RGBA desde disco
// y subimos a GPU como textura directamente.
//
// Formato en disco: header binario + raw RGBA8888. Sin PNG (el decode de PNG
// para una imagen de 512x288 tarda mas que volver a calcular el blur en GPU).
//
// Thread-safety: lookups async en background thread (I/O), callback vuelve al
// main thread para upload GPU.

#include <Geode/utils/cocos.hpp>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <atomic>
#include <cstdint>

namespace paimon::blur {

class BlurDiskCache {
public:
    static BlurDiskCache& get();

    using ReadyCallback = std::function<void(cocos2d::CCTexture2D* texture)>;

    /// Inicializa el cache. Crea el directorio si no existe, lee el indice.
    /// Seguro de llamar multiples veces.
    void init();

    /// Busca una entrada en el cache. Si hit, carga desde disco en background
    /// y crea la textura en main thread; si miss, invoca onReady(nullptr).
    /// `key` debe ser el string canonico generado por makeKey().
    void lookupAsync(std::string const& key, ReadyCallback onReady);

    /// Busqueda sincrona solo del indice (no hace I/O). Util para decidir si
    /// un caller debe iniciar el blur GPU o esperar al lookup async.
    bool hasEntry(std::string const& key) const;

    /// Persiste un blur calculado a disco. Lee pixeles del RT, serializa a
    /// formato .pblur, escribe en background thread. `key` debe matchear el
    /// que se uso en lookupAsync.
    void storeAsync(std::string const& key, cocos2d::CCRenderTexture* rt);

    /// Alternativa para almacenar desde un CCTexture2D (lee pixeles via glReadPixels).
    void storeFromTextureAsync(std::string const& key, cocos2d::CCTexture2D* tex, int width, int height);

    /// Invalida una entrada (por ejemplo cuando el thumbnail source cambia).
    void invalidate(std::string const& key);

    /// Limpia todo el cache de blur.
    void clear();

    /// Estadisticas
    std::size_t diskEntryCount() const;
    std::size_t ramEntryCount() const;

    /// Shutdown ordenado
    void shutdown();

private:
    BlurDiskCache() = default;
    ~BlurDiskCache() = default;

    struct IndexEntry {
        int width = 0;
        int height = 0;
        std::int64_t mtimeEpoch = 0;
        std::int64_t byteSize = 0;
    };

    std::filesystem::path cacheDir() const;
    std::filesystem::path pathForKey(std::string const& key) const;

    bool loadIndex();
    bool writeIndex();

    cocos2d::CCTexture2D* uploadRawRGBA(std::vector<uint8_t> const& pixels, int w, int h);

    mutable std::shared_mutex m_mutex;
    std::unordered_map<std::string, IndexEntry> m_index;
    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_shuttingDown{false};
    std::atomic<std::size_t> m_ramHits{0};
    std::atomic<std::size_t> m_diskHits{0};
    std::atomic<std::size_t> m_misses{0};
    std::atomic<std::size_t> m_stores{0};

    // Maximo tamaño de cache en disco (MB). Se evictan las entries mas antiguas
    // cuando se excede. Conservador: blurs de 512x288 raw son ~576KB cada uno,
    // 256MB = ~450 entradas.
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    static constexpr std::int64_t MAX_DISK_SIZE_BYTES = 64LL * 1024 * 1024;  // 64 MB
#else
    static constexpr std::int64_t MAX_DISK_SIZE_BYTES = 256LL * 1024 * 1024; // 256 MB
#endif

    // Formato: 4 bytes magic + 4 bytes version + 4 bytes width + 4 bytes height +
    // 4 bytes reservado + raw RGBA8888 (width*height*4 bytes).
    static constexpr std::uint32_t MAGIC = 0x504C4255u; // 'PLBU' en LE = 'UBLP'
    static constexpr std::uint32_t VERSION = 1u;
    static constexpr std::size_t HEADER_SIZE = 20;
};

/// Genera una key canonica para una combinacion de parametros de blur.
/// `sourceID` suele ser levelID o URL hash, `thumbIndex` para galeria,
/// `style` "paimon" o "gauss", `intensity` el slider.
std::string makeKey(std::int64_t sourceID, int thumbIndex, char const* style,
                    int intensity, int width, int height);

} // namespace paimon::blur
