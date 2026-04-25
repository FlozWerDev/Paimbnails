#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/function.hpp>
#include <prevter.imageplus/include/events.hpp>
#include <vector>
#include <string>
#include <utility>
#include <list>
#include <deque>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <atomic>
#include <filesystem>
#include <chrono>

// Animated sprite that plays GIF frames with caching and incremental loading.
class AnimatedGIFSprite : public cocos2d::CCSprite {
public:
    static AnimatedGIFSprite* create(std::string const& filename);
    static AnimatedGIFSprite* create(const void* data, size_t size);

    static void pinGIF(std::string const& key);
    static void unpinGIF(std::string const& key);
    static bool isPinned(std::string const& key);

    struct SharedGIFData {
        std::vector<cocos2d::CCTexture2D*> textures;
        std::vector<float> delays;
        std::vector<cocos2d::CCRect> frameRects; // Stores left, top, width, height
        int width;
        int height;
    };

protected:
    struct GIFFrame {
        cocos2d::CCTexture2D* texture = nullptr;
        cocos2d::CCRect rect; // Position and size in canvas
        float delay = 0.1f; // Seconds
        
        ~GIFFrame() {
            if (texture) {
                texture->release();
                texture = nullptr;
            }
        }
    };
    
    static std::unordered_map<std::string, SharedGIFData> s_gifCache;
    static std::list<std::string> s_lruList;
    static std::unordered_map<std::string, std::list<std::string>::iterator> s_lruMap; // LRU O(1)
    static std::unordered_set<std::string> s_pinnedGIFs;
    static std::mutex s_cacheMutex; // protege s_gifCache, s_lruList, s_pinnedGIFs, s_currentCacheSize
    
    static size_t s_currentCacheSize; // Bytes
    static size_t getMaxCacheMem();
    static void pruneDiskCache();
    static std::filesystem::path getDiskCacheDir();
    
    // eviccion centralizada: quita entradas LRU hasta que el cache esta por debajo del limite
    static void evictIfNeeded();

    std::vector<GIFFrame*> m_frames;
    // Dominant colors per frame: {A, B}
    std::vector<std::pair<cocos2d::ccColor3B, cocos2d::ccColor3B>> m_frameColors;
    
    unsigned int m_currentFrame = 0;
    float m_frameTimer = 0.0f;
    bool m_isPlaying = true;
    bool m_loop = true;
    std::string m_filename;
    int m_canvasWidth = 0;
    int m_canvasHeight = 0;
    
    // Frame pendiente de carga incremental (datos RGBA ya decodificados)
    struct PendingFrame {
        std::vector<uint8_t> pixels; // RGBA8888
        int left = 0;
        int top = 0;
        int width = 0;
        int height = 0;
        int delayMs = 100;
    };

    std::deque<PendingFrame> m_pendingFrames;
    void updateTextureLoading(float dt);

    void updateAnimation(float dt);
    
    virtual ~AnimatedGIFSprite();
    
public:
    // Shader support (used by e.g. LevelCell blur)
    float m_intensity = 0.0f;
    float m_time = 0.0f;
    float m_brightness = 1.0f;
    cocos2d::CCSize m_texSize = {0, 0};
    cocos2d::CCSize m_screenSize = {0, 0};

    static void clearCache();
    static void remove(std::string const& filename);
    static bool isCached(std::string const& filename);
    static size_t currentCacheBytes() { return s_currentCacheSize; }
    
    using AsyncCallback = geode::CopyableFunction<void(AnimatedGIFSprite*)>;
    static void createAsync(std::string const& path, AsyncCallback callback);
    static void createAsync(std::vector<uint8_t> const& data, std::string const& key, AsyncCallback callback);
    
    static AnimatedGIFSprite* createFromCache(std::string const& key);

    // Disk cache
    struct DiskCacheEntry {
        int width;
        int height;
        struct Frame {
            std::vector<uint8_t> pixels; // RGBA8888
            float delay;
            int width;
            int height;
        };
        std::vector<Frame> frames;
    };
    
    static bool loadFromDiskCache(std::string const& path, DiskCacheEntry& outEntry);
    static void saveToDiskCache(std::string const& path, DiskCacheEntry const& entry);
    static std::string getCachePath(std::string const& path);

private:
    static constexpr auto MAX_DISK_CACHE_AGE = std::chrono::hours(24 * 21);

    // Worker queue
    struct GIFTask {
        std::string path;
        std::vector<uint8_t> data;
        std::string key;
        AsyncCallback callback;
        bool isData = false;
    };
    
    static std::deque<GIFTask> s_taskQueue;
    static std::mutex s_queueMutex;
    static std::condition_variable s_queueCV;
    static std::thread s_workerThread;
    static std::atomic<bool> s_workerRunning;
    static std::atomic<bool> s_shutdownMode;
    static std::mutex s_workerLifecycleMutex;
    static void workerLoop();
    static void initWorker();
    static void shutdownWorker();

public:
    void play() { m_isPlaying = true; this->scheduleUpdate(); }
    void pause() { m_isPlaying = false; }
    void stop() { 
        m_isPlaying = false; 
        m_currentFrame = 0;
        if (!m_frames.empty() && m_frames[0] && m_frames[0]->texture) {
            this->setTexture(m_frames[0]->texture);
        }
    }

    void onEnter() override {
        CCSprite::onEnter();
        if (m_isPlaying && !m_frames.empty()) {
            this->scheduleUpdate();
        }
    }
    
    void setLoop(bool loop) { m_loop = loop; }
    bool isPlaying() const { return m_isPlaying; }
    bool isLooping() const { return m_loop; }
    
    unsigned int getCurrentFrame() const { return m_currentFrame; }
    unsigned int getFrameCount() const { return m_frames.size(); }
    std::string getCacheKey() const { return m_filename; }
    
    void setCurrentFrame(unsigned int frame);

    // Used by the async loader to ensure frame 0 exists before layout.
    bool processNextPendingFrame();

    std::pair<cocos2d::ccColor3B, cocos2d::ccColor3B> getCurrentFrameColors() const {
        if (m_currentFrame < m_frameColors.size()) {
            return m_frameColors[m_currentFrame];
        }
        return { {0,0,0}, {0,0,0} };
    }

private:
    bool initFromCache(std::string const& cacheKey);
    
    std::string const& getFilename() const { return m_filename; }
    
    void update(float dt) override;

    // Override draw for shader support
    void draw() override;

private:
};
