#include "ProfileThumbs.hpp"
#include "../../../managers/ThumbnailAPI.hpp"
#include "../../../core/Settings.hpp"
#include "../../../utils/AnimatedGIFSprite.hpp"
#include "../../../core/QualityConfig.hpp"
#include "../../../utils/ImageConverter.hpp"
#include "../../../utils/PaimonDrawNode.hpp"
#include <Geode/utils/file.hpp>
#include <Geode/utils/string.hpp>
#include <filesystem>
#include "../../../utils/TimedJoin.hpp"
#include <Geode/loader/Mod.hpp>
#include <fstream>
#include <algorithm>
#include <deque>
#include <future>

#include "../../../utils/Shaders.hpp"
#include "../../../blur/BlurSystem.hpp"
#include <prevter.imageplus/include/events.hpp>

using namespace geode::prelude;
using namespace cocos2d;
using namespace Shaders;




namespace {
    struct Header { int32_t w; int32_t h; int32_t fmt; };
    constexpr uintmax_t kMaxProfileThumbFileBytes = 20ull * 1024ull * 1024ull;

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

        auto res = imgp::tryDecode(fileData.data(), fileData.size());
        if (res.isOk()) {
            auto& decoded = res.unwrap();
            if (auto* img = std::get_if<imgp::DecodedImage>(&decoded)) {
                if (img->width > 0 && img->height > 0 && img->width <= 4096 && img->height <= 4096) {
                auto* tex = new CCTexture2D();
                if (tex->initWithData(img->data.get(), kCCTexture2DPixelFormat_RGBA8888,
                        img->width, img->height, { (float)img->width, (float)img->height })) {
                    tex->autorelease();
                    log::info("[ProfileThumbs] Loaded WebP/PNG thumbnail for account {}", accountID);
                    return tex;
                }
                tex->release();
                }
            }
        }
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
    
    std::vector<uint8_t> buf(h.w * h.h * 3);
    in.read(reinterpret_cast<char*>(buf.data()), buf.size());
    in.close();

    if (in.gcount() != static_cast<std::streamsize>(buf.size())) {
        log::error("[ProfileThumbs] Legacy file truncated");
        return nullptr;
    }

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
            auto res = imgp::tryDecode(fileData.data(), fileData.size());
            if (res.isOk()) {
                if (auto* img = std::get_if<imgp::DecodedImage>(&res.unwrap())) {
                    if (img->width > 0 && img->height > 0 && img->width <= 4096 && img->height <= 4096) {
                    w = img->width; h = img->height;
                    // Convierte RGBA a RGB
                    out.resize(w * h * 3);
                    for (int i = 0; i < w * h; ++i) {
                        out[i * 3 + 0] = img->data[i * 4 + 0];
                        out[i * 3 + 1] = img->data[i * 4 + 1];
                        out[i * 3 + 2] = img->data[i * 4 + 2];
                    }
                    return true;
                    }
                }
            }
        }
    }
    // Fallback a .rgb
    auto legacy = makeLegacyPath(accountID);
    if (!std::filesystem::exists(legacy, ec) || ec) return false;
    std::ifstream in(legacy, std::ios::binary);
    if (!in) return false;
    Header head{}; in.read(reinterpret_cast<char*>(&head), sizeof(head));
    if (head.fmt != 24 || head.w <= 0 || head.h <= 0) return false;
    out.resize(head.w * head.h * 3);
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

// Devuelve un puntero temporal al cache. Usarlo inmediatamente.
ProfileCacheEntry* ProfileThumbs::getCachedProfile(int accountID) {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    auto it = m_profileCache.find(accountID);
    if (it == m_profileCache.end()) {
        return nullptr;
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
        return nullptr;
    }
    
    // Marca como recientemente usado
    auto lruIt = m_lruMap.find(accountID);
    if (lruIt != m_lruMap.end()) {
        m_lruOrder.erase(lruIt->second);
    }
    m_lruOrder.push_back(accountID);
    m_lruMap[accountID] = std::prev(m_lruOrder.end());
    
    log::debug("[ProfileThumbs] Cache found for account {}", accountID);
    return &it->second;
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
    if (m_noProfileCache.size() >= MAX_NO_PROFILE_CACHE_SIZE) {
        m_noProfileCache.clear();
    }
    m_noProfileCache.insert(accountID);
}

