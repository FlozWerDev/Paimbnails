#include "CursorManager.hpp"
#include "../../../utils/ImageLoadHelper.hpp"
#include "../../../utils/AnimatedGIFSprite.hpp"
#include "../../../core/Settings.hpp"
#include <Geode/binding/PlatformToolbox.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/cocos.hpp>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <limits>
#include <typeinfo>

using namespace geode::prelude;
using namespace cocos2d;

// ── Trail texture (2x2 white, created once) ──────────────────────────────
namespace {
geode::Ref<CCTexture2D>& whiteTrailTexture() {
    static geode::Ref<CCTexture2D> s_tex = nullptr;
    return s_tex;
}

geode::Ref<CCTexture2D>& fallbackCursorTexture() {
    static geode::Ref<CCTexture2D> s_tex = nullptr;
    return s_tex;
}

constexpr int kCursorBaseZOrder = 1000000;
constexpr int kCursorMaxZOrder = std::numeric_limits<int>::max() - 2048;

std::string normalizeCursorToken(std::string value) {
    auto pos = value.find("class ");
    if (pos == 0) {
        value = value.substr(6);
    }
    return geode::utils::string::toLower(value);
}

bool nodeMatchesLayerFilters(CCNode* node, std::set<std::string> const& filters) {
    if (!node) return false;

    auto className = normalizeCursorToken(typeid(*node).name());
    auto nodeID = normalizeCursorToken(node->getID());

    for (auto const& layer : filters) {
        auto token = normalizeCursorToken(layer);
        if (!token.empty() && className.find(token) != std::string::npos) {
            return true;
        }
        if (!token.empty() && !nodeID.empty() && nodeID.find(token) != std::string::npos) {
            return true;
        }
    }

    return false;
}

bool containsVisibleLayerMatch(CCNode* node, std::set<std::string> const& filters) {
    if (!node || !node->isVisible()) return false;
    if (nodeMatchesLayerFilters(node, filters)) return true;

    auto* children = node->getChildren();
    if (!children) return false;

    for (auto* child : CCArrayExt<CCNode*>(children)) {
        if (containsVisibleLayerMatch(child, filters)) {
            return true;
        }
    }

    return false;
}

bool sampleCursorPosition(CCPoint& outPos, bool& outInsideWindow) {
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    auto mousePos = geode::cocos::getMousePos();
    outInsideWindow = mousePos.x >= 0.f && mousePos.y >= 0.f &&
        mousePos.x <= winSize.width && mousePos.y <= winSize.height;
    outPos.x = std::clamp(mousePos.x, 0.f, winSize.width);
    outPos.y = std::clamp(mousePos.y, 0.f, winSize.height);
    return true;
}

float clampCursorScale(float scale) {
    return std::clamp(scale, CURSOR_SCALE_MIN, CURSOR_SCALE_MAX);
}

CCPoint cursorHotspotAnchor() {
    return ccp(CURSOR_HOTSPOT_X, CURSOR_HOTSPOT_Y);
}

int desiredCursorHostZ(CCScene* scene, CCNode* cursorNode) {
    if (!scene) return kCursorBaseZOrder;

    int highestZ = std::numeric_limits<int>::min();
    bool foundRenderableSibling = false;

    if (auto* children = scene->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(children)) {
            if (!child || child == cursorNode || !child->isVisible()) {
                continue;
            }

            highestZ = std::max(highestZ, child->getZOrder());
            foundRenderableSibling = true;
        }
    }

    if (!foundRenderableSibling) {
        return kCursorBaseZOrder;
    }

    if (highestZ >= kCursorMaxZOrder) {
        return kCursorMaxZOrder;
    }

    return highestZ + 1;
}

void ensureCursorHostIsFrontmost(CCScene* scene, CCNode* cursorNode) {
    if (!scene || !cursorNode || cursorNode->getParent() != scene) {
        return;
    }

    auto targetZ = desiredCursorHostZ(scene, cursorNode);
    if (cursorNode->getZOrder() != targetZ) {
        scene->reorderChild(cursorNode, targetZ);
    }
}
} // namespace

