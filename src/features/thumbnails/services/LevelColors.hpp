#pragma once

#include <Geode/DefaultInclude.hpp>
#include <cstdint>
#include <optional>
#include <utility>
#include <mutex>
#include <unordered_map>
#include <vector>

struct LevelColorPair {
    cocos2d::ccColor3B a;
    cocos2d::ccColor3B b;
};

class LevelColors {
public:
    static LevelColors& get();
    static bool isInitialized();

    void set(int32_t levelID, cocos2d::ccColor3B a, cocos2d::ccColor3B b);
    std::optional<LevelColorPair> getPair(int32_t levelID) const;
    
    void flushIfDirty();
    
    void preloadIndexFromDisk();

    void extractColorsFromCache();
    
    void extractFromImage(int32_t levelID, cocos2d::CCImage* image);

    void extractFromRawData(int32_t levelID, const uint8_t* data, int width, int height, bool hasAlpha);

private:
    LevelColors() = default;
    ~LevelColors();
    std::filesystem::path path() const;
    void load() const;
    // serializes and writes a snapshot; never called with m_mutex held.
    bool saveItems(std::vector<std::pair<int32_t, LevelColorPair>> const& items) const;

    mutable bool m_loaded = false;
    mutable std::unordered_map<int32_t, LevelColorPair> m_items;
    mutable std::mutex m_mutex;
    // serializes file publication only; the data lock above stays free for
    // per-cell reads while a snapshot is being written.
    mutable std::mutex m_saveMutex;
    mutable bool m_dirty = false;
    mutable int m_pendingWrites = 0;
    mutable uint64_t m_generation = 0;
    static constexpr int BATCH_SAVE_THRESHOLD = 10;
};
