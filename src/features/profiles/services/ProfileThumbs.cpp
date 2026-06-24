#include "ProfileThumbs.hpp"
#include "../../../managers/ThumbnailAPI.hpp"
#include "../../../core/Settings.hpp"
#include "../../../utils/AnimatedGIFSprite.hpp"
#include "../../../core/QualityConfig.hpp"
#include "../../../utils/ImageConverter.hpp"
#include "../../../utils/PaimonDrawNode.hpp"
#include "../../../utils/HttpClient.hpp"
#include "ProfileImageService.hpp"
#include <Geode/utils/file.hpp>
#include <Geode/utils/string.hpp>
#include <filesystem>
#include "../../../utils/TimedJoin.hpp"
#include <Geode/loader/Mod.hpp>
#include <fstream>
#include <algorithm>
#include <deque>

#include "../../../utils/Shaders.hpp"
#include "../../../utils/GLSLLoader.hpp"
#include "../../../blur/BlurSystem.hpp"
#include "../../../utils/stb_image.h"

using namespace geode::prelude;
using namespace cocos2d;
using namespace Shaders;




namespace {
    struct Header { int32_t w; int32_t h; int32_t fmt; };
    constexpr uintmax_t kMaxProfileThumbFileBytes = 20ull * 1024ull * 1024ull;

    using ProfileThumbCallback = geode::CopyableFunction<void(bool, cocos2d::CCTexture2D*)>;

    bool profileThumbsShouldAbort() {
        return ProfileThumbs::s_shutdownMode.load(std::memory_order_acquire);
    }

    void dispatchProfileThumbCallbacks(std::vector<ProfileThumbCallback> callbacks,
                                       bool success,
                                       cocos2d::CCTexture2D* texture) {
        if (callbacks.empty() || profileThumbsShouldAbort()) return;
        Ref<CCTexture2D> texRef = texture;
        Loader::get()->queueInMainThread([callbacks = std::move(callbacks), success, texRef]() mutable {
            if (profileThumbsShouldAbort()) return;
            for (auto& cb : callbacks) {
                if (cb) cb(success, texRef);
            }
        });
    }

    std::mutex& getProfileThumbsPruneMutex() {
        static std::mutex mutex;
        return mutex;
    }

    std::filesystem::path getProfileThumbsDir() {
        return paimon::quality::cacheDir() / "profiles";
    }

    size_t getProfileThumbsMaxBytes() {
        return std::clamp<size_t>(
            paimon::settings::quality::diskCacheBytes() / 2,
            128ull * 1024ull * 1024ull,
            512ull * 1024ull * 1024ull
        );
    }

    void pruneProfileThumbsDiskCache() {
        std::lock_guard<std::mutex> lock(getProfileThumbsPruneMutex());

        auto dir = getProfileThumbsDir();
        std::error_code ec;
        if (!std::filesystem::exists(dir, ec)) {
            return;
        }

        struct CacheEntry {
            std::filesystem::path path;
            std::filesystem::file_time_type mtime;
            uintmax_t size = 0;
        };

        std::vector<CacheEntry> entries;
        uintmax_t totalBytes = 0;
        auto now = std::filesystem::file_time_type::clock::now();
        auto maxAge = std::chrono::hours(24 * 14);

        for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (ec || !entry.is_regular_file()) continue;

            std::error_code sizeEc;
            auto fileSize = entry.file_size(sizeEc);
            if (sizeEc) continue;

            std::error_code timeEc;
            auto mtime = entry.last_write_time(timeEc);
            if (timeEc) continue;

            if (now - mtime > maxAge) {
                std::filesystem::remove(entry.path(), timeEc);
                continue;
            }

            totalBytes += fileSize;
            entries.push_back({entry.path(), mtime, fileSize});
        }

        auto maxBytes = getProfileThumbsMaxBytes();
        if (totalBytes <= maxBytes) {
            return;
        }

        std::sort(entries.begin(), entries.end(), [](CacheEntry const& lhs, CacheEntry const& rhs) {
            return lhs.mtime < rhs.mtime;
        });

        for (auto const& entry : entries) {
            if (totalBytes <= maxBytes) {
                break;
            }

            std::error_code rmEc;
            std::filesystem::remove(entry.path, rmEc);
            if (!rmEc) {
                totalBytes = (entry.size > totalBytes) ? 0 : (totalBytes - entry.size);
            }
        }
    }
}

ProfileThumbs& ProfileThumbs::get() {
    static ProfileThumbs inst; 
    static bool initialized = false;
    if (!initialized) {
        initialized = true;
        pruneProfileThumbsDiskCache();
    }
    return inst;
}

std::string ProfileThumbs::makePath(int accountID) const {
    auto dir = getProfileThumbsDir();
    (void)file::createDirectoryAll(dir);
    return geode::utils::string::pathToString(dir / fmt::format("{}.webp", accountID));
}

/// ruta legacy (.rgb) para migrar archivos antiguos
static std::string makeLegacyPath(int accountID) {
    auto dir = getProfileThumbsDir();
    return geode::utils::string::pathToString(dir / fmt::format("{}.rgb", accountID));
}