// ── Trail presets ─────────────────────────────────────────────────────────
const CursorTrailPreset CursorManager::TRAIL_PRESETS[CursorManager::TRAIL_PRESET_COUNT] = {
    {"Blanco Clasico",  ccc3(255, 255, 255),  80.f,  3.f, 0, 200},
    {"Fuego",           ccc3(255, 140,   0), 120.f,  5.f, 1, 210},
    {"Hielo",           ccc3(  0, 220, 255), 100.f,  3.f, 0, 190},
    {"Arcoiris",        ccc3(255, 255, 255),  90.f,  4.f, 1, 220},
    {"Sombra",          ccc3( 80,  80,  80),  60.f,  6.f, 2, 180},
    {"Electrico",       ccc3(255, 255,   0),  40.f,  2.f, 1, 230},
    {"Rosa Neon",       ccc3(255,  50, 200),  90.f,  4.f, 0, 200},
    {"Verde Matrix",    ccc3( 50, 255,  50), 110.f,  2.f, 2, 190},
    {"Dorado",          ccc3(255, 215,   0),  70.f,  5.f, 1, 200},
    {"Invisible",       ccc3(  0,   0,   0),  10.f,  1.f, 0,   0},
};

// ── Singleton ─────────────────────────────────────────────────────────────
CursorManager& CursorManager::get() {
    static CursorManager inst;
    return inst;
}

// ── Paths ─────────────────────────────────────────────────────────────────
std::filesystem::path CursorManager::configPath() const {
    return Mod::get()->getSaveDir() / "cursor_config.json";
}

std::filesystem::path CursorManager::galleryDir() const {
    auto dir = Mod::get()->getSaveDir() / "cursor_gallery";
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) {
        std::filesystem::create_directories(dir, ec);
    }
    return dir;
}

// ── Config persistence ────────────────────────────────────────────────────
void CursorManager::loadConfig() {
    log::debug("[CursorManager] loadConfig");
    auto path = configPath();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return;

    auto rawRes = file::readString(path);
    if (!rawRes) {
        log::error("[CursorManager] Failed to open config file");
        return;
    }
    auto res = matjson::parse(rawRes.unwrap());
    if (res.isErr()) return;
    auto j = res.unwrap();

    m_config.enabled       = j["enabled"].asBool().unwrapOr(false);
    m_config.idleImage     = j["idleImage"].asString().unwrapOr("");
    m_config.moveImage     = j["moveImage"].asString().unwrapOr("");
    m_config.scale         = clampCursorScale(static_cast<float>(j["scale"].asDouble().unwrapOr(CURSOR_SCALE_DEFAULT)));
    m_config.opacity       = j["opacity"].asInt().unwrapOr(255);
    m_config.trailEnabled  = j["trailEnabled"].asBool().unwrapOr(false);
    m_config.trailR        = j["trailR"].asInt().unwrapOr(255);
    m_config.trailG        = j["trailG"].asInt().unwrapOr(255);
    m_config.trailB        = j["trailB"].asInt().unwrapOr(255);
    m_config.trailLength   = static_cast<float>(j["trailLength"].asDouble().unwrapOr(80.0));
    m_config.trailWidth    = static_cast<float>(j["trailWidth"].asDouble().unwrapOr(4.0));
    m_config.trailFadeType = j["trailFadeType"].asInt().unwrapOr(0);
    m_config.trailOpacity  = j["trailOpacity"].asInt().unwrapOr(200);
    m_config.trailPreset   = j["trailPreset"].asInt().unwrapOr(-1);

    auto layersArr = j["visibleLayers"].asArray();
    if (layersArr.isOk()) {
        m_config.visibleLayers.clear();
        for (auto& v : layersArr.unwrap()) {
            auto s = v.asString().unwrapOr("");
            if (!s.empty()) m_config.visibleLayers.insert(s);
        }
    }
}

