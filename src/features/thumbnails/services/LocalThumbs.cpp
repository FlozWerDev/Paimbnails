#include "LocalThumbs.hpp"

#include <Geode/Geode.hpp>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "../../../utils/ThreadTracker.hpp"
#include <unordered_set>
#include <thread>
#include "../../../core/QualityConfig.hpp"
#include "../../../utils/ImageConverter.hpp"

using namespace geode::prelude;

namespace {
#pragma pack(push, 1)
struct RGBHeader {
    uint32_t width;
    uint32_t height;
};
#pragma pack(pop)
}

LocalThumbs::LocalThumbs() = default;

void LocalThumbs::initCache() {
    log::info("[LocalThumbs] initCache: scanning local thumbnails");
    std::lock_guard<std::mutex> lock(m_mutex);
    m_availableLevels.clear();
    
    auto d = dir();
    std::error_code ec;
    if (!std::filesystem::exists(d, ec)) {
        m_cacheInitialized.store(true, std::memory_order_release);
        return;
    }

    for (auto const& entry : std::filesystem::directory_iterator(d, ec)) {
        if (m_shuttingDown.load(std::memory_order_relaxed)) {
            break;
        }
        if (ec) break;
        if (entry.is_regular_file() && entry.path().extension() == ".rgb") {
            auto stemStr = geode::utils::string::pathToString(entry.path().stem());
            if (auto res = geode::utils::numFromString<int32_t>(stemStr); res.isOk()) {
                int32_t levelID = res.unwrap();
                migrateLegacyFile(levelID, entry.path());
                m_availableLevels.insert(levelID);
            }
            else {
                auto underscorePos = stemStr.find_last_of('_');
                if (underscorePos != std::string::npos) {
                    auto idPart = stemStr.substr(0, underscorePos);
                    if (auto idRes = geode::utils::numFromString<int32_t>(idPart); idRes.isOk()) {
                        m_availableLevels.insert(idRes.unwrap());
                    }
                }
            }
        }
    }

    log::info("[LocalThumbs] initCache: found {} levels", m_availableLevels.size());
    m_cacheInitialized.store(true, std::memory_order_release);
}

LocalThumbs& LocalThumbs::get() {
    // Kept alive intentionally: avoids destruction races if initCache was never awaited (RuntimeLifecycle::shutdown handles teardown).
    static auto* inst = new LocalThumbs();
    static std::once_flag loadFlag;
    static std::once_flag initFlag;
    std::call_once(loadFlag, [&]() {
        inst->loadMappings();
    });
    std::call_once(initFlag, [&]() {
        LocalThumbs* self = inst;
        paimon::ThreadTracker::get().spawn([self]() {
            geode::utils::thread::setName("PaimonLocalThumbs");
            self->initCache();
        });
    });
    return *inst;
}

std::string LocalThumbs::dir() const {
    auto save = Mod::get()->getSaveDir();
    std::filesystem::path base(geode::utils::string::pathToString(save));
    auto d = base / "thumbnails";
    std::error_code ec;
    std::error_code ecDir;
    if (!std::filesystem::exists(d, ecDir)) {
        std::filesystem::create_directories(d, ec);
        if (ec) {
            log::error("no se pudo crear la carpeta thumbnails: {}", ec.message());
        } else {
            log::debug("carpeta thumbnails lista en: {}", geode::utils::string::pathToString(d));
        }
    }
    return geode::utils::string::pathToString(d);
}

