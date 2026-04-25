#pragma once

#include <Geode/DefaultInclude.hpp>
#include <optional>
#include <utility>
#include <mutex>
#include <unordered_map>

struct LevelColorPair {
    cocos2d::ccColor3B a;
    cocos2d::ccColor3B b;
};

class LevelColors {
public:
    static LevelColors& get();

    void set(int32_t levelID, cocos2d::ccColor3B a, cocos2d::ccColor3B b);
    std::optional<LevelColorPair> getPair(int32_t levelID) const;
    
    // fuerza escritura a disco si hay cambios pendientes
    void flushIfDirty();
    
    // procesar todas miniatura cacheadas y extraer colores
    void extractColorsFromCache();
    
    // extraer colores de un ccimage cargado
    void extractFromImage(int32_t levelID, cocos2d::CCImage* image);

    // extraer colores de datos rgba/rgb crudos
    void extractFromRawData(int32_t levelID, const uint8_t* data, int width, int height, bool hasAlpha);

private:
    LevelColors() = default;
    ~LevelColors();
    std::filesystem::path path() const;
    void load() const;
    void save() const;

    mutable bool m_loaded = false;
    mutable std::unordered_map<int32_t, LevelColorPair> m_items;
    mutable std::mutex m_mutex;
    mutable bool m_dirty = false;
    mutable int m_pendingWrites = 0;
    static constexpr int BATCH_SAVE_THRESHOLD = 10; // guardar cada N cambios
};