void CursorManager::saveConfig() {
    m_config.scale = clampCursorScale(m_config.scale);

    matjson::Value j = matjson::Value();
    j["enabled"]      = m_config.enabled;
    j["idleImage"]    = m_config.idleImage;
    j["moveImage"]    = m_config.moveImage;
    j["scale"]        = static_cast<double>(m_config.scale);
    j["opacity"]      = m_config.opacity;
    j["trailEnabled"] = m_config.trailEnabled;
    j["trailR"]       = m_config.trailR;
    j["trailG"]       = m_config.trailG;
    j["trailB"]       = m_config.trailB;
    j["trailLength"]  = static_cast<double>(m_config.trailLength);
    j["trailWidth"]   = static_cast<double>(m_config.trailWidth);
    j["trailFadeType"]= m_config.trailFadeType;
    j["trailOpacity"] = m_config.trailOpacity;
    j["trailPreset"]  = m_config.trailPreset;

    matjson::Value layers = matjson::Value::array();
    for (auto& l : m_config.visibleLayers) {
        layers.push(l);
    }
    j["visibleLayers"] = layers;

    auto str = j.dump();
    auto writeRes = file::writeString(configPath(), str);
    if (!writeRes) {
        log::error("[CursorManager] Failed to write config: {}", writeRes.unwrapErr());
    }

    // Sync key values to Geode mod settings so the native settings UI stays in sync
    Mod::get()->setSettingValue<bool>("custom-cursor-enable", m_config.enabled);
    Mod::get()->setSettingValue<double>("custom-cursor-scale", static_cast<double>(m_config.scale));
    Mod::get()->setSettingValue<bool>("custom-cursor-trail", m_config.trailEnabled);
}

// ── Scene visibility ──────────────────────────────────────────────────────
bool CursorManager::shouldShowOnCurrentScene() const {
    auto scene = CCDirector::sharedDirector()->getRunningScene();
    if (!scene) return false;

    auto sceneClass = normalizeCursorToken(typeid(*scene).name());
    if (sceneClass.find("transition") != std::string::npos) {
        return false;
    }

    // cursor is always visible on every layer
    return true;
}

bool CursorManager::sceneMatchesVisibleLayers(CCScene* scene) const {
    if (!scene) return false;
    if (m_config.visibleLayers.empty()) return false;

    bool allSelected = true;
    for (auto const& opt : CURSOR_LAYER_OPTIONS) {
        if (m_config.visibleLayers.count(opt) == 0) {
            allSelected = false;
            break;
        }
    }

    if (allSelected) return true;

    return containsVisibleLayerMatch(scene, m_config.visibleLayers);
}

// ── Gallery ───────────────────────────────────────────────────────────────
std::vector<std::string> CursorManager::getGalleryImages() const {
    std::vector<std::string> result;
    auto dir = galleryDir();
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || ec) return result;

    for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        auto ext = geode::utils::string::pathToString(entry.path().extension());
        ext = geode::utils::string::toLower(ext);
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".gif"
            || ext == ".bmp" || ext == ".webp" || ext == ".tiff" || ext == ".tif"
            || ext == ".tga" || ext == ".psd" || ext == ".qoi" || ext == ".jxl") {
            result.push_back(geode::utils::string::pathToString(entry.path().filename()));
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::string CursorManager::addToGallery(std::filesystem::path const& srcPath) {
    auto dir = galleryDir();
    auto filename = geode::utils::string::pathToString(srcPath.filename());
    auto dest = dir / filename;
    int counter = 1;
    std::error_code existsEc;
    while (std::filesystem::exists(dest, existsEc) && !existsEc) {
        auto stem = geode::utils::string::pathToString(srcPath.stem());
        auto ext  = geode::utils::string::pathToString(srcPath.extension());
        filename  = fmt::format("{}_{}{}", stem, counter++, ext);
        dest      = dir / filename;
    }
    std::error_code copyEc;
    std::filesystem::copy_file(srcPath, dest, std::filesystem::copy_options::overwrite_existing, copyEc);
    if (copyEc) {
        log::error("[CursorManager] Failed to copy to gallery: {}", copyEc.message());
        return "";
    }
    return filename;
}

void CursorManager::removeFromGallery(std::string const& filename) {
    auto path = galleryDir() / filename;
    std::error_code rmEc;
    if (std::filesystem::exists(path, rmEc)) {
        std::filesystem::remove(path, rmEc);
    }
    bool changed = false;
    if (m_config.idleImage == filename) { m_config.idleImage = ""; changed = true; }
    if (m_config.moveImage == filename) { m_config.moveImage = ""; changed = true; }
    if (changed) { saveConfig(); reloadSprites(); }
}

void CursorManager::removeAllFromGallery() {
    for (auto& img : getGalleryImages()) {
        auto path = galleryDir() / img;
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) std::filesystem::remove(path, ec);
    }
    m_config.idleImage = "";
    m_config.moveImage = "";
    saveConfig();
    reloadSprites();
}