std::optional<std::string> LocalThumbs::getThumbPath(int32_t levelID) const {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_cacheInitialized.load(std::memory_order_acquire)) {
            if (m_availableLevels.find(levelID) == m_availableLevels.end()) {
                return std::nullopt;
            }
        }
    }

    auto d = dir();
    int maxIdx = -1;
    std::error_code ec;
    for (int i = 0; i < MAX_THUMBS_PER_LEVEL; ++i) {
        auto p = std::filesystem::path(d) / (std::to_string(levelID) + "_" + std::to_string(i) + ".rgb");
        if (std::filesystem::exists(p, ec)) {
            maxIdx = i;
        } else {
            break;
        }
    }

    if (maxIdx >= 0) {
        auto p = std::filesystem::path(d) / (std::to_string(levelID) + "_" + std::to_string(maxIdx) + ".rgb");
        return geode::utils::string::pathToString(p);
    }

    auto legacyPath = std::filesystem::path(d) / (std::to_string(levelID) + ".rgb");
    if (std::filesystem::exists(legacyPath, ec)) {
        return geode::utils::string::pathToString(legacyPath);
    }

    return std::nullopt;
}

std::optional<std::string> LocalThumbs::getThumbPathByIndex(int32_t levelID, int index) const {
    if (index < 0 || index >= MAX_THUMBS_PER_LEVEL) return std::nullopt;
    auto p = std::filesystem::path(dir()) / (std::to_string(levelID) + "_" + std::to_string(index) + ".rgb");
    std::error_code ec;
    if (std::filesystem::exists(p, ec)) {
        return geode::utils::string::pathToString(p);
    }
    return std::nullopt;
}

std::vector<std::string> LocalThumbs::getAllThumbPaths(int32_t levelID) const {
    std::vector<std::string> paths;
    auto d = dir();
    std::error_code ec;
    for (int i = 0; i < MAX_THUMBS_PER_LEVEL; ++i) {
        auto p = std::filesystem::path(d) / (std::to_string(levelID) + "_" + std::to_string(i) + ".rgb");
        if (std::filesystem::exists(p, ec)) {
            paths.push_back(geode::utils::string::pathToString(p));
        } else {
            break;
        }
    }
    return paths;
}

int LocalThumbs::getThumbCount(int32_t levelID) const {
    return static_cast<int>(getAllThumbPaths(levelID).size());
}

std::optional<std::string> LocalThumbs::findAnyThumbnail(int32_t levelID) const {
    {
        std::lock_guard<std::mutex> lock(m_lookupMutex);
        auto it = m_lookupCache.find(levelID);
        if (it != m_lookupCache.end()) {
            return it->second.path;
        }
    }

    auto store = [this, levelID](std::optional<std::string> value) -> std::optional<std::string> {
        std::lock_guard<std::mutex> lock(m_lookupMutex);
        m_lookupCache[levelID] = LookupEntry{value};
        return value;
    };

    auto rgbPath = getThumbPath(levelID);
    if (rgbPath) return store(rgbPath);

    bool skipThumbDir = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_cacheInitialized.load(std::memory_order_acquire) &&
            m_availableLevels.find(levelID) == m_availableLevels.end()) {
            skipThumbDir = true;
        }
    }

    static const std::vector<std::string> exts = {".png", ".jpg", ".jpeg", ".webp", ".gif", ".mp4"};
    std::error_code ecFind;

    if (!skipThumbDir) {
        auto thumbDir = std::filesystem::path(dir());
        for (auto const& ext : exts) {
            auto p = thumbDir / (std::to_string(levelID) + ext);
            if (std::filesystem::exists(p, ecFind)) return store(geode::utils::string::pathToString(p));
        }
    }

    auto qualityCacheDir = paimon::quality::cacheDir();
    for (auto const& ext : exts) {
        auto p = qualityCacheDir / (std::to_string(levelID) + ext);
        if (std::filesystem::exists(p, ecFind)) return store(geode::utils::string::pathToString(p));
    }

    return store(std::nullopt);
}

void LocalThumbs::invalidateLookup(int32_t levelID) {
    {
        std::lock_guard<std::mutex> lock(m_lookupMutex);
        if (levelID == 0) {
            m_lookupCache.clear();
        } else {
            m_lookupCache.erase(levelID);
        }
    }
    invalidateTexture(levelID);
}