bool ProfileThumbs::saveRGB(int accountID, const uint8_t* rgb, int width, int height) {
    // Actualiza el cache en memoria

    if (!rgb || width <= 0 || height <= 0) return false;

    // Convierte RGB a RGBA
    size_t pixelCount = static_cast<size_t>(width) * height;
    std::vector<uint8_t> rgbaBuf(pixelCount * 4);
    ImageConverter::rgbToRgbaFast(rgb, rgbaBuf.data(), pixelCount);

    auto* tex = new CCTexture2D();
    if (tex->initWithData(rgbaBuf.data(), kCCTexture2DPixelFormat_RGBA8888, width, height, { (float)width, (float)height })) {
        tex->autorelease();

        auto path = makePath(accountID);

        // Guarda en disco en segundo plano como WebP
        spawnBackground([accountID, width, height, path, data = std::move(rgbaBuf)]() mutable {
            std::vector<uint8_t> encoded;
            if (ImageConverter::rgbaToWebpBuffer(data.data(), width, height, encoded, 85.f)) {
                std::ofstream out(path, std::ios::binary);
                if (out) {
                    out.write(reinterpret_cast<char const*>(encoded.data()), encoded.size());
                    out.close();
                    pruneProfileThumbsDiskCache();
                    log::debug("[ProfileThumbs] Saved profile WebP to disk for account {} ({} bytes)", accountID, encoded.size());
                }
            } else {
                // Fallback a PNG
                std::vector<uint8_t> pngData;
                if (ImageConverter::rgbaToPngBuffer(data.data(), width, height, pngData)) {
                    std::ofstream out(path, std::ios::binary);
                    if (out) {
                        out.write(reinterpret_cast<char const*>(pngData.data()), pngData.size());
                        out.close();
                        pruneProfileThumbsDiskCache();
                        log::debug("[ProfileThumbs] Saved profile PNG fallback for account {}", accountID);
                    }
                }
            }
        });

        // Actualiza cache preservando la config anterior
        ccColor3B cA = {255,255,255};
        ccColor3B cB = {255,255,255};
        float wF = 0.6f;
        
        auto it = m_profileCache.find(accountID);
        if (it != m_profileCache.end()) {
            cA = it->second.colorA;
            cB = it->second.colorB;
            wF = it->second.widthFactor;
        }
        
        this->cacheProfile(accountID, tex, cA, cB, wF);
        log::info("[ProfileThumbs] Memory cache updated for account {}", accountID);
    } else {
        tex->release();
        return false;
    }
    
    return true; 
}

bool ProfileThumbs::has(int accountID) const {
    std::error_code ec;
    if (std::filesystem::exists(makePath(accountID), ec)) return true;
    return std::filesystem::exists(makeLegacyPath(accountID), ec);
}

void ProfileThumbs::deleteProfile(int accountID) {
    clearCache(accountID);
    std::error_code ec;
    // Borra ambos formatos
    auto path = makePath(accountID);
    if (std::filesystem::exists(path, ec)) {
        std::filesystem::remove(path, ec);
        log::debug("[ProfileThumbs] Deleted profile WebP for account {}", accountID);
    }
    auto legacy = makeLegacyPath(accountID);
    if (std::filesystem::exists(legacy, ec)) {
        std::filesystem::remove(legacy, ec);
        log::debug("[ProfileThumbs] Deleted legacy .rgb for account {}", accountID);
    }
}