void ProfileThumbs::removeFromNoProfileCache(int accountID) {
    m_noProfileCache.erase(accountID);
}

bool ProfileThumbs::isNoProfile(int accountID) const {
    return m_noProfileCache.find(accountID) != m_noProfileCache.end();
}

void ProfileThumbs::clearNoProfileCache() {
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
    pruneFinishedWorkers();
    m_backgroundWorkers.emplace_back(std::async(std::launch::async, [job = std::move(job)]() mutable {
        job();
    }));
}

void ProfileThumbs::pruneFinishedWorkers() {
    auto it = m_backgroundWorkers.begin();
    while (it != m_backgroundWorkers.end()) {
        if (!it->valid() || it->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            it = m_backgroundWorkers.erase(it);
        } else {
            ++it;
        }
    }
}

void ProfileThumbs::waitBackgroundWorkers() {
    std::vector<std::future<void>> workers;
    {
        std::lock_guard<std::mutex> lock(m_workerMutex);
        workers.swap(m_backgroundWorkers);
    }
    for (auto& worker : workers) {
        if (worker.valid()) {
            paimon::timedWait(worker, std::chrono::seconds(3));
        }
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

    // --- logica del fondo ---
    CCNode* bg = nullptr;

    // tipo de fondo final que se va a usar
    std::string bgType = config.backgroundType;
    
    // fuerzo modo thumbnail si hay textura/GIF y:
    // 1. estamos en modo banner (onlyBackground=true)
    // 2. o config viene en "gradient" por defecto
    if ((onlyBackground || bgType == "gradient") && (texture || !config.gifKey.empty())) {
        bgType = "thumbnail";
    }

    if (bgType == "thumbnail") {
        if (gifSprite) {
            // --- fondo con GIF (blur en tiempo real) ---
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
                
                // Shader de blur rapido
                auto shader = BlurSystem::getInstance()->getRealtimeBlurShader();
                if (!shader) shader = getOrCreateShader("fast-blur", vertexShaderCell, fragmentShaderFastBlur);
                if (shader) {
                    bgSprite->setShaderProgram(shader);
                }
                
                // clipper para que no se salga del rectangulo
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

    // --- sprite de perfil ---
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
    // 0. miro cache negativa (si ya fallo antes, ni lo intento en esta sesion)
    if (isNoProfile(accountID)) {
        if (callback) callback(false, nullptr);
        return;
    }

    // 1. miro cache primero
    auto cached = getCachedProfile(accountID);
    if (cached && cached->texture) {
        if (callback) callback(true, cached->texture);
        return;
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
    // si ya hay un batch en vuelo, espero a que termine
    if (m_batchInFlight) return;

    // si hay binarios pendientes del batch anterior, los proceso primero
    if (!m_binaryQueue.empty()) {
        processBinaryQueue();
        return;
    }

    // si no hay nada en la cola principal, nada que hacer
    if (m_downloadQueue.empty()) return;

    // dreno hasta 100 IDs de la cola para el batch (limite del servidor)
    static constexpr int MAX_BATCH_SIZE = 100;
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
                        for (auto const& cb : it->second) {
                            if (cb) cb(false, nullptr);
                        }
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
                    if (!ProfileThumbs::s_shutdownMode.load(std::memory_order_acquire)) {
                        processQueue();
                    }
                });
            }
        });
}

void ProfileThumbs::processBinaryQueue() {
    while (m_activeDownloads < MAX_CONCURRENT_DOWNLOADS && !m_binaryQueue.empty()) {
        m_activeDownloads++;

        int accountID = m_binaryQueue.front();
        m_binaryQueue.pop_front();

        log::debug("[ProfileThumbs] Binary download: AccountID {}", accountID);

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
                        for (auto const& cb : it->second) {
                            if (cb) cb(success, texRef);
                        }
                        m_pendingCallbacks.erase(it);
                    }

                    m_activeDownloads--;

                    if (!ProfileThumbs::s_shutdownMode.load(std::memory_order_acquire)) {
                        Loader::get()->queueInMainThread([this]() {
                            if (!ProfileThumbs::s_shutdownMode.load(std::memory_order_acquire)) {
                                processBinaryQueue();
                                if (!m_downloadQueue.empty()) processQueue();
                            }
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