LocalThumbs::LoadResult LocalThumbs::loadAsRGBA(int32_t levelID) const {
    LoadResult result;

    auto localPath = findAnyThumbnail(levelID);
    if (!localPath) return result;

    std::filesystem::path fsPath(*localPath);
    bool isRgbFormat = (fsPath.extension() == ".rgb");

    if (isRgbFormat) {
        // .rgb: header (width+height, 8 bytes) + datos RGB24
        std::ifstream rgbFile(fsPath, std::ios::binary);
        if (!rgbFile) return result;

        uint32_t rgbW = 0, rgbH = 0;
        rgbFile.read(reinterpret_cast<char*>(&rgbW), sizeof(rgbW));
        rgbFile.read(reinterpret_cast<char*>(&rgbH), sizeof(rgbH));
        if (!rgbFile || rgbW == 0 || rgbH == 0 || rgbW > 16384 || rgbH > 16384) return result;

        size_t rgbSize = static_cast<size_t>(rgbW) * rgbH * 3;
        result.pixels.resize(rgbSize);
        rgbFile.read(reinterpret_cast<char*>(result.pixels.data()), rgbSize);
        if (!rgbFile) { result.pixels.clear(); return result; }

        // Keep raw RGB888 — GPU converts to RGBA during upload, saving CPU conversion time
        result.width = static_cast<int>(rgbW);
        result.height = static_cast<int>(rgbH);
        result.isRgb = true;
    } else {
        // formato estandar (png/jpg/webp): bytes crudos para el caller
        std::ifstream imgFile(fsPath, std::ios::binary | std::ios::ate);
        if (!imgFile.is_open()) return result;

        size_t fileSize = imgFile.tellg();
        imgFile.seekg(0, std::ios::beg);
        result.pixels.resize(fileSize);
        imgFile.read(reinterpret_cast<char*>(result.pixels.data()), fileSize);
        if (!imgFile) { result.pixels.clear(); return result; }

        result.isRgb = false;
    }

    return result;
}

std::vector<int32_t> LocalThumbs::getAllLevelIDs() const {
    std::vector<int32_t> ids;
    std::unordered_set<int32_t> uniqueIds;

    auto scanDir = [&](std::filesystem::path const& path) {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) return;
        for (auto const& entry : std::filesystem::directory_iterator(path, ec)) {
            if (ec) break;
            if (entry.is_regular_file()) {
                auto ext = geode::utils::string::pathToString(entry.path().extension());
                if (ext == ".rgb" || ext == ".png" || ext == ".webp" || ext == ".jpg") {
                    std::string stem = geode::utils::string::pathToString(entry.path().stem());
                    auto underscorePos = stem.find_last_of('_');
                    std::string idPart = stem;
                    if (underscorePos != std::string::npos) {
                        auto suffix = stem.substr(underscorePos + 1);
                        if (auto suffixRes = geode::utils::numFromString<int>(suffix); suffixRes.isOk()) {
                            idPart = stem.substr(0, underscorePos);
                        }
                    }
                    if (auto res = geode::utils::numFromString<int32_t>(idPart); res.isOk()) {
                         uniqueIds.insert(res.unwrap());
                    }
                }
            }
        }
    };

    scanDir(dir());
    scanDir(paimon::quality::cacheDir());

    ids.assign(uniqueIds.begin(), uniqueIds.end());
    return ids;
}

CCTexture2D* LocalThumbs::getCachedTexture(int32_t levelID) const {
    std::lock_guard<std::mutex> lock(m_texCacheMutex);
    auto it = m_texCache.find(levelID);
    if (it == m_texCache.end()) return nullptr;
    auto lruIt = std::find(m_texCacheLru.begin(), m_texCacheLru.end(), levelID);
    if (lruIt != m_texCacheLru.end()) m_texCacheLru.erase(lruIt);
    m_texCacheLru.push_back(levelID);
    return it->second;
}

