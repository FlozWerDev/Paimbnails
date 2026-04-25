#include "LevelColors.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/PaimonFormat.hpp"
#include "../../../utils/DominantColors.hpp"
#include "../../../utils/ImageConverter.hpp"
#include "../../../core/QualityConfig.hpp"
#include <Geode/loader/Mod.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/Geode.hpp>
#include <sstream>
#include <filesystem>

using namespace geode::prelude;
using namespace cocos2d;

LevelColors& LevelColors::get() { static LevelColors lc; return lc; }

LevelColors::~LevelColors() {
    if (paimon::isRuntimeShuttingDown()) return;
    flushIfDirty();
}

std::filesystem::path LevelColors::path() const {
    return Mod::get()->getSaveDir() / "thumbnails" / "level_colors.paimon";
}

void LevelColors::load() const {
    if (m_loaded) return; 
    log::info("[LevelColors] load: loading color data");
    m_loaded = true; 
    m_items.clear();
    
    auto p = path();
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) return;
    
    // cargar datos desencriptados de .paimon.
    auto data = PaimonFormat::load(p);
    if (data.empty()) return;
    
    // parsear csv.
    std::string content(data.begin(), data.end());
    std::stringstream ss(content); 
    std::string line;
    
    log::debug("[LevelColors] load: parsing {} bytes", content.size());
    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        std::stringstream ls(line);
        std::string id,r1,g1,b1,r2,g2,b2;
        if (!std::getline(ls, id, ',')) continue;
        if (!std::getline(ls, r1, ',')) continue;
        if (!std::getline(ls, g1, ',')) continue;
        if (!std::getline(ls, b1, ',')) continue;
        if (!std::getline(ls, r2, ',')) continue;
        if (!std::getline(ls, g2, ',')) continue;
        if (!std::getline(ls, b2, ',')) continue;
        LevelColorPair pair{ 
            ccColor3B{(GLubyte)geode::utils::numFromString<int>(r1).unwrapOr(0), (GLubyte)geode::utils::numFromString<int>(g1).unwrapOr(0), (GLubyte)geode::utils::numFromString<int>(b1).unwrapOr(0)},
            ccColor3B{(GLubyte)geode::utils::numFromString<int>(r2).unwrapOr(0), (GLubyte)geode::utils::numFromString<int>(g2).unwrapOr(0), (GLubyte)geode::utils::numFromString<int>(b2).unwrapOr(0)} 
        };
        m_items[geode::utils::numFromString<int32_t>(id).unwrapOr(0)] = pair;
    }
}

void LevelColors::save() const {
    log::info("[LevelColors] save: writing {} entries", m_items.size());
    std::stringstream ss;
    for (auto const& [id, p] : m_items) {
        ss << id << "," << (int)p.a.r << "," << (int)p.a.g << "," << (int)p.a.b
           << "," << (int)p.b.r << "," << (int)p.b.g << "," << (int)p.b.b << "\n";
    }
    
    std::string content = ss.str();
    std::vector<uint8_t> data(content.begin(), content.end());
    
    // guardar encriptado en formato .paimon.
    auto p = path();
    PaimonFormat::save(p, data);
}

void LevelColors::set(int32_t levelID, ccColor3B a, ccColor3B b) {
    log::debug("[LevelColors] set: levelID={} a=({},{},{}) b=({},{},{})", levelID, a.r, a.g, a.b, b.r, b.g, b.b);
    std::lock_guard<std::mutex> lock(m_mutex);
    load();
    m_items[levelID] = LevelColorPair{a, b};
    m_dirty = true;
    m_pendingWrites++;
    if (m_pendingWrites >= BATCH_SAVE_THRESHOLD) {
        save();
        m_dirty = false;
        m_pendingWrites = 0;
    }
}

std::optional<LevelColorPair> LevelColors::getPair(int32_t levelID) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    load();
    auto it = m_items.find(levelID);
    if (it != m_items.end()) return it->second;
    return std::nullopt;
}

void LevelColors::flushIfDirty() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_dirty) {
        log::info("[LevelColors] flushIfDirty: flushing pending writes");
        save();
        m_dirty = false;
        m_pendingWrites = 0;
    }
}