int CursorManager::cleanupInvalidImages() {
    int removed = 0;
    for (auto& img : getGalleryImages()) {
        auto path = galleryDir() / img;
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) continue;

        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) { removeFromGallery(img); removed++; continue; }

        unsigned char header[12] = {};
        f.read(reinterpret_cast<char*>(header), 12);
        auto bytesRead = f.gcount();
        f.close();

        if (bytesRead < 4) { removeFromGallery(img); removed++; continue; }

        bool valid = false;
        // PNG: 89 50 4E 47
        if (header[0] == 0x89 && header[1] == 0x50 && header[2] == 0x4E && header[3] == 0x47) valid = true;
        // JPEG: FF D8 FF
        else if (header[0] == 0xFF && header[1] == 0xD8 && header[2] == 0xFF) valid = true;
        // GIF: GIF8
        else if (header[0] == 'G' && header[1] == 'I' && header[2] == 'F' && header[3] == '8') valid = true;
        // WEBP: RIFF....WEBP
        else if (bytesRead >= 12 && header[0] == 'R' && header[1] == 'I' && header[2] == 'F' && header[3] == 'F'
                 && header[8] == 'W' && header[9] == 'E' && header[10] == 'B' && header[11] == 'P') valid = true;
        // BMP: BM
        else if (header[0] == 'B' && header[1] == 'M') valid = true;
        // TIFF: II (little-endian) or MM (big-endian)
        else if ((header[0] == 'I' && header[1] == 'I' && header[2] == 0x2A && header[3] == 0x00)
              || (header[0] == 'M' && header[1] == 'M' && header[2] == 0x00 && header[3] == 0x2A)) valid = true;
        // QOI: qoif
        else if (header[0] == 'q' && header[1] == 'o' && header[2] == 'i' && header[3] == 'f') valid = true;
        // JXL: \x00\x00\x00\x0C JXL \x20\x0C (12 bytes)
        else if (bytesRead >= 12 && header[0] == 0x00 && header[1] == 0x00 && header[2] == 0x00 && header[3] == 0x0C
                 && header[4] == 'J' && header[5] == 'X' && header[6] == 'L' && header[7] == 0x20
                 && header[8] == 0x0C) valid = true;

        if (!valid) { removeFromGallery(img); removed++; }
    }
    return removed;
}

CCTexture2D* CursorManager::loadGalleryThumb(std::string const& filename) const {
    auto path = galleryDir() / filename;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) return nullptr;
    auto img = ImageLoadHelper::loadStaticImage(path);
    if (img.success && img.texture) return img.texture;
    return nullptr;
}

// ── Init ──────────────────────────────────────────────────────────────────
void CursorManager::init() {
    log::info("[CursorManager] init");
    loadConfig();
    m_config.scale = clampCursorScale(m_config.scale);

    // Push loaded config to Geode mod settings for initial sync
    Mod::get()->setSettingValue<bool>("custom-cursor-enable", m_config.enabled);
    Mod::get()->setSettingValue<double>("custom-cursor-scale", static_cast<double>(m_config.scale));
    Mod::get()->setSettingValue<bool>("custom-cursor-trail", m_config.trailEnabled);
}