void LocalThumbs::cacheTexture(int32_t levelID, CCTexture2D* tex) const {
    if (!tex) return;
    std::lock_guard<std::mutex> lock(m_texCacheMutex);
    auto it = m_texCache.find(levelID);
    if (it != m_texCache.end()) {
        if (it->second == tex) return;
        it->second->release();
        m_texCache.erase(it);
        auto lruIt = std::find(m_texCacheLru.begin(), m_texCacheLru.end(), levelID);
        if (lruIt != m_texCacheLru.end()) m_texCacheLru.erase(lruIt);
    }
    tex->retain();
    m_texCache[levelID] = tex;
    m_texCacheLru.push_back(levelID);
    while (m_texCacheLru.size() > MAX_TEX_CACHE_ENTRIES) {
        int32_t evict = m_texCacheLru.front();
        m_texCacheLru.pop_front();
        auto eIt = m_texCache.find(evict);
        if (eIt != m_texCache.end()) {
            eIt->second->release();
            m_texCache.erase(eIt);
        }
    }
}

void LocalThumbs::invalidateTexture(int32_t levelID) const {
    std::lock_guard<std::mutex> lock(m_texCacheMutex);
    if (levelID == 0) {
        for (auto& [id, tex] : m_texCache) tex->release();
        m_texCache.clear();
        m_texCacheLru.clear();
        return;
    }
    auto it = m_texCache.find(levelID);
    if (it != m_texCache.end()) {
        it->second->release();
        m_texCache.erase(it);
        auto lruIt = std::find(m_texCacheLru.begin(), m_texCacheLru.end(), levelID);
        if (lruIt != m_texCacheLru.end()) m_texCacheLru.erase(lruIt);
    }
}

CCTexture2D* LocalThumbs::loadTexture(int32_t levelID) const {
    if (auto* cached = getCachedTexture(levelID)) {
        return cached;
    }
    log::debug("[LocalThumbs] loadTexture: levelID={}", levelID);

    auto tryLoadFromDir = [&](std::filesystem::path const& baseDir) -> CCTexture2D* {
        std::filesystem::path rgbPath;
        std::error_code fsEc;

        for (int i = MAX_THUMBS_PER_LEVEL - 1; i >= 0; --i) {
            auto indexed = baseDir / (std::to_string(levelID) + "_" + std::to_string(i) + ".rgb");
            if (std::filesystem::exists(indexed, fsEc) && !fsEc) {
                rgbPath = indexed;
                break;
            }
        }
        if (rgbPath.empty()) {
            auto legacy = baseDir / (std::to_string(levelID) + ".rgb");
            if (std::filesystem::exists(legacy, fsEc) && !fsEc) {
                rgbPath = legacy;
            }
        }

        if (!rgbPath.empty()) {
            log::debug("cargando desde rgb: {}", geode::utils::string::pathToString(rgbPath));
            std::ifstream in(rgbPath, std::ios::binary);
            if (in) {
                RGBHeader head{};
                in.read(reinterpret_cast<char*>(&head), sizeof(head));
                if (in && head.width > 0 && head.height > 0) {
                    const size_t size = static_cast<size_t>(head.width) * head.height * 3;
                    auto buf = std::make_unique<uint8_t[]>(size);
                    in.read(reinterpret_cast<char*>(buf.get()), size);
                    if (in) {
                        size_t pixelCount = static_cast<size_t>(head.width) * head.height;
                        auto rgbaBuf = std::make_unique<uint8_t[]>(pixelCount * 4);
                        ImageConverter::rgbToRgbaFast(buf.get(), rgbaBuf.get(), pixelCount);

                        auto tex = new CCTexture2D();
                        if (tex->initWithData(rgbaBuf.get(), kCCTexture2DPixelFormat_RGBA8888, head.width, head.height, CCSize(head.width, head.height))) {
                            ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
                            tex->setTexParameters(&params);
                            tex->autorelease();
                            return tex;
                        }
                        tex->release();
                    }
                }
            }
        }

        std::vector<std::string> extensions = {".png", ".webp", ".jpg"};
        for (auto const& ext : extensions) {
            auto p = baseDir / (std::to_string(levelID) + ext);
            std::error_code extEc;
            if (std::filesystem::exists(p, extEc) && !extEc) {
                std::string pathStr = geode::utils::string::pathToString(p);
                log::debug("cargando imagen: {}", pathStr);
                auto tex = CCTextureCache::sharedTextureCache()->addImage(pathStr.c_str(), false);
                if (tex) {
                    return tex;
                }
            }
        }
        return nullptr;
    };

    if (auto tex = tryLoadFromDir(dir())) {
        cacheTexture(levelID, tex);
        return tex;
    }

    if (auto tex = tryLoadFromDir(paimon::quality::cacheDir())) {
        cacheTexture(levelID, tex);
        return tex;
    }

    log::debug("[LocalThumbs] loadTexture: not found levelID={}", levelID);
    return nullptr;
}

