#pragma once

#include <Geode/DefaultInclude.hpp>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <mutex>
#include <atomic>

class LocalThumbs {
public:
    static constexpr int MAX_THUMBS_PER_LEVEL = 10;

    static LocalThumbs& get();

    std::optional<std::string> getThumbPath(int32_t levelID) const;

    std::optional<std::string> getThumbPathByIndex(int32_t levelID, int index) const;

    std::vector<std::string> getAllThumbPaths(int32_t levelID) const;

    int getThumbCount(int32_t levelID) const;

    std::optional<std::string> findAnyThumbnail(int32_t levelID) const;

    // .rgb: raw rgb888 pixels (isrgb=true). png/jpg/webp: raw file bytes.
    struct LoadResult {
        std::vector<uint8_t> pixels;
        int width = 0;
        int height = 0;
        bool isRgb = false;
    };
    LoadResult loadAsRGBA(int32_t levelID) const;

    bool has(int32_t levelID) const;

    // ram-cached: runs per layer transition; uncached it would re-read mbs from disk on main.
    cocos2d::CCTexture2D* loadTexture(int32_t levelID) const;

    cocos2d::CCTexture2D* getCachedTexture(int32_t levelID) const;

    // async load: read+convert on worker, gpu upload on main; callback always on main.
    void loadTextureAsync(int32_t levelID, std::function<void(cocos2d::CCTexture2D*)> callback);

    cocos2d::CCTexture2D* loadTextureByIndex(int32_t levelID, int index) const;

    std::vector<int32_t> getAllLevelIDs() const;

    // appends to the gallery, never overwrites.
    bool saveRGB(int32_t levelID, const uint8_t* data, uint32_t width, uint32_t height);

    bool removeThumb(int32_t levelID, int index);

    void loadMappings();
    void shutdown();

    void invalidateLookup(int32_t levelID);

    // drops the ram texture cache (dies with the gl context on reloadall); disk files stay, reload lazy.
    void clearTextureCache();

private:
    LocalThumbs();
    std::filesystem::path dir() const;
    std::filesystem::path mappingFile() const;
    std::unordered_map<int32_t, std::string> m_fileMapping;
    
    std::unordered_set<int32_t> m_availableLevels;
    mutable std::mutex m_mutex;
    std::atomic<bool> m_cacheInitialized{false};
    std::atomic<bool> m_shuttingDown{false};

    struct LookupEntry {
        std::optional<std::string> path;
    };
    mutable std::unordered_map<int32_t, LookupEntry> m_lookupCache;
    mutable std::mutex m_lookupMutex;

    static constexpr size_t MAX_TEX_CACHE_ENTRIES = 4;
    mutable std::mutex m_texCacheMutex;
    mutable std::unordered_map<int32_t, cocos2d::CCTexture2D*> m_texCache;
    mutable std::deque<int32_t> m_texCacheLru;
    void cacheTexture(int32_t levelID, cocos2d::CCTexture2D* tex) const;
    void invalidateTexture(int32_t levelID) const;

    void initCache();
    void migrateLegacyFile(int32_t levelID, std::filesystem::path const& legacyPath);
    int nextIndex(int32_t levelID) const;
};