// ── Image loading ─────────────────────────────────────────────────────────
CCSprite* CursorManager::loadSprite(std::string const& filename) {
    if (filename.empty()) return nullptr;
    auto path = galleryDir() / filename;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) return nullptr;

    return ImageLoadHelper::loadAnimatedOrStatic(path, 10,
        [](std::string const& p) -> CCSprite* {
            return AnimatedGIFSprite::create(p);
        });
}

CCSprite* CursorManager::createFallbackSprite() {
    auto& fallbackTex = fallbackCursorTexture();
    if (!fallbackTex) {
        constexpr int sz = 16;
        uint8_t pixels[sz * sz * 4] = {};

        auto setPixel = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
            if (x < 0 || y < 0 || x >= sz || y >= sz) return;
            auto idx = (y * sz + x) * 4;
            pixels[idx + 0] = r;
            pixels[idx + 1] = g;
            pixels[idx + 2] = b;
            pixels[idx + 3] = a;
        };

        for (int y = 0; y <= 9; ++y) {
            for (int x = 0; x <= y / 2 + 1; ++x) {
                setPixel(x, y, 16, 16, 16, 255);
            }
        }
        for (int y = 0; y <= 8; ++y) {
            for (int x = 0; x <= y / 2; ++x) {
                setPixel(x, y, 255, 255, 255, 255);
            }
        }
        for (int y = 6; y <= 15; ++y) {
            for (int x = 2; x <= 4; ++x) {
                setPixel(x, y, 16, 16, 16, 255);
            }
        }
        for (int y = 7; y <= 14; ++y) {
            setPixel(3, y, 255, 255, 255, 255);
        }
        for (int x = 3; x <= 8; ++x) {
            setPixel(x, 10, 16, 16, 16, 255);
        }
        for (int x = 4; x <= 7; ++x) {
            setPixel(x, 10, 255, 255, 255, 255);
        }

        auto* newTex = new CCTexture2D();
        if (newTex->initWithData(pixels, kCCTexture2DPixelFormat_RGBA8888, sz, sz, CCSizeMake(sz, sz))) {
            fallbackTex = newTex;
        } else {
            newTex->release();
        }
    }

    if (!fallbackTex) return nullptr;
    return CCSprite::createWithTexture(fallbackTex.data());
}

void CursorManager::setIdleImage(std::string const& filename) {
    m_config.idleImage = filename;
    saveConfig();
    reloadSprites();
}

void CursorManager::setMoveImage(std::string const& filename) {
    m_config.moveImage = filename;
    saveConfig();
    reloadSprites();
}

void CursorManager::reloadSprites() {
    if (m_idleSprite && m_cursorNode) { m_idleSprite->removeFromParent(); m_idleSprite = nullptr; }
    if (m_moveSprite && m_cursorNode) { m_moveSprite->removeFromParent(); m_moveSprite = nullptr; }

    if (!m_config.enabled || !m_cursorNode) return;

    m_config.scale = clampCursorScale(m_config.scale);

    m_idleSprite = loadSprite(m_config.idleImage);
    if (!m_idleSprite) {
        m_idleSprite = createFallbackSprite();
    }
    if (m_idleSprite) {
        m_idleSprite->setScale(m_config.scale);
        m_idleSprite->setOpacity(static_cast<GLubyte>(m_config.opacity));
        m_idleSprite->setAnchorPoint(cursorHotspotAnchor());
        m_idleSprite->setZOrder(10);
        m_cursorNode->addChild(m_idleSprite);
    }

    m_moveSprite = loadSprite(m_config.moveImage);
    if (m_moveSprite) {
        m_moveSprite->setScale(m_config.scale);
        m_moveSprite->setOpacity(static_cast<GLubyte>(m_config.opacity));
        m_moveSprite->setAnchorPoint(cursorHotspotAnchor());
        m_moveSprite->setZOrder(10);
        m_cursorNode->addChild(m_moveSprite);
    }

    // Initial visibility
    bool showMove = m_isMoving && m_moveSprite;
    if (m_idleSprite) m_idleSprite->setVisible(!showMove || !m_moveSprite);
    if (m_moveSprite) m_moveSprite->setVisible(showMove);

    updateTrail();
}