void LocalThumbs::loadTextureAsync(int32_t levelID, std::function<void(CCTexture2D*)> callback) {
    if (auto* cached = getCachedTexture(levelID)) {
        if (callback) callback(cached);
        return;
    }

    paimon::ThreadTracker::get().spawn([this, levelID, callback = std::move(callback)]() mutable {
        if (m_shuttingDown.load(std::memory_order_acquire)) return;

        auto data = loadAsRGBA(levelID);
        std::vector<uint8_t> rgba;
        int w = 0, h = 0;
        if (!data.pixels.empty() && data.isRgb && data.width > 0 && data.height > 0) {
            w = data.width;
            h = data.height;
            size_t pixelCount = static_cast<size_t>(w) * h;
            rgba.resize(pixelCount * 4);
            ImageConverter::rgbToRgbaFast(data.pixels.data(), rgba.data(), pixelCount);
        }

        geode::Loader::get()->queueInMainThread(
            [this, levelID, rgba = std::move(rgba), w, h, callback = std::move(callback)]() mutable {
                if (m_shuttingDown.load(std::memory_order_acquire)) return;

                CCTexture2D* tex = nullptr;
                if (!rgba.empty() && w > 0 && h > 0) {
                    auto* t = new CCTexture2D();
                    if (t->initWithData(rgba.data(), kCCTexture2DPixelFormat_RGBA8888,
                                        w, h, CCSize(static_cast<float>(w), static_cast<float>(h)))) {
                        ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
                        t->setTexParameters(&params);
                        t->autorelease();
                        tex = t;
                    } else {
                        t->release();
                    }
                }
                if (tex) {
                    cacheTexture(levelID, tex);
                } else {
                    tex = loadTexture(levelID);
                }
                if (callback) callback(tex);
            });
    });
}

bool LocalThumbs::saveRGB(int32_t levelID, const uint8_t* data, uint32_t width, uint32_t height) {
    log::info("[LocalThumbs] saveRGB: levelID={} {}x{}", levelID, width, height);
    
    if (!data) {
        log::error("no se puede guardar: data es null");
        return false;
    }
    
    if (width == 0 || height == 0) {
        log::error("dimensiones invalidas pa guardar ({}x{})", width, height);
        return false;
    }
    
    int idx = nextIndex(levelID);
    if (idx >= MAX_THUMBS_PER_LEVEL) {
        log::warn("[LocalThumbs] nivel {} ya tiene {} thumbnails, limite alcanzado", levelID, MAX_THUMBS_PER_LEVEL);
        return false;
    }

    auto p = std::filesystem::path(dir()) / (std::to_string(levelID) + "_" + std::to_string(idx) + ".rgb");
    auto tmp = p;
    tmp += ".tmp";
    log::debug("escribiendo en: {}", geode::utils::string::pathToString(p));
    
    bool writeOk = false;
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            log::error("error abriendo archivo temporal pa escribir: {}", geode::utils::string::pathToString(tmp));
            return false;
        }

        RGBHeader head{ width, height };
        out.write(reinterpret_cast<char const*>(&head), sizeof(head));

        const size_t size = static_cast<size_t>(width) * height * 3;
        log::debug("escribiendo {} bytes", size);
        out.write(reinterpret_cast<char const*>(data), size);

        writeOk = out.good();
    }

    if (!writeOk) {
        log::error("fallo la escritura de datos al temporal");
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
        return false;
    }

    // rename atomico: tmp → final
    std::error_code ec;
    std::filesystem::rename(tmp, p, ec);
    if (ec) {
        log::error("rename fallo {}: {}", geode::utils::string::pathToString(tmp), ec.message());
        std::filesystem::remove(tmp, ec);
        return false;
    }

    log::info("miniatura guardada OK pal nivel: {} (indice {})", levelID, idx);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_availableLevels.insert(levelID);
    }
    invalidateLookup(levelID);
    return true;
}

