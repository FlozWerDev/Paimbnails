#pragma once

#include <Geode/DefaultInclude.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <mutex>
#include <atomic>
#include <future>

class LocalThumbs {
public:
    static constexpr int MAX_THUMBS_PER_LEVEL = 10;

    static LocalThumbs& get();

    // ruta local thumb si existe (retorna la mas reciente)
    std::optional<std::string> getThumbPath(int32_t levelID) const;

    // ruta a thumb por indice (0-based)
    std::optional<std::string> getThumbPathByIndex(int32_t levelID, int index) const;

    // todas las rutas de thumbnails locales para un nivel
    std::vector<std::string> getAllThumbPaths(int32_t levelID) const;

    // cuantos thumbnails locales tiene un nivel
    int getThumbCount(int32_t levelID) const;

    // ruta a thumb valida (rgb/png/jpg/webp)
    std::optional<std::string> findAnyThumbnail(int32_t levelID) const;

    // carga un archivo local (.rgb o imagen estandar) y retorna pixels.
    // Para .rgb: retorna RGB888 raw (isRgb=true) — ThumbnailLoader convierte a RGBA antes de subir.
    // Para png/jpg/webp: retorna bytes crudos para decoder.
    // out_width/out_height se llenan con las dimensiones de la imagen.
    // retorna vector vacio si fallo la carga.
    struct LoadResult {
        std::vector<uint8_t> pixels; // RGB888 if isRgb, else raw file bytes
        int width = 0;
        int height = 0;
        bool isRgb = false; // true si la fuente era .rgb (pixels son RGB888, se convierte antes de upload)
    };
    LoadResult loadAsRGBA(int32_t levelID) const;

    bool has(int32_t levelID) const { return getThumbPath(levelID).has_value(); }

    // load textura levelID; nullptr si no
    cocos2d::CCTexture2D* loadTexture(int32_t levelID) const;

    // load textura por indice
    cocos2d::CCTexture2D* loadTextureByIndex(int32_t levelID, int index) const;

    // todos los levelIDs con thumb local
    std::vector<int32_t> getAllLevelIDs() const;

    // guardar rgb24 + size (agrega al final de la galeria, no sobreescribe)
    bool saveRGB(int32_t levelID, const uint8_t* data, uint32_t width, uint32_t height);

    // guardar rgba32 (-> rgb24 interno)
    bool saveFromRGBA(int32_t levelID, const uint8_t* data, uint32_t width, uint32_t height);

    // borrar un thumbnail por indice, re-indexa los restantes
    bool removeThumb(int32_t levelID, int index);

    // Mapping system: levelID -> fileName (para nueva API)
    void storeFileMapping(int32_t levelID, std::string const& fileName);
    std::optional<std::string> getFileName(int32_t levelID) const;
    void loadMappings();
    void saveMappings();
    void shutdown();

private:
    LocalThumbs(); // privado
    std::string dir() const;
    std::string mappingFile() const;
    std::unordered_map<int32_t, std::string> m_fileMapping;
    
    // cache busqueda
    std::unordered_set<int32_t> m_availableLevels;
    mutable std::mutex m_mutex;
    std::atomic<bool> m_cacheInitialized{false};
    std::atomic<bool> m_shuttingDown{false};
    mutable std::future<void> m_initFuture;
    void initCache();
    void migrateLegacyFile(int32_t levelID, std::filesystem::path const& legacyPath);
    int nextIndex(int32_t levelID) const; // siguiente indice disponible (sin lock)
};

