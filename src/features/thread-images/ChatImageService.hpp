#pragma once

#include "ChatImageToken.hpp"
#include "../../utils/ClipboardImage.hpp"
#include "../../utils/ThreadPool.hpp"

#include <Geode/Geode.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace paimon::chatimg {

struct PreparedUpload {
    std::vector<uint8_t> bytes;
    std::string ext;
    std::string mime;
    int width = 0;
    int height = 0;
    bool isGif = false;
    std::string error;

    bool ok() const {
        return error.empty() && !bytes.empty() && width > 0 && height > 0;
    }
};

struct UploadResult {
    bool ok = false;
    ChatImageRef ref;
    std::string token;
    bool needsVerification = false;
    std::string error;
};

struct LoadedChatImage {
    bool ok = false;
    bool isGif = false;
    bool needsVerification = false;
    geode::Ref<cocos2d::CCTexture2D> texture;
    std::vector<uint8_t> rawBytes;
    int width = 0;
    int height = 0;
    std::string error;
};

class ChatImageService {
public:
    using PrepareCallback = geode::CopyableFunction<void(PreparedUpload result)>;
    using UploadCallback  = geode::CopyableFunction<void(UploadResult result)>;
    using LoadCallback    = geode::CopyableFunction<void(LoadedChatImage image)>;
    using ActionCallback  = geode::CopyableFunction<void(bool ok, std::string const& message)>;
    using RgbaCallback    = geode::CopyableFunction<void(bool ok, std::vector<uint8_t> rgba, int w, int h)>;

    static ChatImageService& get();
    static bool isInitialized();
    void requestShutdown();
    void waitShutdown(std::chrono::steady_clock::time_point deadline);
    void shutdown();

    // Reads, downscales (static max 1280px longest side), and encodes JPEG/PNG off-thread. GIFs <= 4 MB pass raw.
    void prepareFromFile(std::filesystem::path const& path, PrepareCallback cb);
    void prepareFromClipboard(paimon::ClipboardImageData data, PrepareCallback cb);

    // Encrypts (PIMG2 ChaCha20-Poly1305 bound to threadID) and uploads privately.
    void uploadPrepared(PreparedUpload const& prepared, int threadID, UploadCallback cb);
    void loadImage(ChatImageRef const& ref, int threadID, LoadCallback cb);
    void deleteImage(ChatImageRef const& ref, ActionCallback cb);
    void saveRawBytesToPath(std::vector<uint8_t> bytes, std::filesystem::path dest, ActionCallback cb);
    void decodeRawToRgbaAsync(std::vector<uint8_t> bytes, RgbaCallback cb);

    LoadedChatImage const* getCached(std::string const& ramKey) const;
    void evict(std::string const& imageId);
    void clearTextureCache();

private:
    ChatImageService();
    ~ChatImageService() = default;
    ChatImageService(ChatImageService const&) = delete;
    ChatImageService& operator=(ChatImageService const&) = delete;

    std::filesystem::path cacheDir() const;
    std::filesystem::path diskPathForId(std::string const& imageId) const;
    static std::string makeRamKey(ChatImageRef const& ref, int threadID);

    void putRamCache(std::string const& ramKey, LoadedChatImage const& image);
    void notifyPending(std::string const& ramKey, LoadedChatImage const& image);
    void downloadFromServer(std::string const& ramKey, ChatImageRef const& ref, int threadID);
    void decodeAndCachePlaintext(std::string const& ramKey, ChatImageRef const& ref, std::vector<uint8_t> plaintext);
    void finishDecodedStatic(std::string const& ramKey, ChatImageRef const& ref, std::vector<uint8_t> rawBytes,
        std::vector<uint8_t> rgba, int w, int h);
    void finishWebpOnMain(std::string const& ramKey, ChatImageRef const& ref, std::vector<uint8_t> rawBytes);
    void writeDiskCacheAsync(std::string const& imageId, std::vector<uint8_t> encryptedBlob);
    void pruneDiskCacheLocked();

    paimon::ThreadPool* ensurePool();
    void enqueuePoolJob(std::function<void()> job);

    static constexpr size_t kMaxRamEntries = 48;
    static constexpr size_t kMaxStaticUploadBytes = 2u * 1024u * 1024u;
    static constexpr size_t kMaxGifUploadBytes = 4u * 1024u * 1024u;
    static constexpr size_t kMaxDiskCacheBytes = 50u * 1024u * 1024u;
    static constexpr int kMaxStaticDimension = 1280;
    static constexpr int kFallbackStaticDimension = 960;
    static constexpr int kMaxGifDimension = 1920;
    static constexpr auto kMaxDiskCacheAge = std::chrono::hours(24 * 14);

    std::atomic<bool> m_shuttingDown{false};
    std::mutex m_poolMutex;
    paimon::ThreadPool* m_pool = nullptr;
    std::list<std::pair<std::string, LoadedChatImage>> m_lru;
    std::unordered_map<std::string, std::list<std::pair<std::string, LoadedChatImage>>::iterator> m_lruIndex;
    std::unordered_map<std::string, std::vector<LoadCallback>> m_pending;
    std::mutex m_diskWriteMutex;
    size_t m_writesSincePrune = 0;
};

} // namespace paimon::chatimg