bool LocalThumbs::saveFromRGBA(int32_t levelID, const uint8_t* data, uint32_t width, uint32_t height) {
    if (!data || width == 0 || height == 0) return false;

    size_t pixelCount = static_cast<size_t>(width) * height;
    std::vector<uint8_t> rgbData(pixelCount * 3);

    for (size_t i = 0; i < pixelCount; ++i) {
        rgbData[i * 3 + 0] = data[i * 4 + 0];
        rgbData[i * 3 + 1] = data[i * 4 + 1];
        rgbData[i * 3 + 2] = data[i * 4 + 2];
    }

    return saveRGB(levelID, rgbData.data(), width, height);
}

std::string LocalThumbs::mappingFile() const {
    return geode::utils::string::pathToString(std::filesystem::path(dir()) / "filename_mapping.txt");
}

void LocalThumbs::storeFileMapping(int32_t levelID, std::string const& fileName) {
    m_fileMapping[levelID] = fileName;
    saveMappings();
    log::info("mapping guardado: {} -> {}", levelID, fileName);
}

std::optional<std::string> LocalThumbs::getFileName(int32_t levelID) const {
    auto it = m_fileMapping.find(levelID);
    if (it != m_fileMapping.end()) {
        return it->second;
    }
    return std::nullopt;
}

void LocalThumbs::loadMappings() {
    m_fileMapping.clear();
    auto dataRes = file::readString(mappingFile());
    if (!dataRes) {
        log::debug("no se hallo archivo de mapping, empezamos de cero");
        return;
    }
    
    std::istringstream stream(dataRes.unwrap());
    std::string line;
    int count = 0;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        
        std::istringstream iss(line);
        int32_t levelID;
        std::string fileName;
        if (iss >> levelID >> fileName) {
            m_fileMapping[levelID] = fileName;
            count++;
        }
    }
    log::info("se cargaron {} mappings", count);
}

void LocalThumbs::saveMappings() {
    std::string content;
    for (auto const& [levelID, fileName] : m_fileMapping) {
        content += fmt::format("{} {}\n", levelID, fileName);
    }
    auto res = file::writeString(mappingFile(), content);
    if (!res) {
        log::error("error guardando mappings en {}: {}", mappingFile(), res.unwrapErr());
        return;
    }
    log::debug("se guardaron {} mappings", m_fileMapping.size());
}