// ── Attach / Detach ───────────────────────────────────────────────────────
void CursorManager::attachToScene(CCScene* scene) {
    log::debug("[CursorManager] attachToScene");
    if (!scene || !m_config.enabled) return;

    detachFromScene();

    m_cursorNode = CCNode::create();
    m_cursorNode->setID("paimon-cursor-host"_spr);
    m_cursorNode->setZOrder(kCursorBaseZOrder);
    scene->addChild(m_cursorNode);
    ensureCursorHostIsFrontmost(scene, m_cursorNode);

    bool insideWindow = true;
    if (!sampleCursorPosition(m_currentPos, insideWindow)) {
        auto winSize = CCDirector::sharedDirector()->getWinSize();
        m_currentPos = ccp(winSize.width / 2.f, winSize.height / 2.f);
    }
    m_velocity   = ccp(0.f, 0.f);
    m_isMoving   = false;
    m_moveTimer  = 0.f;

    reloadSprites();
    m_cursorNode->setVisible(insideWindow);
    if (m_trail) {
        m_trail->setVisible(insideWindow);
        m_trail->setPosition(m_currentPos);
    }
    syncSystemCursorVisibility(insideWindow);
}

void CursorManager::detachFromScene() {
    if (m_cursorNode) {
        m_cursorNode->removeFromParent();
        m_cursorNode  = nullptr;
        m_idleSprite  = nullptr;
        m_moveSprite  = nullptr;
        m_trail       = nullptr;
    }
    syncSystemCursorVisibility(false);
}

void CursorManager::releaseSharedResources() {
    detachFromScene();
    whiteTrailTexture() = nullptr;
    fallbackCursorTexture() = nullptr;
}

void CursorManager::syncSystemCursorVisibility(bool hideSystemCursor) {
    if (hideSystemCursor == m_systemCursorHidden) return;

    if (hideSystemCursor) {
        PlatformToolbox::hideCursor();
    } else {
        PlatformToolbox::showCursor();
    }

    m_systemCursorHidden = hideSystemCursor;
}

// ── Update (every frame) ──────────────────────────────────────────────────
void CursorManager::update(float dt) {
    if (!m_config.enabled || !m_cursorNode) return;

    // Safety: lost parent during scene transition
    if (!m_cursorNode->getParent()) {
        detachFromScene();
        return;
    }

    if (auto* scene = m_cursorNode->getParentByType<CCScene>()) {
        ensureCursorHostIsFrontmost(scene, m_cursorNode);
    }

    CCPoint newPos;
    bool insideWindow = true;
    if (!sampleCursorPosition(newPos, insideWindow)) return;

    // Hide during gameplay if the user has disabled the native cursor or the mod option
    bool hideInGameplay = false;
    if (PlayLayer::get() != nullptr) {
        bool nativeHide = !GameManager::get()->getGameVariable("0024"); // GameVar::ShowCursor
        bool modHide = paimon::settings::cursor::hideInGameplay();
        if (nativeHide || modHide) {
            hideInGameplay = true;
        }
    }

    if (!insideWindow || hideInGameplay) {
        if (m_trail) {
            m_trail->removeFromParent();
            m_trail = nullptr;
        }
        m_cursorNode->setVisible(false);
        syncSystemCursorVisibility(false);
        return;
    }

    bool regainedVisibility = false;
    if (!m_cursorNode->isVisible()) {
        m_cursorNode->setVisible(true);
        regainedVisibility = true;
    }
    syncSystemCursorVisibility(true);

    CCPoint prevPos = m_currentPos;
    m_currentPos    = newPos;

    m_velocity.x = (m_currentPos.x - prevPos.x) / std::max(dt, 0.001f);
    m_velocity.y = (m_currentPos.y - prevPos.y) / std::max(dt, 0.001f);
    float speed  = std::sqrt(m_velocity.x * m_velocity.x + m_velocity.y * m_velocity.y);

    // Movement detection: considers "moving" for 0.15s after last movement above threshold
    if (speed > 5.f) {
        m_isMoving  = true;
        m_moveTimer = 0.15f;
    } else if (m_moveTimer > 0.f) {
        m_moveTimer -= dt;
        if (m_moveTimer <= 0.f) {
            m_isMoving  = false;
            m_moveTimer = 0.f;
        }
    }

    // Switch sprite visibility based on movement state
    if (m_idleSprite && m_moveSprite) {
        m_idleSprite->setVisible(!m_isMoving);
        m_moveSprite->setVisible(m_isMoving);
    }

    if (m_config.trailEnabled && (regainedVisibility || !m_trail || m_trail->getParent() != m_cursorNode)) {
        updateTrail();
    }

    // Update positions
    CCPoint spritePos = m_currentPos;
    if (m_idleSprite) m_idleSprite->setPosition(spritePos);
    if (m_moveSprite) m_moveSprite->setPosition(spritePos);
    if (m_trail) {
        m_trail->setVisible(true);
        m_trail->setPosition(spritePos);
    }
}

