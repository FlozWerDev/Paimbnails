
#include "ProfileImageCache.hpp"
#include "ProfileImageService.hpp"

#include <Geode/Geode.hpp>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <list>
#include <mutex>
#include <unordered_map>
#include <vector>
#include "../../../utils/FormatDetect.hpp"
#include "../../../utils/ImageLoadHelper.hpp"

using namespace geode::prelude;

static std::mutex s_profileImgMutex;
static std::unordered_map<int, geode::Ref<CCTexture2D>> s_profileImgCache;
static std::list<int> s_profileImgLru;
static std::unordered_map<int, std::list<int>::iterator> s_profileImgLruMap;
static constexpr size_t MAX_PROFILEIMG_CACHE_SIZE = 64;
static constexpr size_t MAX_PROFILEIMG_CACHE_BYTES = 64ull * 1024 * 1024;
static size_t s_profileImgCacheBytes = 0;
static std::atomic<bool> s_profileImgShutdown{false};

static size_t estimateTextureBytes(CCTexture2D* tex) {
    if (!tex) return 0;
    return static_cast<size_t>(tex->getPixelsWide()) * static_cast<size_t>(tex->getPixelsHigh()) * 4;
}

static void touchProfileImgCache(int accountID) {
    auto it = s_profileImgLruMap.find(accountID);
    if (it != s_profileImgLruMap.end()) {
        s_profileImgLru.erase(it->second);
    }
    s_profileImgLru.push_back(accountID);
    s_profileImgLruMap[accountID] = std::prev(s_profileImgLru.end());
}

void cacheProfileImgTexture(int accountID, CCTexture2D* texture) {
    if (!texture) return;
    if (s_profileImgShutdown.load(std::memory_order_acquire)) return;
    std::lock_guard<std::mutex> lock(s_profileImgMutex);

    size_t incomingBytes = estimateTextureBytes(texture);

    auto oldIt = s_profileImgCache.find(accountID);
    if (oldIt != s_profileImgCache.end()) {
        size_t oldBytes = estimateTextureBytes(oldIt->second);
        if (s_profileImgCacheBytes >= oldBytes) s_profileImgCacheBytes -= oldBytes;
    }

    s_profileImgCache[accountID] = texture;
    s_profileImgCacheBytes += incomingBytes;
    touchProfileImgCache(accountID);

    while ((s_profileImgCache.size() > MAX_PROFILEIMG_CACHE_SIZE ||
            s_profileImgCacheBytes > MAX_PROFILEIMG_CACHE_BYTES) &&
           !s_profileImgLru.empty()) {
        int removeID = s_profileImgLru.front();
        s_profileImgLru.pop_front();
        s_profileImgLruMap.erase(removeID);
        auto removeIt = s_profileImgCache.find(removeID);
        if (removeIt != s_profileImgCache.end()) {
            size_t removedBytes = estimateTextureBytes(removeIt->second);
            if (s_profileImgCacheBytes >= removedBytes) s_profileImgCacheBytes -= removedBytes;
            s_profileImgCache.erase(removeIt);
        }
    }
}

$on_game(Exiting) {
    s_profileImgShutdown.store(true, std::memory_order_release);
    std::lock_guard<std::mutex> lock(s_profileImgMutex);
    for (auto& [id, ref] : s_profileImgCache) {
        (void)ref.take();
    }
    s_profileImgCache.clear();
    s_profileImgCacheBytes = 0;
    s_profileImgLru.clear();
    s_profileImgLruMap.clear();
}

CCTexture2D* getProfileImgCachedTexture(int accountID) {
    if (s_profileImgShutdown.load(std::memory_order_acquire)) return nullptr;
    std::lock_guard<std::mutex> lock(s_profileImgMutex);
    auto it = s_profileImgCache.find(accountID);
    if (it != s_profileImgCache.end()) {
        touchProfileImgCache(accountID);
        return it->second;
    }
    return nullptr;
}

static std::filesystem::path getProfileImgCacheDir() {
    return Mod::get()->getSaveDir() / "profileimg_cache";
}

std::filesystem::path getProfileImgCachePath(int accountID) {
    return getProfileImgCacheDir() / fmt::format("{}.dat", accountID);
}

std::string getProfileImgGifCacheKey(int accountID) {
    auto key = ProfileImageService::get().getProfileImgGifKey(accountID);
    if (!key.empty()) {
        return key;
    }
    return fmt::format("profileimg_gif_{}", accountID);
}

void clearProfileImgCache() {
    std::lock_guard<std::mutex> lock(s_profileImgMutex);
    s_profileImgCache.clear();
    s_profileImgCacheBytes = 0;
    s_profileImgLru.clear();
    s_profileImgLruMap.clear();
    std::error_code ec;
    auto dir = getProfileImgCacheDir();
    if (std::filesystem::exists(dir, ec)) {
        std::filesystem::remove_all(dir, ec);
    }
}

void invalidateProfileImgCache(int accountID) {
    if (s_profileImgShutdown.load(std::memory_order_acquire)) return;
    std::lock_guard<std::mutex> lock(s_profileImgMutex);
    
    auto it = s_profileImgCache.find(accountID);
    if (it != s_profileImgCache.end()) {
        size_t removedBytes = estimateTextureBytes(it->second);
        if (s_profileImgCacheBytes >= removedBytes) {
            s_profileImgCacheBytes -= removedBytes;
        }
        s_profileImgCache.erase(it);
        
        auto lruIt = s_profileImgLruMap.find(accountID);
        if (lruIt != s_profileImgLruMap.end()) {
            s_profileImgLru.erase(lruIt->second);
            s_profileImgLruMap.erase(lruIt);
        }
        
        log::info("[ProfileImageCache] Invalidated RAM cache for accountID={}", accountID);
    }
}

CCTexture2D* decodeProfileImgBytes(uint8_t const* data, size_t size) {
    if (!data || size == 0) return nullptr;

    if (paimon::format::isGif(data, size)) {
        return nullptr;
    }

    if (size > 12) {
        for (size_t i = 0; i + 3 < size && i < 12; ++i) {
            if (data[i]=='f' && data[i+1]=='t' && data[i+2]=='y' && data[i+3]=='p') {
                return nullptr;
            }
        }
    }

    auto loaded = ImageLoadHelper::loadWithSTBFromMemory(data, size);
    if (!loaded.success || !loaded.texture) {
        return nullptr;
    }
    loaded.texture->autorelease();
    return loaded.texture;
}

CCTexture2D* loadProfileImgFromDisk(int accountID) {
    auto path = getProfileImgCachePath(accountID);

    // Open first and size via the stream: the separate exists() was one more
    // main-thread stat, and a failed open covers the missing-file case anyway.
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return nullptr;

    auto size = file.tellg();
    if (size <= 0) return nullptr;
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) return nullptr;
    file.close();

    return decodeProfileImgBytes(data.data(), data.size());
}

void saveProfileImgToDisk(int accountID, std::vector<uint8_t> const& data) {
    auto cacheDir = getProfileImgCacheDir();
    std::error_code ec;
    std::filesystem::create_directories(cacheDir, ec);
    auto cachePath = getProfileImgCachePath(accountID);
    std::ofstream cacheFile(cachePath, std::ios::binary);
    if (cacheFile) {
        cacheFile.write(reinterpret_cast<char const*>(data.data()), data.size());
        cacheFile.close();
    }
}