void LocalThumbs::shutdown() {
    log::info("[LocalThumbs] shutdown");
    m_shuttingDown.store(true, std::memory_order_release);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!m_cacheInitialized.load(std::memory_order_acquire)) {
        if (std::chrono::steady_clock::now() >= deadline) {
            log::warn("[LocalThumbs] initCache still running after timeout");
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void LocalThumbs::migrateLegacyFile(int32_t levelID, std::filesystem::path const& legacyPath) {
    auto newPath = std::filesystem::path(dir()) / (std::to_string(levelID) + "_0.rgb");
    std::error_code ec;
    if (std::filesystem::exists(newPath, ec)) {
        std::filesystem::remove(legacyPath, ec);
        return;
    }
    std::filesystem::rename(legacyPath, newPath, ec);
    if (ec) {
        log::warn("[LocalThumbs] migracion legacy fallo para {}: {}", levelID, ec.message());
    } else {
        log::info("[LocalThumbs] migrado {}.rgb -> {}_0.rgb", levelID, levelID);
    }
}

int LocalThumbs::nextIndex(int32_t levelID) const {
    auto d = dir();
    std::error_code ec;
    for (int i = 0; i < MAX_THUMBS_PER_LEVEL; ++i) {
        auto p = std::filesystem::path(d) / (std::to_string(levelID) + "_" + std::to_string(i) + ".rgb");
        if (!std::filesystem::exists(p, ec)) {
            return i;
        }
    }
    return MAX_THUMBS_PER_LEVEL;
}

bool LocalThumbs::removeThumb(int32_t levelID, int index) {
    auto d = dir();
    int count = getThumbCount(levelID);
    if (index < 0 || index >= count) {
        log::warn("[LocalThumbs] removeThumb: indice {} fuera de rango (count={})", index, count);
        return false;
    }

    auto target = std::filesystem::path(d) / (std::to_string(levelID) + "_" + std::to_string(index) + ".rgb");
    std::error_code ec;
    std::filesystem::remove(target, ec);
    if (ec) {
        log::error("[LocalThumbs] removeThumb: error borrando {}: {}", geode::utils::string::pathToString(target), ec.message());
        return false;
    }

    for (int i = index + 1; i < count; ++i) {
        auto from = std::filesystem::path(d) / (std::to_string(levelID) + "_" + std::to_string(i) + ".rgb");
        auto to = std::filesystem::path(d) / (std::to_string(levelID) + "_" + std::to_string(i - 1) + ".rgb");
        ec.clear();
        std::filesystem::rename(from, to, ec);
        if (ec) {
            log::warn("[LocalThumbs] removeThumb: re-index fallo {} -> {}: {}", i, i - 1, ec.message());
        }
    }

    if (count <= 1) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_availableLevels.erase(levelID);
    }
    invalidateLookup(levelID);

    log::info("[LocalThumbs] removeThumb: borrado indice {} de nivel {} (quedan {})", index, levelID, std::max(0, count - 1));
    return true;
}

CCTexture2D* LocalThumbs::loadTextureByIndex(int32_t levelID, int index) const {
    auto pathOpt = getThumbPathByIndex(levelID, index);
    if (!pathOpt) return nullptr;

    auto rgbPath = std::filesystem::path(*pathOpt);
    std::ifstream in(rgbPath, std::ios::binary);
    if (!in) return nullptr;

    RGBHeader head{};
    in.read(reinterpret_cast<char*>(&head), sizeof(head));
    if (!in || head.width == 0 || head.height == 0) return nullptr;

    const size_t size = static_cast<size_t>(head.width) * head.height * 3;
    auto buf = std::make_unique<uint8_t[]>(size);
    in.read(reinterpret_cast<char*>(buf.get()), size);
    if (!in) return nullptr;

    size_t pixelCount = static_cast<size_t>(head.width) * head.height;
    auto rgbaBuf = std::make_unique<uint8_t[]>(pixelCount * 4);
    ImageConverter::rgbToRgbaFast(buf.get(), rgbaBuf.get(), pixelCount);

    auto tex = new CCTexture2D();
    if (tex->initWithData(rgbaBuf.get(), kCCTexture2DPixelFormat_RGBA8888, head.width, head.height, CCSize(head.width, head.height))) {
        ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
        tex->setTexParameters(&params);
        tex->autorelease();
        return tex;
    }
    tex->release();
    return nullptr;
}