// ── Apply config live ─────────────────────────────────────────────────────
void CursorManager::applyConfigLive() {
    m_config.scale = clampCursorScale(m_config.scale);

    if (m_idleSprite) {
        m_idleSprite->setScale(m_config.scale);
        m_idleSprite->setOpacity(static_cast<GLubyte>(m_config.opacity));
    }
    if (m_moveSprite) {
        m_moveSprite->setScale(m_config.scale);
        m_moveSprite->setOpacity(static_cast<GLubyte>(m_config.opacity));
    }
    updateTrail();
    saveConfig();
}

// ── Trail ─────────────────────────────────────────────────────────────────
void CursorManager::updateTrail() {
    if (m_trail && m_cursorNode) {
        m_trail->removeFromParent();
        m_trail = nullptr;
    }

    if (!m_config.trailEnabled || !m_cursorNode) return;

    auto& trailTex = whiteTrailTexture();
    if (!trailTex) {
        const int sz = 2;
        uint8_t pixels[sz * sz * 4];
        memset(pixels, 255, sizeof(pixels));
        auto* newTex = new CCTexture2D();
        if (newTex->initWithData(pixels, kCCTexture2DPixelFormat_RGBA8888, sz, sz, CCSizeMake(sz, sz))) {
            trailTex = newTex;
        } else {
            newTex->release();
        }
    }
    if (!trailTex) return;

    float fadeTime = m_config.trailLength / 60.f;
    // Apply fade type variation via fade time scaling
    if (m_config.trailFadeType == 1) fadeTime *= 1.4f; // sine: longer fade
    if (m_config.trailFadeType == 2) fadeTime = 0.05f; // none: instant cut

    m_trail = CCMotionStreak::create(
        fadeTime,
        1.f,
        m_config.trailWidth,
        ccc3(static_cast<GLubyte>(m_config.trailR),
             static_cast<GLubyte>(m_config.trailG),
             static_cast<GLubyte>(m_config.trailB)),
        trailTex.data()
    );

    if (m_trail && m_trail->getTexture()) {
        m_trail->setOpacity(static_cast<GLubyte>(m_config.trailOpacity));
        ccBlendFunc blend = {GL_SRC_ALPHA, GL_ONE};
        m_trail->setBlendFunc(blend);
        m_trail->setZOrder(5);
        m_cursorNode->addChild(m_trail);
        m_trail->setVisible(m_cursorNode->isVisible());
        m_trail->setPosition(m_currentPos);
    } else {
        m_trail = nullptr;
        log::warn("[CursorManager] Failed to create trail with valid texture");
    }
}