void LevelColors::extractFromImage(int32_t levelID, cocos2d::CCImage* image) {
    if (!image) return;
    log::debug("[LevelColors] extractFromImage: levelID={}", levelID);

    unsigned char* imgData = image->getData();
    int w = image->getWidth();
    int h = image->getHeight();
    bool hasAlpha = image->hasAlpha();
    
    if (!imgData || w <= 0 || h <= 0) return;
    
    // convertir rgba->rgb24 si necesario.
    std::vector<uint8_t> rgb24;
    const uint8_t* rgbPtr = nullptr;
    
    if (hasAlpha) {
        rgb24.resize(w * h * 3);
        ImageConverter::rgbaToRgbFast(imgData, rgb24.data(), static_cast<size_t>(w) * h);
        rgbPtr = rgb24.data();
    } else {
        rgbPtr = imgData;
    }
    
    auto pair = DominantColors::extract(rgbPtr, w, h);
    cocos2d::ccColor3B colorA{pair.first.r, pair.first.g, pair.first.b};
    cocos2d::ccColor3B colorB{pair.second.r, pair.second.g, pair.second.b};

    this->set(levelID, colorA, colorB);
}

void LevelColors::extractFromRawData(int32_t levelID, const uint8_t* imgData, int w, int h, bool hasAlpha) {
    if (!imgData || w <= 0 || h <= 0) return;
    log::debug("[LevelColors] extractFromRawData: levelID={} {}x{} alpha={}", levelID, w, h, hasAlpha);
    
    // convertir rgba->rgb24 si necesario.
    std::vector<uint8_t> rgb24;
    const uint8_t* rgbPtr = nullptr;
    
    if (hasAlpha) {
        rgb24.resize(w * h * 3);
        ImageConverter::rgbaToRgbFast(imgData, rgb24.data(), static_cast<size_t>(w) * h);
        rgbPtr = rgb24.data();
    } else {
        rgbPtr = imgData;
    }
    
    auto pair2 = DominantColors::extract(rgbPtr, w, h);
    cocos2d::ccColor3B colorA2{pair2.first.r, pair2.first.g, pair2.first.b};
    cocos2d::ccColor3B colorB2{pair2.second.r, pair2.second.g, pair2.second.b};

    this->set(levelID, colorA2, colorB2);
}

// procesar imagen cacheada.
void processCachedImage(std::filesystem::path const& filepath, int32_t levelID) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        log::warn("[LevelColors] fallo al abrir: {}", geode::utils::string::pathToString(filepath));
        return;
    }
    
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    
    if (data.empty()) return;
    
    auto image = new cocos2d::CCImage();
    if (!image->initWithImageData(const_cast<uint8_t*>(data.data()), data.size())) {
        log::warn("[LevelColors] fallo al decodificar imagen: {}", geode::utils::string::pathToString(filepath));
        image->release();
        return;
    }
    
    LevelColors::get().extractFromImage(levelID, image);

    image->release();
}

void LevelColors::extractColorsFromCache() {
    log::info("[LevelColors] extrayendo colores de cache...");

    std::error_code ecCache;
    int processed = 0;
    int success = 0;
    int skipped = 0;

    auto scanDir = [&](std::filesystem::path const& cacheDir) {
        std::error_code existsEc;
        if (!std::filesystem::exists(cacheDir, existsEc) || existsEc) return;

        for (auto const& entry : std::filesystem::directory_iterator(cacheDir, ecCache)) {
            if (ecCache || !entry.is_regular_file()) continue;

            auto filepath = entry.path();
            auto ext = geode::utils::string::toLower(geode::utils::string::pathToString(filepath.extension()));
            if (!(ext == ".png" || ext == ".webp" || ext == ".jpg" || ext == ".jpeg")) continue;

            std::string filename = geode::utils::string::pathToString(filepath.stem());
            auto levelIDResult = geode::utils::numFromString<int32_t>(filename);
            if (!levelIDResult.isOk()) {
                log::debug("[LevelColors] saltando archivo no-numerico: {}", filename);
                continue;
            }
            int32_t levelID = levelIDResult.unwrap();

            if (getPair(levelID).has_value()) {
                skipped++;
                continue;
            }

            processed++;
            processCachedImage(filepath, levelID);
            if (getPair(levelID).has_value()) {
                success++;
            }

            if (success % 10 == 0 && success > 0) {
                log::info("[LevelColors] progreso: {} ok, {} saltado", success, skipped);
            }
        }
    };

    // coherencia: escanear ambos orígenes de thumbnails
    scanDir(Mod::get()->getSaveDir() / "thumbnails");
    scanDir(paimon::quality::cacheDir());
    
    log::info("[LevelColors] listo: {} ok, {} saltado, {} procesado", 
              success, skipped, processed);
    
    // fuerzo escritura si quedaron cambios pendientes del batch
    flushIfDirty();
}