CCTexture2D* ProfileThumbs::loadTexture(int accountID) {
    auto path = makePath(accountID);     // .webp
    auto legacy = makeLegacyPath(accountID); // .rgb
    log::debug("[ProfileThumbs] Loading profile thumbnail for account {}: {}", accountID, path);
    
    std::error_code ec;
    bool hasNew = std::filesystem::exists(path, ec) && !ec;
    bool hasOld = !hasNew && std::filesystem::exists(legacy, ec) && !ec;

    if (!hasNew && !hasOld) {
        log::debug("[ProfileThumbs] Thumbnail not found for account {}", accountID);
        return nullptr;
    }

    // Formato nuevo (WebP/PNG)
    if (hasNew) {
        std::error_code sizeEc;
        auto fileSize = std::filesystem::file_size(path, sizeEc);
        if (sizeEc || fileSize == 0 || fileSize > kMaxProfileThumbFileBytes) {
            log::warn("[ProfileThumbs] Rejecting thumbnail for account {} due to invalid file size ({})", accountID, sizeEc ? 0 : fileSize);
            return nullptr;
        }

        std::ifstream in(path, std::ios::binary);
        if (!in) { log::error("[ProfileThumbs] Error opening file: {}", path); return nullptr; }
        std::vector<uint8_t> fileData((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();

        int w = 0, h = 0, ch = 0;
        unsigned char* px = stbi_load_from_memory(fileData.data(), static_cast<int>(fileData.size()), &w, &h, &ch, 4);
        if (px && w > 0 && h > 0 && w <= 4096 && h <= 4096) {
                auto* tex = new CCTexture2D();
                if (tex->initWithData(px, kCCTexture2DPixelFormat_RGBA8888,
                        w, h, { (float)w, (float)h })) {
                    tex->autorelease();
                    stbi_image_free(px);
                    log::info("[ProfileThumbs] Loaded thumbnail for account {} (stb_image)", accountID);
                    return tex;
                }
                tex->release();
        }
        if (px) stbi_image_free(px);
        log::warn("[ProfileThumbs] Failed to decode new-format file for account {}", accountID);
        return nullptr;
    }

    // Formato legacy (.rgb)
    std::ifstream in(legacy, std::ios::binary);
    if (!in) { log::error("[ProfileThumbs] Error opening legacy file: {}", legacy); return nullptr; }
    
    Header h{}; 
    in.read(reinterpret_cast<char*>(&h), sizeof(h));
    
    if (h.fmt != 24 || h.w <= 0 || h.h <= 0 || h.w > 4096 || h.h > 4096) {
        log::error("[ProfileThumbs] Invalid legacy header: fmt={}, w={}, h={}", h.fmt, h.w, h.h);
        return nullptr;
    }
    
    if (!in) {
        log::error("[ProfileThumbs] Legacy header read failed");
        return nullptr;
    }

    std::vector<uint8_t> buf(h.w * h.h * 3);
    in.read(reinterpret_cast<char*>(buf.data()), buf.size());

    if (in.gcount() != static_cast<std::streamsize>(buf.size())) {
        log::error("[ProfileThumbs] Legacy file truncated");
        return nullptr;
    }
    in.close();

    // Convierte RGB a RGBA
    std::vector<uint8_t> rgbaBuf(h.w * h.h * 4);
    ImageConverter::rgbToRgbaFast(buf.data(), rgbaBuf.data(), static_cast<size_t>(h.w) * h.h);

    auto* tex = new CCTexture2D();
    if (!tex->initWithData(rgbaBuf.data(), kCCTexture2DPixelFormat_RGBA8888, h.w, h.h, { (float)h.w, (float)h.h })) {
        log::error("[ProfileThumbs] Failed to create texture from legacy data");
        tex->release();
        return nullptr;
    }
    tex->autorelease();
    
    log::info("[ProfileThumbs] Loaded legacy .rgb thumbnail for account {}", accountID);
    return tex;
}

bool ProfileThumbs::loadRGB(int accountID, std::vector<uint8_t>& out, int& w, int& h) {
    // Intenta formato nuevo primero
    auto path = makePath(accountID);
    std::error_code ec;
    if (std::filesystem::exists(path, ec) && !ec) {
        auto fileSize = std::filesystem::file_size(path, ec);
        if (ec || fileSize == 0 || fileSize > kMaxProfileThumbFileBytes) {
            log::warn("[ProfileThumbs] Rejecting RGB load for account {} due to invalid file size ({})", accountID, ec ? 0 : fileSize);
            return false;
        }

        std::ifstream in(path, std::ios::binary);
        if (in) {
            std::vector<uint8_t> fileData((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            in.close();
            int sw = 0, sh = 0, sch = 0;
            unsigned char* px = stbi_load_from_memory(fileData.data(), static_cast<int>(fileData.size()), &sw, &sh, &sch, 4);
            if (px && sw > 0 && sh > 0 && sw <= 4096 && sh <= 4096) {
                w = sw; h = sh;
                // Convierte RGBA a RGB
                out.resize(static_cast<size_t>(sw) * sh * 3);
                ImageConverter::rgbaToRgbFast(px, out.data(), static_cast<size_t>(sw) * sh);
                stbi_image_free(px);
                return true;
            }
            if (px) stbi_image_free(px);
        }
    }
    // Fallback a .rgb
    auto legacy = makeLegacyPath(accountID);
    if (!std::filesystem::exists(legacy, ec) || ec) return false;
    std::ifstream in(legacy, std::ios::binary);
    if (!in) return false;
    Header head{}; in.read(reinterpret_cast<char*>(&head), sizeof(head));
    // Mismo clamp que loadTexture(): sin el limite de 4096, head.w * head.h * 3
    // (aritmetica int32) puede desbordar con un header corrupto y producir un
    // resize() con un tamano basura (bad_alloc o asignacion masiva).
    if (head.fmt != 24 || head.w <= 0 || head.h <= 0 || head.w > 4096 || head.h > 4096) return false;
    out.resize(static_cast<size_t>(head.w) * head.h * 3);
    in.read(reinterpret_cast<char*>(out.data()), out.size());
    w = head.w; h = head.h; return static_cast<bool>(in);
}

void ProfileThumbs::cacheProfile(int accountID, CCTexture2D* texture, 
                                 ccColor3B colorA, ccColor3B colorB, float widthFactor) {
    if (!texture) return;
    
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    
    // Limpia expiradas cada N inserciones
    m_insertsSinceCleanup++;
    if (m_insertsSinceCleanup >= CLEANUP_INTERVAL) {
        clearOldCache();
        m_insertsSinceCleanup = 0;
    }
    
    log::debug("[ProfileThumbs] Caching profile for account {} with colors RGB({},{},{}) -> RGB({},{},{}), width: {}", 
               accountID, colorA.r, colorA.g, colorA.b, colorB.r, colorB.g, colorB.b, widthFactor);
    
    // Preserva config y gifKey existentes
    ProfileConfig existingConfig;
    std::string existingGifKey;
    auto it = m_profileCache.find(accountID);
    if (it != m_profileCache.end()) {
        existingConfig = it->second.config;
        existingGifKey = it->second.gifKey;
    }

    m_profileCache[accountID] = ProfileCacheEntry(texture, colorA, colorB, widthFactor);
    m_profileCache[accountID].config = existingConfig;

    // Restaura la gifKey si existia
    if (!existingGifKey.empty()) {
        m_profileCache[accountID].gifKey = existingGifKey;
        log::debug("[ProfileThumbs] Preserved existing gifKey: {} for account {}", existingGifKey, accountID);
    }

    // Actualiza el orden LRU
    auto lruIt = m_lruMap.find(accountID);
    if (lruIt != m_lruMap.end()) {
        m_lruOrder.erase(lruIt->second);
    }
    m_lruOrder.push_back(accountID);
    m_lruMap[accountID] = std::prev(m_lruOrder.end());

    // Elimina entradas viejas si excede el limite
    while (m_profileCache.size() > MAX_PROFILE_CACHE_SIZE && !m_lruOrder.empty()) {
        int removeID = m_lruOrder.front();
        if (removeID == accountID) {
            // No elimina la entrada recien agregada
            break;
        }
        m_lruOrder.pop_front();
        m_lruMap.erase(removeID);
        auto evictIt = m_profileCache.find(removeID);
        if (evictIt != m_profileCache.end()) {
            if (!evictIt->second.gifKey.empty()) {
                AnimatedGIFSprite::unpinGIF(evictIt->second.gifKey);
            }
            m_profileCache.erase(evictIt);
        }
    }
}

void ProfileThumbs::cacheProfileGIF(int accountID, std::string const& gifKey, 
                                    cocos2d::ccColor3B colorA, cocos2d::ccColor3B colorB, float widthFactor) {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    
    // Limpia cache viejo periodicamente
    m_insertsSinceCleanup++;
    if (m_insertsSinceCleanup >= CLEANUP_INTERVAL) {
        clearOldCache();
        m_insertsSinceCleanup = 0;
    }
    
    log::debug("[ProfileThumbs] Caching GIF profile for account {} with key {}", accountID, gifKey);
    
    AnimatedGIFSprite::pinGIF(gifKey);

    // Preserva la config existente
    ProfileConfig existingConfig;
    auto it = m_profileCache.find(accountID);
    if (it != m_profileCache.end()) {
        existingConfig = it->second.config;
    }

    // Solo guarda la key del GIF (no la textura)
    
    m_profileCache[accountID] = ProfileCacheEntry(gifKey, colorA, colorB, widthFactor);
    m_profileCache[accountID].config = existingConfig;
}

void ProfileThumbs::cacheProfileConfig(int accountID, ProfileConfig const& config) {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    auto it = m_profileCache.find(accountID);
    if (it != m_profileCache.end()) {
        it->second.config = config;
    } else {
        // Crea entrada vacia con solo config
        ProfileCacheEntry entry;
        entry.config = config;
        m_profileCache[accountID] = std::move(entry);
    }
}

ProfileConfig ProfileThumbs::getProfileConfig(int accountID) {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    auto it = m_profileCache.find(accountID);
    if (it != m_profileCache.end()) {
        ProfileConfig config = it->second.config;
        // Incluye la gifKey en la config
        if (!it->second.gifKey.empty()) {
            config.gifKey = it->second.gifKey;
        }
        return config;
    }
    return ProfileConfig();
}

std::optional<ProfileCacheEntry> ProfileThumbs::getCachedProfile(int accountID) {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    auto it = m_profileCache.find(accountID);
    if (it == m_profileCache.end()) {
        return std::nullopt;
    }

    // Revisa si expiro el cache
    auto now = std::chrono::steady_clock::now();
    if (now - it->second.timestamp > CACHE_DURATION) {
        log::debug("[ProfileThumbs] Cache expired for account {}", accountID);
        // Limpia del orden LRU
        auto lruIt = m_lruMap.find(accountID);
        if (lruIt != m_lruMap.end()) {
            m_lruOrder.erase(lruIt->second);
            m_lruMap.erase(lruIt);
        }
        m_profileCache.erase(it);
        return std::nullopt;
    }

    // Marca como recientemente usado
    auto lruIt = m_lruMap.find(accountID);
    if (lruIt != m_lruMap.end()) {
        m_lruOrder.erase(lruIt->second);
    }
    m_lruOrder.push_back(accountID);
    m_lruMap[accountID] = std::prev(m_lruOrder.end());

    log::debug("[ProfileThumbs] Cache found for account {}", accountID);
    return it->second;
}

void ProfileThumbs::clearCache(int accountID) {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    auto it = m_profileCache.find(accountID);
    if (it != m_profileCache.end()) {
        log::debug("[ProfileThumbs] Clearing cache for account {}", accountID);
        m_profileCache.erase(it);
        auto lruIt = m_lruMap.find(accountID);
        if (lruIt != m_lruMap.end()) {
            m_lruOrder.erase(lruIt->second);
            m_lruMap.erase(lruIt);
        }
    }
    removeFromNoProfileCache(accountID);
}

// El caller debe tener el mutex bloqueado
void ProfileThumbs::clearOldCache() {
    auto now = std::chrono::steady_clock::now();
    
    for (auto it = m_profileCache.begin(); it != m_profileCache.end();) {
        if (now - it->second.timestamp > CACHE_DURATION) {
            log::debug("[ProfileThumbs] Removing old cache for account {}", it->first);
            if (!it->second.gifKey.empty()) {
                AnimatedGIFSprite::unpinGIF(it->second.gifKey);
            }
            auto lruIt = m_lruMap.find(it->first);
            if (lruIt != m_lruMap.end()) {
                m_lruOrder.erase(lruIt->second);
                m_lruMap.erase(lruIt);
            }
            it = m_profileCache.erase(it);
        } else {
            ++it;
        }
    }
}

void ProfileThumbs::clearAllCache() {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    log::info("[ProfileThumbs] Clearing all profile cache ({} entries)", m_profileCache.size());
    for (auto const& [id, entry] : m_profileCache) {
        if (!entry.gifKey.empty()) {
            AnimatedGIFSprite::unpinGIF(entry.gifKey);
        }
    }
    m_profileCache.clear();
    m_lruOrder.clear();
    m_lruMap.clear();
    m_noProfileCache.clear();
}

void ProfileThumbs::markNoProfile(int accountID) {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    if (m_noProfileCache.size() >= MAX_NO_PROFILE_CACHE_SIZE && !m_noProfileCache.empty()) {
        m_noProfileCache.erase(m_noProfileCache.begin());
    }
    m_noProfileCache.insert(accountID);
}

void ProfileThumbs::removeFromNoProfileCache(int accountID) {
    m_noProfileCache.erase(accountID);
}

bool ProfileThumbs::isNoProfile(int accountID) const {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    return m_noProfileCache.find(accountID) != m_noProfileCache.end();
}

void ProfileThumbs::clearNoProfileCache() {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    m_noProfileCache.clear();
}

void ProfileThumbs::clearPendingDownloads() {
    m_pendingCallbacks.clear();
    m_downloadQueue.clear();
    m_binaryQueue.clear();
    m_batchConfigs.clear();
    m_batchInFlight = false;
    m_usernameMap.clear();
    m_activeDownloads = 0;
}

void ProfileThumbs::spawnBackground(std::function<void()> job) {
    std::lock_guard<std::mutex> lock(m_workerMutex);
    if (!m_workerPool) {
        // Pool con 2 threads — suficiente para encode WebP + escritura disco
        // sin saturar el bus de I/O. Lazy init: solo si efectivamente se usa.
        m_workerPool = std::make_unique<paimon::ThreadPool>(2, "PaimonProfileThumbsBG");
    }
    m_workerPool->enqueue(std::move(job));
}

void ProfileThumbs::pruneFinishedWorkers() {
    // No-op con ThreadPool — el pool maneja su propia ciclo de vida.
}

void ProfileThumbs::waitBackgroundWorkers() {
    std::unique_ptr<paimon::ThreadPool> poolToShutdown;
    {
        std::lock_guard<std::mutex> lock(m_workerMutex);
        poolToShutdown = std::move(m_workerPool);
    }
    if (poolToShutdown) {
        // ThreadPool::shutdown() ya usa timedJoin con 3s — descarta jobs
        // pendientes y solo espera a los actualmente en ejecucion.
        poolToShutdown->shutdown();
    }
}

void ProfileThumbs::shutdown() {
    waitBackgroundWorkers();
}

CCNode* ProfileThumbs::createProfileNode(CCTexture2D* texture, ProfileConfig const& config, CCSize cs, bool onlyBackground) {
    // si tenemos gifKey intento crear un AnimatedGIFSprite
    AnimatedGIFSprite* gifSprite = nullptr;
    if (!config.gifKey.empty()) {
        // primero pruebo a crearlo desde el cache
        if (AnimatedGIFSprite::isCached(config.gifKey)) {
            gifSprite = AnimatedGIFSprite::createFromCache(config.gifKey);
        }
    }

    if (!texture && !gifSprite) return nullptr;

    // nodo contenedor
    auto container = CCNode::create();
    container->setContentSize(cs);

    CCNode* bg = nullptr;

    // tipo de fondo final que se va a usar
    std::string bgType = config.backgroundType;

    // "icon-gradient" se trata como un degradado normal pero NO debe ser
    // pisado por el modo "thumbnail" aunque exista textura/gif: el usuario
    // pidio explicitamente un degradado basado en sus iconos y eso manda.
    // Si no hay textura/gif podemos cargar el flujo de gradiente igual que
    // antes.
    if (bgType == "icon-gradient") {
        bgType = "gradient";
    } else if ((onlyBackground || bgType == "gradient") && (texture || !config.gifKey.empty())) {
        // fuerzo modo thumbnail si hay textura/GIF y:
        // 1. estamos en modo banner (onlyBackground=true)
        // 2. o config viene en "gradient" por defecto
        bgType = "thumbnail";
    }

    if (bgType == "thumbnail") {
        if (gifSprite) {
            auto bgSprite = AnimatedGIFSprite::createFromCache(config.gifKey);
            if (bgSprite) {
                CCSize targetSize = cs;
                targetSize.width = std::max(targetSize.width, 512.f);
                targetSize.height = std::max(targetSize.height, 256.f);
                
                float scaleX = targetSize.width / gifSprite->getContentSize().width;
                float scaleY = targetSize.height / gifSprite->getContentSize().height;
                float scale = std::max(scaleX, scaleY);
                
                bgSprite->setScale(scale);
                bgSprite->setPosition(targetSize * 0.5f);
                
                auto shader = BlurSystem::getInstance()->getRealtimeBlurShader();
                if (!shader) shader = paimon::shaders::getBlurFastShader();
                if (shader) {
                    bgSprite->setShaderProgram(shader);
                }

                auto stencil = PaimonDrawNode::create();
                CCPoint rect[4];
                rect[0] = ccp(0, 0);
                rect[1] = ccp(cs.width, 0);
                rect[2] = ccp(cs.width, cs.height);
                rect[3] = ccp(0, cs.height);
                ccColor4F white = {1, 1, 1, 1};
                stencil->drawPolygon(rect, 4, white, 0, white);
                
                auto clipper = CCClippingNode::create(stencil);
                clipper->setAlphaThreshold(0.05f);
                clipper->setContentSize(cs);
                clipper->setPosition({0,0});
                clipper->setZOrder(-10);
                
                bgSprite->setPosition(cs / 2);
                clipper->addChild(bgSprite);
                bg = clipper;
                
                if (config.darkness > 0.0f) {
                    auto overlay = CCLayerColor::create({0, 0, 0, static_cast<GLubyte>(config.darkness * 255)});
                    overlay->setContentSize(cs);
                    overlay->setPosition({0, 0});
                    overlay->setZOrder(-5); 
                    container->addChild(overlay);
                }
            }
        } else if (texture) {
            CCSize targetSize = cs;
            targetSize.width = std::max(targetSize.width, 512.f);
            targetSize.height = std::max(targetSize.height, 256.f);

            CCSprite* bgSprite = BlurSystem::getInstance()->createBlurredSprite(texture, targetSize, config.blurIntensity);
            if (!bgSprite) bgSprite = CCSprite::createWithTexture(texture);

            if (bgSprite) {
                // clipper para el fondo estatico
                auto stencil = PaimonDrawNode::create();
                CCPoint rect[4];
                rect[0] = ccp(0, 0);
                rect[1] = ccp(cs.width, 0);
                rect[2] = ccp(cs.width, cs.height);
                rect[3] = ccp(0, cs.height);
                ccColor4F white = {1, 1, 1, 1};
                stencil->drawPolygon(rect, 4, white, 0, white);
                
                auto clipper = CCClippingNode::create(stencil);
                clipper->setAlphaThreshold(0.05f);
                clipper->setContentSize(cs);
                clipper->setPosition({0,0});
                clipper->setZOrder(-10); // bien detras de todo
                
                float targetW = cs.width;
                float targetH = cs.height;
                float finalScale = std::max(
                    targetW / bgSprite->getContentSize().width,
                    targetH / bgSprite->getContentSize().height
                );
                bgSprite->setScale(finalScale);
                bgSprite->setPosition(cs / 2);
                
                clipper->addChild(bgSprite);
                bg = clipper;

                // capa oscura extra por encima
                if (config.darkness > 0.0f) {
                    auto overlay = CCLayerColor::create({0, 0, 0, static_cast<GLubyte>(config.darkness * 255)});
                    overlay->setContentSize(cs);
                    overlay->setPosition({0, 0});
                    overlay->setZOrder(-5); 
                    container->addChild(overlay);
                }
            }
        }
    } else if (bgType != "none") {
        // degradado o color solido
        if (config.useGradient) {
            auto grad = CCLayerGradient::create(
                ccc4(config.colorA.r, config.colorA.g, config.colorA.b, 255),
                ccc4(config.colorB.r, config.colorB.g, config.colorB.b, 255)
            );
            grad->setContentSize(cs);
            grad->setAnchorPoint({0,0});
            grad->setPosition({0,0});
            grad->setVector({1.f, 0.f}); // horizontal
            grad->setZOrder(-10);
            bg = grad;
        } else {
            auto solid = CCLayerColor::create(ccc4(config.colorA.r, config.colorA.g, config.colorA.b, 255));
            solid->setContentSize(cs);
            solid->setAnchorPoint({0,0});
            solid->setPosition({0,0});
            solid->setZOrder(-10);
            bg = solid;
        }
    }

    if (bg) {
        container->addChild(bg);
    }

    if (onlyBackground) {
        return container;
    }

    CCNode* mainSprite = nullptr;
    float contentW = 0, contentH = 0;

    if (gifSprite) {
        mainSprite = gifSprite;
        contentW = gifSprite->getContentSize().width;
        contentH = gifSprite->getContentSize().height;
        // me aseguro de que el GIF este actualizando
        gifSprite->scheduleUpdate();
    } else if (texture) {
        auto s = CCSprite::createWithTexture(texture);
        mainSprite = s;
        contentW = s->getContentWidth();
        contentH = s->getContentHeight();
    }

    if (mainSprite && contentW > 0 && contentH > 0) {
        float factor = 0.60f;
        if (config.hasConfig) {
            factor = config.widthFactor;
        } else {
            factor = Mod::get()->getSavedValue<float>("profile-thumb-width", 0.6f);
        }
        factor = std::max(0.30f, std::min(0.95f, factor));
        float desiredWidth = cs.width * factor;

        float scaleY = cs.height / contentH;
        float scaleX = desiredWidth / contentW;

        mainSprite->setScaleY(scaleY);
        mainSprite->setScaleX(scaleX);
        
        // clipping inclinado estilo “banner”
        constexpr float angle = 18.f;
        CCSize scaledSize{ desiredWidth, contentH * scaleY };
        auto mask = PaimonDrawNode::create();
        {
            CCPoint rect[4] = { ccp(0,0), ccp(scaledSize.width,0), ccp(scaledSize.width,scaledSize.height), ccp(0,scaledSize.height) };
            ccColor4F white = {1,1,1,1};
            mask->drawPolygon(rect, 4, white, 0, white);
        }
        mask->setContentSize(scaledSize);
        mask->setAnchorPoint({1,0});
        mask->ignoreAnchorPointForPosition(true);
        mask->setSkewX(angle);

        auto clip = CCClippingNode::create();
        clip->setStencil(mask);
        clip->setAlphaThreshold(0.5f);
        clip->setContentSize(scaledSize);
        clip->setAnchorPoint({1,0});
        
        // pego el clip al lado derecho
        clip->setPosition({cs.width, 0});
        clip->setZOrder(10); // lo dejo por encima del fondo
        
        // ajusto posicion del sprite dentro del clip
        mainSprite->setAnchorPoint({1,0});
        mainSprite->setPosition({scaledSize.width, 0});
        
        clip->addChild(mainSprite);
        container->addChild(clip);
        
        // linea separadora
        auto separator = CCLayerColor::create({
            config.separatorColor.r, 
            config.separatorColor.g, 
            config.separatorColor.b, 
            (GLubyte)std::clamp(config.separatorOpacity, 0, 255)
        });
        separator->setContentSize({2.0f, cs.height});
        separator->setAnchorPoint({0.5f, 0});
        separator->setSkewX(angle);
        separator->setPosition({cs.width - desiredWidth, 0});
        separator->setZOrder(15); // por encima del clip
        container->addChild(separator);
    }

    return container;
}

void ProfileThumbs::queueLoad(int accountID, std::string const& username, geode::CopyableFunction<void(bool, cocos2d::CCTexture2D*)> callback) {
    if (ProfileThumbs::s_shutdownMode.load(std::memory_order_acquire)) {
        if (callback) callback(false, nullptr);
        return;
    }
    // 0. miro cache negativa (si ya fallo antes, ni lo intento en esta sesion)
    if (isNoProfile(accountID)) {
        if (callback) callback(false, nullptr);
        return;
    }

    // 1. miro cache primero
    auto cached = getCachedProfile(accountID);
    if (cached) {
        if (cached->texture) {
            if (callback) callback(true, cached->texture);
            return;
        }
        if (!cached->gifKey.empty() && AnimatedGIFSprite::isCached(cached->gifKey)) {
            if (callback) callback(true, nullptr);
            return;
        }
    }

    // 2. si ya esta en cola, solo apilo el callback
    if (m_pendingCallbacks.find(accountID) != m_pendingCallbacks.end()) {
        m_pendingCallbacks[accountID].push_back(callback);
        return;
    }

    // 3. lo meto en la cola (FIFO, al final)
    // asi la lista carga de arriba a abajo, y la visibilidad afina el orden
    m_downloadQueue.push_back(accountID);
    m_pendingCallbacks[accountID].push_back(callback);
    
    // guardo username asociado a esta peticion
    m_usernameMap[accountID] = username;

    // 4. arranco el procesado de la cola
    processQueue();
}

void ProfileThumbs::notifyVisible(int accountID) {
    m_visibilityMap[accountID] = std::chrono::steady_clock::now();
}

void ProfileThumbs::processQueue() {
    if (ProfileThumbs::s_shutdownMode.load(std::memory_order_acquire)) return;
    // si ya hay un batch en vuelo, espero a que termine
    if (m_batchInFlight) return;

    // si hay binarios pendientes del batch anterior, los proceso primero
    if (!m_binaryQueue.empty()) {
        processBinaryQueue();
        return;
    }

    // si no hay nada en la cola principal, nada que hacer
    if (m_downloadQueue.empty()) return;

    // Perfiles visibles recientes primero (notifyVisible).
    // Partition visible to front (O(n)) instead of full sort (O(n log n))
    std::stable_partition(m_downloadQueue.begin(), m_downloadQueue.end(),
        [this](int id) {
            return m_visibilityMap.find(id) != m_visibilityMap.end();
        });

    // dreno hasta 16 IDs de la cola para el batch (limite del servidor por
    // budget de subrequests del Worker Free). El servidor descarta lo que
    // venga arriba, asi que mandar mas seria desperdicio de bandwidth.
    static constexpr int MAX_BATCH_SIZE = 16;
    std::vector<int> batchIDs;
    while (!m_downloadQueue.empty() && static_cast<int>(batchIDs.size()) < MAX_BATCH_SIZE) {
        batchIDs.push_back(m_downloadQueue.front());
        m_downloadQueue.pop_front();
    }

    m_batchInFlight = true;
    log::info("[ProfileThumbs] Batch check: {} accounts", batchIDs.size());

    ThumbnailAPI::get().batchCheckProfiles(batchIDs,
        [this, batchIDs](bool success,
                         std::unordered_set<int> const& found,
                         std::unordered_map<int, ProfileConfig> const& configs) {
            m_batchInFlight = false;

            if (ProfileThumbs::s_shutdownMode.load(std::memory_order_acquire)) return;

            if (!success) {
                // fallback: si el endpoint no existe aun o fallo,
                // meto todo en la cola binaria y bajo uno a uno (sin config batch)
                log::warn("[ProfileThumbs] Batch check failed, fallback to individual downloads");
                for (int id : batchIDs) {
                    m_binaryQueue.push_back(id);
                }
                processBinaryQueue();
                return;
            }

            // los que NO tienen perfil: notifico y marco cache negativa de golpe
            for (int id : batchIDs) {
                if (found.count(id) == 0) {
                    markNoProfile(id);
                    auto it = m_pendingCallbacks.find(id);
                    if (it != m_pendingCallbacks.end()) {
                        dispatchProfileThumbCallbacks(std::move(it->second), false, nullptr);
                        m_pendingCallbacks.erase(it);
                    }
                }
            }

            // cacheo las configs que vinieron en el batch (ahorro 1 request por perfil)
            for (auto& [id, config] : configs) {
                m_batchConfigs[id] = config;
                cacheProfileConfig(id, config);
            }

            // meto los que SI existen en la cola binaria
            for (int id : batchIDs) {
                if (found.count(id)) {
                    m_binaryQueue.push_back(id);
                }
            }

            log::info("[ProfileThumbs] Batch result: {} found, {} not found, saved {} config requests",
                found.size(), batchIDs.size() - found.size(), configs.size());

            // arranco descargas binarias solo de los que existen
            processBinaryQueue();

            // si no hubo binarios (0 found) y quedan IDs pendientes, proceso el siguiente batch
            if (!m_downloadQueue.empty()) {
                Loader::get()->queueInMainThread([this]() {
                    if (profileThumbsShouldAbort()) return;
                    processQueue();
                });
            }
        });
}

void ProfileThumbs::processBinaryQueue() {
    if (ProfileThumbs::s_shutdownMode.load(std::memory_order_acquire)) return;
    // Si solo queda 1 en la cola y ya hay descargas activas, dejamos el flujo individual.
    // En cuanto haya >=2 items, aprovechamos /api/profilebackground/batch para ahorrar requests.
    if (m_binaryQueue.empty() || m_activeDownloads >= MAX_CONCURRENT_DOWNLOADS) return;

    if (m_binaryQueue.size() >= 2) {
        // Batch path
        // Drenamos hasta 40 IDs (cap del server). Solo 1 batch en vuelo a la vez,
        // contabilizado como "1 active download" para no bloquear la cola.
        static constexpr size_t BATCH_CAP = 40;
        std::vector<int> batchIDs;
        std::vector<std::string> batchUsernames;
        size_t take = std::min<size_t>(m_binaryQueue.size(), BATCH_CAP);
        batchIDs.reserve(take);
        batchUsernames.reserve(take);
        for (size_t i = 0; i < take; ++i) {
            int id = m_binaryQueue.front();
            m_binaryQueue.pop_front();
            batchIDs.push_back(id);
            std::string uname;
            auto uit = m_usernameMap.find(id);
            if (uit != m_usernameMap.end()) {
                uname = uit->second;
                m_usernameMap.erase(uit);
            }
            batchUsernames.push_back(std::move(uname));
        }

        m_activeDownloads++;
        log::info("[ProfileThumbs] batch background download: {} ids", batchIDs.size());

        // Snapshot de configs vino-en-batch (los iremos consumiendo por id).
        std::unordered_map<int, ProfileConfig> snappedConfigs;
        for (int id : batchIDs) {
            auto it = m_batchConfigs.find(id);
            if (it != m_batchConfigs.end()) {
                snappedConfigs.emplace(id, it->second);
                m_batchConfigs.erase(it);
            }
        }

        HttpClient::get().downloadProfileBackgroundsBatch(batchIDs,
            [this, batchIDs, snappedConfigs = std::move(snappedConfigs)]
            (bool success, std::unordered_map<int, HttpClient::BatchItem> const& items) {
                if (ProfileThumbs::s_shutdownMode.load(std::memory_order_acquire)) {
                    m_activeDownloads = std::max(0, m_activeDownloads - 1);
                    return;
                }

                // Para cada ID del batch original, dispatch de imagen + finalizar callback.
                // Los que fallaron vuelven al fallback individual.
                std::vector<int> fallbackIds;
                fallbackIds.reserve(4);

                for (int accountID : batchIDs) {
                    auto itItem = items.find(accountID);
                    bool itemOk = (itItem != items.end()) && itItem->second.ok && !itItem->second.data.empty();

                    if (!success || !itemOk) {
                        // Servidor no devolvio bytes: requeue para fallback individual.
                        fallbackIds.push_back(accountID);
                        continue;
                    }

                    ProfileConfig cfg;
                    bool hasCfg = false;
                    auto cit = snappedConfigs.find(accountID);
                    if (cit != snappedConfigs.end()) {
                        cfg = cit->second;
                        hasCfg = true;
                    }

                    // Decode bytes (puede ser GIF/MP4/static — el helper lo maneja).
                    ProfileImageService::processProfileBackgroundBytes(accountID, itItem->second.data,
                        [this, accountID, cfg, hasCfg](bool decodeOk, CCTexture2D* texture) {
                            if (ProfileThumbs::s_shutdownMode.load(std::memory_order_acquire)) return;

                            Ref<CCTexture2D> texRef = texture;

                            auto finalize = [this, accountID, decodeOk, texRef]
                                (bool configSuccess, ProfileConfig const& finalConfig) {
                                if (decodeOk && texRef) {
                                    this->cacheProfile(accountID, texRef, finalConfig.colorA,
                                        finalConfig.colorB, finalConfig.widthFactor);
                                }
                                if (configSuccess) {
                                    this->cacheProfileConfig(accountID, finalConfig);
                                }
                                if (!decodeOk && !configSuccess) {
                                    markNoProfile(accountID);
                                }
                                auto it = m_pendingCallbacks.find(accountID);
                                if (it != m_pendingCallbacks.end()) {
                                    dispatchProfileThumbCallbacks(std::move(it->second), decodeOk, texRef);
                                    m_pendingCallbacks.erase(it);
                                }
                            };

                            if (hasCfg) {
                                finalize(true, cfg);
                            } else {
                                ThumbnailAPI::get().downloadProfileConfig(accountID,
                                    [finalize](bool cs, ProfileConfig const& dlc) {
                                        finalize(cs, dlc);
                                    });
                            }
                        });
                }

                // El batch contó como 1 download — ya termino para el accounting de la cola.
                m_activeDownloads = std::max(0, m_activeDownloads - 1);

                // Re-queue de los que fallaron, para fallback individual.
                if (!fallbackIds.empty()) {
                    Loader::get()->queueInMainThread([this, fallbackIds]() {
                        if (profileThumbsShouldAbort()) return;
                        for (int id : fallbackIds) {
                            m_binaryQueue.push_back(id);
                        }
                        // Re-disparamos uno por uno por la ruta legacy (profileImg).
                        // Para evitar loops infinitos si tambien falla el individual,
                        // procesamos como en el path antiguo de a uno.
                        processBinaryQueueIndividual();
                    });
                }

                // Si quedan IDs en la cola principal o secundaria, seguimos.
                if (!profileThumbsShouldAbort()) {
                    Loader::get()->queueInMainThread([this]() {
                        if (profileThumbsShouldAbort()) return;
                        if (!m_binaryQueue.empty()) processBinaryQueue();
                        if (!m_downloadQueue.empty()) processQueue();
                    });
                }
            });

        return; // batch en vuelo, salimos
    }

    // Single path (1 elemento)
    processBinaryQueueIndividual();
}

void ProfileThumbs::processBinaryQueueIndividual() {
    while (m_activeDownloads < MAX_CONCURRENT_DOWNLOADS && !m_binaryQueue.empty()) {
        m_activeDownloads++;

        int accountID = m_binaryQueue.front();
        m_binaryQueue.pop_front();

        log::debug("[ProfileThumbs] Binary download (individual): AccountID {}", accountID);

        std::string username;
        if (m_usernameMap.find(accountID) != m_usernameMap.end()) {
            username = m_usernameMap[accountID];
            m_usernameMap.erase(accountID);
        }

        // si la config ya vino en el batch, la uso y me ahorro el request individual
        ProfileConfig config;
        bool hasConfig = false;
        auto cfgIt = m_batchConfigs.find(accountID);
        if (cfgIt != m_batchConfigs.end()) {
            config = cfgIt->second;
            hasConfig = true;
            m_batchConfigs.erase(cfgIt);
        }

        ThumbnailAPI::get().downloadProfile(accountID, username,
            [this, accountID, config, hasConfig](bool success, CCTexture2D* texture) {
                if (ProfileThumbs::s_shutdownMode.load(std::memory_order_acquire)) {
                    m_activeDownloads = std::max(0, m_activeDownloads - 1);
                    return;
                }

                Ref<CCTexture2D> texRef = texture;

                // funcion que notifica callbacks y sigue con la cola
                auto finalize = [this, accountID, success, texRef](bool configSuccess, ProfileConfig const& finalConfig) {
                    if (success && texRef) {
                        this->cacheProfile(accountID, texRef, finalConfig.colorA, finalConfig.colorB, finalConfig.widthFactor);
                    }
                    if (configSuccess) {
                        this->cacheProfileConfig(accountID, finalConfig);
                    }
                    if (!success && !configSuccess) {
                        markNoProfile(accountID);
                    }

                    auto it = m_pendingCallbacks.find(accountID);
                    if (it != m_pendingCallbacks.end()) {
                        dispatchProfileThumbCallbacks(std::move(it->second), success, texRef);
                        m_pendingCallbacks.erase(it);
                    }

                    m_activeDownloads--;

                    if (!profileThumbsShouldAbort()) {
                        Loader::get()->queueInMainThread([this]() {
                            if (profileThumbsShouldAbort()) return;
                            processBinaryQueue();
                            if (!m_downloadQueue.empty()) processQueue();
                        });
                    }
                };

                if (hasConfig) {
                    // ya tengo la config del batch, no hago request extra
                    finalize(true, config);
                } else {
                    // fallback: si no vino config en batch, la bajo individual
                    ThumbnailAPI::get().downloadProfileConfig(accountID,
                        [finalize](bool configSuccess, ProfileConfig const& dlConfig) {
                            finalize(configSuccess, dlConfig);
                        });
                }
            });
    }
}
