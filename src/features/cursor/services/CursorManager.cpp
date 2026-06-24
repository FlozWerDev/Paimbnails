#include "CursorManager.hpp"
#include <Geode/utils/string.hpp>
#include "../../../utils/ImageLoadHelper.hpp"
#include "../../../utils/AnimatedGIFSprite.hpp"
#include "../../../utils/CursorIcoDecoder.hpp"
#include "../../../utils/GifEncoder.hpp"
#include "../../../utils/ImageConverter.hpp"
#include "../../../core/Settings.hpp"
#include <Geode/binding/PlatformToolbox.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/EndLevelLayer.hpp>
#include <Geode/binding/RetryLevelLayer.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/CCTextInputNode.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/ui/OverlayManager.hpp>
#include <Geode/cocos/layers_scenes_transitions_nodes/CCTransition.h>
#include <fstream>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <typeinfo>

using namespace geode::prelude;
using namespace cocos2d;

// Trail texture (2x2 white, created once)
namespace {
geode::Ref<CCTexture2D>& whiteTrailTexture() {
    static geode::Ref<CCTexture2D> s_tex = nullptr;
    return s_tex;
}

geode::Ref<CCTexture2D>& fallbackCursorTexture() {
    static geode::Ref<CCTexture2D> s_tex = nullptr;
    return s_tex;
}

// Use INT_MAX to ensure the cursor is ALWAYS on top, even when other mods
// (like Eclipse Menu or other overlay-based menus) add popups with high z-orders.
// This fixes the issue where the cursor appears UNDER some menus instead of on top.
constexpr int kCursorBaseZOrder = INT_MAX;

// States in ascending priority order for stable iteration.
constexpr std::array<CursorState, CURSOR_STATE_COUNT> kAllStates = {
    CursorState::Idle, CursorState::Move, CursorState::Hover,
    CursorState::Click, CursorState::Text, CursorState::Disabled
};

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
    auto winSize = CCDirector::get()->getWinSize();
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

// Normalize any source image (gallery images can be high-resolution) to a
// consistent on-screen size derived from the user scale. This is the core
// positioning fix: applying a raw setScale() to an arbitrary-resolution image
// made the cursor huge and visually detached from the click point, exactly the
// "mal posicionamiento" bug. limitNodeSize keeps the on-screen footprint fixed
// regardless of the source resolution (same approach Ecuet uses).
float cursorTargetSize(float scale) {
    return std::max(4.f, 100.f * clampCursorScale(scale));
}

void applyCursorVisual(cocos2d::CCSprite* sprite, float scale, int opacity) {
    if (!sprite) return;
    float target = cursorTargetSize(scale);
    geode::cocos::limitNodeSize(sprite, {target, target}, 999.f, 0.0001f);
    sprite->setOpacity(static_cast<GLubyte>(std::clamp(opacity, 0, 255)));
    sprite->setAnchorPoint(cursorHotspotAnchor());
}

// Cursor context under the pointer, used to pick the active state.
struct CursorContext {
    bool overButton   = false; // enabled CCMenuItem (hand/link)
    bool overDisabled = false; // disabled CCMenuItem (not-allowed)
    bool overText     = false; // CCTextInputNode / text field (I-beam)
};

// Returns true if worldPos falls inside the boundingBox of `node`.
bool nodeContainsWorldPoint(CCNode* node, CCPoint const& worldPos) {
    auto* parent = node->getParent();
    CCPoint local = parent ? parent->convertToNodeSpace(worldPos) : worldPos;
    return node->boundingBox().containsPoint(local);
}

// Walks the hierarchy finding the most relevant context under the cursor.
// A single pass decides hover/disabled/text to avoid three traversals.
void scanCursorContext(CCNode* node, CCPoint const& worldPos, int depth,
                       CursorContext& ctx) {
    if (!node || !node->isVisible() || depth > 14) return;

    // Text field (I-beam). CCTextInputNode is the base class for all GD/Geode inputs.
    if (auto* input = typeinfo_cast<CCTextInputNode*>(node)) {
        if (geode::cocos::nodeIsVisible(input) && nodeContainsWorldPoint(input, worldPos)) {
            ctx.overText = true;
        }
    }

    if (auto* item = typeinfo_cast<CCMenuItem*>(node)) {
        if (geode::cocos::nodeIsVisible(item) && nodeContainsWorldPoint(item, worldPos)) {
            if (item->isEnabled()) ctx.overButton = true;
            else                   ctx.overDisabled = true;
        }
    }

    auto* children = node->getChildren();
    if (!children) return;
    for (auto* child : CCArrayExt<CCNode*>(children)) {
        scanCursorContext(child, worldPos, depth + 1, ctx);
    }
}
} // namespace

CursorManager::~CursorManager() {
    detachFromScene();
    (void)whiteTrailTexture().take();
    (void)fallbackCursorTexture().take();
}

// Trail presets
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

// Singleton
CursorManager& CursorManager::get() {
    static CursorManager inst;
    return inst;
}

// Paths
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

// State <-> config mapping
std::string& CursorManager::configFieldForState(CursorState state) {
    switch (state) {
        case CursorState::Move:     return m_config.moveImage;
        case CursorState::Hover:    return m_config.hoverImage;
        case CursorState::Click:    return m_config.clickImage;
        case CursorState::Text:     return m_config.textImage;
        case CursorState::Disabled: return m_config.disabledImage;
        case CursorState::Idle:
        default:                    return m_config.idleImage;
    }
}

std::string CursorManager::imageForState(CursorState state) const {
    return const_cast<CursorManager*>(this)->configFieldForState(state);
}

void CursorManager::setImageForState(CursorState state, std::string const& filename) {
    configFieldForState(state) = filename;
    saveConfig();
    reloadSprites();
}

// Config persistence
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
    m_config.hoverImage    = j["hoverImage"].asString().unwrapOr("");
    m_config.clickImage    = j["clickImage"].asString().unwrapOr("");
    m_config.textImage     = j["textImage"].asString().unwrapOr("");
    m_config.disabledImage = j["disabledImage"].asString().unwrapOr("");
    m_config.hoverEnabled  = j["hoverEnabled"].asBool().unwrapOr(true);
    m_config.clickEnabled  = j["clickEnabled"].asBool().unwrapOr(true);
    m_config.textEnabled   = j["textEnabled"].asBool().unwrapOr(true);
    m_config.disabledEnabled = j["disabledEnabled"].asBool().unwrapOr(true);
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

    m_config.followDelayEnabled = j["followDelayEnabled"].asBool().unwrapOr(false);
    m_config.followDelay        = std::clamp(static_cast<float>(j["followDelay"].asDouble().unwrapOr(0.5)), 0.f, 1.f);

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
    j["hoverImage"]   = m_config.hoverImage;
    j["clickImage"]   = m_config.clickImage;
    j["textImage"]    = m_config.textImage;
    j["disabledImage"]= m_config.disabledImage;
    j["hoverEnabled"] = m_config.hoverEnabled;
    j["clickEnabled"] = m_config.clickEnabled;
    j["textEnabled"]  = m_config.textEnabled;
    j["disabledEnabled"] = m_config.disabledEnabled;
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
    j["followDelayEnabled"] = m_config.followDelayEnabled;
    j["followDelay"]        = static_cast<double>(m_config.followDelay);

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
}

// Scene visibility
bool CursorManager::shouldShowOnCurrentScene() const {
    auto scene = CCDirector::get()->getRunningScene();
    if (!scene) return false;

    // During a scene transition, getRunningScene() returns a CCTransitionScene
    // wrapping the outgoing and incoming scenes. Instead of returning false here
    // (which made the cursor disappear for the whole transition), we look through
    // the transition at the real scenes: if either the incoming or outgoing scene
    // matches a valid layer, the cursor stays visible without flickering.
    if (auto* transition = typeinfo_cast<CCTransitionScene*>(scene)) {
        bool inOk  = transition->m_pInScene  && sceneMatchesVisibleLayers(transition->m_pInScene);
        bool outOk = transition->m_pOutScene && sceneMatchesVisibleLayers(transition->m_pOutScene);
        return inOk || outOk;
    }

    // CustomTransitionScene is a normal CCScene that reparents source/dest layers
    // as children; sceneMatchesVisibleLayers recurses and finds them.
    return sceneMatchesVisibleLayers(scene);
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

// Gallery & packs
std::filesystem::path CursorManager::packsDir() const {
    auto dir = galleryDir() / "packs";
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) {
        std::filesystem::create_directories(dir, ec);
    }
    return dir;
}

namespace {
bool relPathIsImage(std::filesystem::path const& p) {
    auto ext = geode::utils::string::toLower(
        geode::utils::string::pathToString(p.extension()));
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".gif"
        || ext == ".bmp" || ext == ".webp" || ext == ".tiff" || ext == ".tif"
        || ext == ".tga" || ext == ".psd" || ext == ".qoi" || ext == ".jxl";
}
} // namespace

std::vector<std::string> CursorManager::getPacks() const {
    std::vector<std::string> result;
    auto dir = packsDir();
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || ec) return result;
    for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        if (entry.is_directory()) {
            result.push_back(geode::utils::string::pathToString(entry.path().filename()));
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<std::string> CursorManager::getImagesInPack(std::string const& packName) const {
    std::vector<std::string> result;
    std::filesystem::path dir;
    std::string prefix;
    if (packName.empty()) {
        dir = galleryDir();           // loose images (root)
        prefix = "";
    } else {
        dir = packsDir() / packName;  // inside a pack
        prefix = "packs/" + packName + "/";
    }

    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || ec) return result;
    for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        if (relPathIsImage(entry.path())) {
            result.push_back(prefix + geode::utils::string::pathToString(entry.path().filename()));
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<std::string> CursorManager::getGalleryImages() const {
    // Compat: loose + all packs, all as relative paths.
    std::vector<std::string> result = getImagesInPack("");
    for (auto const& pack : getPacks()) {
        auto imgs = getImagesInPack(pack);
        result.insert(result.end(), imgs.begin(), imgs.end());
    }
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

// Import: normal images + Windows cursors (.cur/.ico/.ani) + .zip
namespace {
// Sanitizes a filename to safe ASCII for the Windows filesystem.
// Cursor packs often have Japanese/unicode names that break file::writeBinary
// ("The system cannot find the path specified"). Only [A-Za-z0-9 _-] is kept;
// separators are collapsed to '_'. Returns "" if nothing remains (caller uses fallback).
std::string sanitizeAsciiStem(std::string const& in) {
    std::string out;
    out.reserve(in.size());
    bool lastSpace = false;
    for (unsigned char c : in) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-') {
            out.push_back(static_cast<char>(c));
            lastSpace = false;
        } else if (c == ' ' || c == '.' || c == '(' || c == ')') {
            // collapse separators to a single '_'
            if (!lastSpace && !out.empty()) {
                out.push_back('_');
                lastSpace = true;
            }
        }
        // discard any other byte (UTF-8 multibyte, etc.)
    }
    // recortar '_' sobrantes al inicio/fin
    while (!out.empty() && out.front() == '_') out.erase(out.begin());
    while (!out.empty() && out.back() == '_') out.pop_back();
    if (out.size() > 48) out.resize(48);
    return out;
}

// Returns a unique filename within `dir` for `baseName`.
std::string uniqueGalleryName(std::filesystem::path const& dir, std::string baseName) {
    std::filesystem::path candidate(baseName);
    auto stem = geode::utils::string::pathToString(candidate.stem());
    auto ext  = geode::utils::string::pathToString(candidate.extension());
    if (ext.empty()) ext = ".png";

    std::string name = stem + ext;
    auto dest = dir / name;
    int counter = 1;
    std::error_code ec;
    while (std::filesystem::exists(dest, ec) && !ec) {
        name = fmt::format("{}_{}{}", stem, counter++, ext);
        dest = dir / name;
    }
    return name;
}

bool extensionLooksLikeCursor(std::string const& lowerExt) {
    return lowerExt == ".cur" || lowerExt == ".ani" || lowerExt == ".ico";
}

// Returns a unique ASCII pack name (subfolder) derived from the zip filename.
std::string uniquePackName(std::filesystem::path const& packsRoot, std::string base) {
    std::string clean = sanitizeAsciiStem(base);
    if (clean.empty()) clean = "pack";
    std::string name = clean;
    auto dest = packsRoot / name;
    int counter = 1;
    std::error_code ec;
    while (std::filesystem::exists(dest, ec) && !ec) {
        name = fmt::format("{}_{}", clean, counter++);
        dest = packsRoot / name;
    }
    return name;
}

bool extensionLooksLikeImage(std::string const& lowerExt) {
    return lowerExt == ".png" || lowerExt == ".jpg" || lowerExt == ".jpeg"
        || lowerExt == ".gif" || lowerExt == ".bmp" || lowerExt == ".webp"
        || lowerExt == ".tiff" || lowerExt == ".tif" || lowerExt == ".tga"
        || lowerExt == ".psd" || lowerExt == ".qoi" || lowerExt == ".jxl";
}
} // namespace

std::string CursorManager::importSingleData(std::vector<uint8_t> const& data,
                                            std::string const& displayName,
                                            std::filesystem::path const& destDir,
                                            std::string const& relPrefix) {
    if (data.empty()) {
        log::warn("[CursorManager] importSingleData: empty data for '{}'", displayName);
        return "";
    }
    auto const& dir = destDir;
    std::error_code mkEc;
    std::filesystem::create_directories(dir, mkEc);

    std::filesystem::path namePath(displayName);
    auto stem = sanitizeAsciiStem(geode::utils::string::pathToString(namePath.stem()));
    if (stem.empty()) stem = "cursor";

    // Windows cursors (.cur / .ico / .ani)
    if (paimon::cursor_ico::isSupported(data.data(), data.size())) {
        auto decoded = paimon::cursor_ico::decode(data.data(), data.size());
        if (!decoded.success || decoded.frames.empty()) {
            log::warn("[CursorManager] Failed to decode cursor '{}': {}", displayName, decoded.error);
            return "";
        }
        log::debug("[CursorManager] decoded '{}': {} frame(s), animated={}",
            displayName, decoded.frames.size(), decoded.animated);

        if (decoded.animated && decoded.frames.size() > 1) {
            // Animado -> GIF (el pipeline de animacion del mod solo lee GIF).
            std::vector<paimon::gif::EncodeFrame> gifFrames;
            gifFrames.reserve(decoded.frames.size());
            for (auto& f : decoded.frames) {
                paimon::gif::EncodeFrame gf;
                gf.width = f.width;
                gf.height = f.height;
                gf.delayMs = f.delayMs;
                gf.rgba = std::move(f.rgba);
                gifFrames.push_back(std::move(gf));
            }
            auto gifBytes = paimon::gif::encode(gifFrames);
            if (gifBytes.empty()) {
                log::warn("[CursorManager] GIF encode failed for '{}'", displayName);
                return "";
            }
            auto name = uniqueGalleryName(dir, stem + ".gif");
            auto writeRes = file::writeBinary(dir / name,
                geode::ByteVector(gifBytes.begin(), gifBytes.end()));
            if (!writeRes) {
                log::error("[CursorManager] Failed to write '{}': {}", name, writeRes.unwrapErr());
                return "";
            }
            return relPrefix + name;
        }

        // Estatico -> PNG.
        auto& f = decoded.frames.front();
        if (f.width <= 0 || f.height <= 0 || f.rgba.empty()) return "";
        std::vector<uint8_t> png;
        if (!ImageConverter::rgbaToPngBuffer(f.rgba.data(),
                static_cast<uint32_t>(f.width), static_cast<uint32_t>(f.height), png)
            || png.empty()) {
            log::warn("[CursorManager] PNG encode failed for '{}'", displayName);
            return "";
        }
        auto name = uniqueGalleryName(dir, stem + ".png");
        auto writeRes = file::writeBinary(dir / name,
            geode::ByteVector(png.begin(), png.end()));
        if (!writeRes) {
            log::error("[CursorManager] Failed to write '{}': {}", name, writeRes.unwrapErr());
            return "";
        }
        return relPrefix + name;
    }

    // Imagen estandar: guardar tal cual (conserva GIF animado original)
    auto ext = geode::utils::string::toLower(
        geode::utils::string::pathToString(namePath.extension()));
    if (ext.empty() || !extensionLooksLikeImage(ext)) {
        // Sin extension util: deducir por contenido para al menos los comunes.
        using paimon::format::ImageFormat;
        switch (paimon::format::detect(data.data(), data.size())) {
            case ImageFormat::PNG:  ext = ".png";  break;
            case ImageFormat::JPEG: ext = ".jpg";  break;
            case ImageFormat::GIF:  ext = ".gif";  break;
            case ImageFormat::WebP: ext = ".webp"; break;
            case ImageFormat::BMP:  ext = ".bmp";  break;
            default: return ""; // unknown format, skip
        }
    }
    auto name = uniqueGalleryName(dir, stem + ext);
    auto writeRes = file::writeBinary(dir / name, geode::ByteVector(data.begin(), data.end()));
    if (!writeRes) {
        log::error("[CursorManager] Failed to write '{}': {}", name, writeRes.unwrapErr());
        return "";
    }
    return relPrefix + name;
}

std::vector<std::string> CursorManager::importFromFile(std::filesystem::path const& srcPath) {
    std::vector<std::string> imported;
    m_lastImportError.clear();

    auto ext = geode::utils::string::toLower(
        geode::utils::string::pathToString(srcPath.extension()));

    log::info("[CursorManager] importFromFile: '{}' (ext='{}')",
        geode::utils::string::pathToString(srcPath), ext);

    m_lastImportedPack.clear();

    // Pack .zip (cursors-4u.com, etc.)
    if (ext == ".zip") {
        auto unzipRes = file::Unzip::create(srcPath);
        if (!unzipRes) {
            log::error("[CursorManager] Failed to open zip: {}", unzipRes.unwrapErr());
            m_lastImportError = "Couldn't open the .zip file.";
            return imported;
        }
        auto& unzip = unzipRes.unwrap();

        // Don't extract entry-by-entry. Cursor packs often have Japanese/unicode names
        // that break Geode's Unzip re-lookup by name on Windows ("Unable to locate
        // entry, code -100"). Instead, extract everything to a temp folder at once,
        // then walk the disk.
        auto tmpDir = Mod::get()->getSaveDir() / "cursor_zip_tmp";
        std::error_code ec;
        std::filesystem::remove_all(tmpDir, ec);
        std::filesystem::create_directories(tmpDir, ec);

        auto extractRes = unzip.extractAllTo(tmpDir);
        if (!extractRes) {
            log::error("[CursorManager] extractAllTo failed: {}", extractRes.unwrapErr());
            m_lastImportError = "Couldn't extract the .zip file.";
            std::filesystem::remove_all(tmpDir, ec);
            return imported;
        }

        // Each zip gets its own pack (subfolder) to avoid cluttering the gallery.
        auto packName = uniquePackName(packsDir(),
            geode::utils::string::pathToString(srcPath.stem()));
        auto packDir  = packsDir() / packName;
        std::string relPrefix = "packs/" + packName + "/";
        std::filesystem::create_directories(packDir, ec);

        int considered = 0, skipped = 0, failed = 0;
        std::error_code itEc;
        for (auto const& dirEntry :
                std::filesystem::recursive_directory_iterator(tmpDir, itEc)) {
            if (itEc) break;
            if (!dirEntry.is_regular_file()) continue;

            auto entryPath = dirEntry.path();
            auto entryExt  = geode::utils::string::toLower(
                geode::utils::string::pathToString(entryPath.extension()));
            auto baseName  = geode::utils::string::pathToString(entryPath.filename());

            if (baseName.empty() || baseName.rfind("._", 0) == 0) { skipped++; continue; }
            if (geode::utils::string::pathToString(entryPath).find("__MACOSX") != std::string::npos) { skipped++; continue; }
            if (!extensionLooksLikeCursor(entryExt) && !extensionLooksLikeImage(entryExt)) {
                log::debug("[CursorManager]   skip '{}' (ext '{}')", baseName, entryExt);
                skipped++;
                continue;
            }

            considered++;
            auto readRes = file::readBinary(entryPath);
            if (!readRes) {
                log::warn("[CursorManager]   read failed '{}': {}", baseName, readRes.unwrapErr());
                failed++;
                continue;
            }
            auto bytes = readRes.unwrap();
            std::vector<uint8_t> vec(bytes.begin(), bytes.end());

            auto name = importSingleData(vec, baseName, packDir, relPrefix);
            if (!name.empty()) {
                imported.push_back(name);
            } else {
                failed++;
            }
        }

        std::filesystem::remove_all(tmpDir, ec);

        log::info("[CursorManager] zip import done: pack='{}' considered={} imported={} skipped={} failed={}",
            packName, considered, imported.size(), skipped, failed);

        if (imported.empty()) {
            // limpiar el pack vacio
            std::filesystem::remove_all(packDir, ec);
            if (considered == 0) {
                m_lastImportError = "The .zip had no cursor/image files (.cur, .ani, .png, .gif).";
            } else {
                m_lastImportError = fmt::format(
                    "Found {} cursor file(s) but none could be decoded.", considered);
            }
        } else {
            m_lastImportedPack = packName;
        }
        return imported;
    }

    // Loose Windows cursor (.cur/.ico/.ani)
    if (extensionLooksLikeCursor(ext)) {
        auto readRes = file::readBinary(srcPath);
        if (!readRes) {
            log::error("[CursorManager] Failed to read cursor file: {}", readRes.unwrapErr());
            m_lastImportError = "Couldn't read the file.";
            return imported;
        }
        auto bytes = readRes.unwrap();
        std::vector<uint8_t> vec(bytes.begin(), bytes.end());
        auto name = importSingleData(vec,
            geode::utils::string::pathToString(srcPath.filename()), galleryDir(), "");
        if (!name.empty()) imported.push_back(name);
        else m_lastImportError = "Couldn't decode that cursor file.";
        return imported;
    }

    // Regular image: classic direct-copy path (loose).
    auto name = addToGallery(srcPath);
    if (!name.empty()) imported.push_back(name);
    else m_lastImportError = "Couldn't import that image.";
    return imported;
}

void CursorManager::removeFromGallery(std::string const& filename) {
    auto path = galleryDir() / filename;
    std::error_code rmEc;
    if (std::filesystem::exists(path, rmEc)) {
        std::filesystem::remove(path, rmEc);
    }
    bool changed = false;
    for (auto state : kAllStates) {
        auto& field = configFieldForState(state);
        if (field == filename) { field = ""; changed = true; }
    }
    if (changed) { saveConfig(); reloadSprites(); }
}

void CursorManager::removeAllFromGallery() {
    // Delete loose images and all pack folders.
    for (auto& img : getImagesInPack("")) {
        auto path = galleryDir() / img;
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) std::filesystem::remove(path, ec);
    }
    std::error_code ec;
    std::filesystem::remove_all(packsDir(), ec);

    m_config.idleImage  = "";
    m_config.moveImage  = "";
    m_config.hoverImage = "";
    m_config.clickImage = "";
    m_config.textImage  = "";
    m_config.disabledImage = "";
    saveConfig();
    reloadSprites();
}

void CursorManager::removePack(std::string const& packName) {
    if (packName.empty()) return;
    auto dir = packsDir() / packName;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);

    // Si algun estado apuntaba a una imagen de este pack, limpiarlo.
    std::string prefix = "packs/" + packName + "/";
    bool changed = false;
    for (auto state : kAllStates) {
        auto& field = configFieldForState(state);
        if (field.rfind(prefix, 0) == 0) { field = ""; changed = true; }
    }
    if (changed) { saveConfig(); reloadSprites(); }
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

    // 1) Normal attempt (PNG/JPG/BMP/... and first GIF frame via stb).
    auto img = ImageLoadHelper::loadStaticImage(path);
    if (img.success && img.texture) return img.texture;

    // 2) Fallback for GIFs that stb can't decode (some disposal/interlace modes):
    //    use the mod's own decoder and take the first frame, so .gif files
    //    don't appear as empty cells in the gallery.
    if (ImageLoadHelper::isAnimatedImage(path)) {
        auto bin = ImageLoadHelper::readBinaryFile(path);
        if (!bin.empty() && GIFDecoder::isGIF(bin.data(), bin.size())) {
            // Gallery thumbnail: only the first frame is shown.
            auto gif = GIFDecoder::decode(bin.data(), bin.size(), 1);
            if (!gif.frames.empty()) {
                auto const& f = gif.frames.front();
                auto* tex = new CCTexture2D();
                if (tex->initWithData(f.pixels.data(), kCCTexture2DPixelFormat_RGBA8888,
                                      f.width, f.height, CCSize(f.width, f.height))) {
                    tex->setAntiAliasTexParameters();
                    return tex;  // caller hace release
                }
                tex->release();
            }
        }
    }
    return nullptr;
}

// Init
void CursorManager::init() {
    log::info("[CursorManager] init");
    loadConfig();
    m_config.scale = clampCursorScale(m_config.scale);

    // Push loaded config to Geode mod settings for initial sync
    Mod::get()->setSettingValue<bool>("custom-cursor-enable", m_config.enabled);
}

// Image loading
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
        // Classic arrow drawn from an explicit pixel map. Hotspot is the
        // top-left corner, matching the OS cursor.
        //   ' ' = transparente,  '#' = borde negro,  '.' = relleno blanco
        static char const* kArrow[] = {
            "#.          ",
            "#..         ",
            "#...        ",
            "#....       ",
            "#.....      ",
            "#......     ",
            "#.......    ",
            "#........   ",
            "#.........  ",
            "#..........#",
            "#.....#####.",
            "#..#..#     ",
            "#.# #..#    ",
            "##  #..#    ",
            "#    #..#   ",
            "     #..#   ",
            "      #..#  ",
            "      #..#  ",
            "       ##   ",
        };
        constexpr int kW = 12;
        constexpr int kH = 19;
        std::vector<uint8_t> pixels(static_cast<size_t>(kW) * kH * 4, 0);

        auto setPixel = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
            if (x < 0 || y < 0 || x >= kW || y >= kH) return;
            auto idx = (static_cast<size_t>(y) * kW + x) * 4;
            pixels[idx + 0] = r;
            pixels[idx + 1] = g;
            pixels[idx + 2] = b;
            pixels[idx + 3] = a;
        };

        for (int y = 0; y < kH; ++y) {
            char const* row = kArrow[y];
            for (int x = 0; x < kW && row[x]; ++x) {
                if (row[x] == '#')      setPixel(x, y, 0, 0, 0, 255);
                else if (row[x] == '.') setPixel(x, y, 255, 255, 255, 255);
            }
        }

        auto* newTex = new CCTexture2D();
        if (newTex->initWithData(pixels.data(), kCCTexture2DPixelFormat_RGBA8888, kW, kH, CCSizeMake(kW, kH))) {
            // Nearest-neighbor filtering keeps the arrow sharp when scaled.
            ccTexParams params{GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
            newTex->setTexParameters(&params);
            // Ref::adopt takes ownership of the existing refcount=1 without
            // adding an extra retain. Without adopt, Ref::operator= retains,
            // leaving refcount=2 — permanent leak.
            fallbackTex = geode::Ref<CCTexture2D>::adopt(newTex);
        } else {
            newTex->release();
        }
    }

    if (!fallbackTex) return nullptr;
    return CCSprite::createWithTexture(fallbackTex.data());
}

CCSprite* CursorManager::spriteForState(CursorState state) const {
    auto it = m_sprites.find(state);
    return it != m_sprites.end() ? it->second : nullptr;
}

bool CursorManager::hasLoadedCursorVisual() const {
    return !m_sprites.empty();
}

void CursorManager::reloadSprites() {
    // Limpia todos los sprites de estado existentes.
    for (auto& [state, sprite] : m_sprites) {
        if (sprite && m_cursorNode) sprite->removeFromParent();
    }
    m_sprites.clear();

    if (!m_config.enabled || !m_cursorNode) return;

    m_config.scale = clampCursorScale(m_config.scale);

    auto attachSprite = [this](CursorState state, CCSprite* sprite) {
        if (!sprite) return;
        applyCursorVisual(sprite, m_config.scale, m_config.opacity);
        sprite->setZOrder(10);
        sprite->setVisible(false);
        m_cursorNode->addChild(sprite);
        m_sprites[state] = sprite;
    };

    // Idle is the base state: if no image is assigned, use the fallback arrow
    // so enabling the feature always renders something visible.
    CCSprite* idle = loadSprite(m_config.idleImage);
    if (!idle) idle = createFallbackSprite();
    attachSprite(CursorState::Idle, idle);

    // Other states are only created when they have an assigned image;
    // resolveActiveState falls back to Idle automatically.
    attachSprite(CursorState::Move,     loadSprite(m_config.moveImage));
    attachSprite(CursorState::Hover,    loadSprite(m_config.hoverImage));
    attachSprite(CursorState::Click,    loadSprite(m_config.clickImage));
    attachSprite(CursorState::Text,     loadSprite(m_config.textImage));
    attachSprite(CursorState::Disabled, loadSprite(m_config.disabledImage));

    // Show the initial base state.
    if (auto* base = spriteForState(CursorState::Idle)) {
        base->setVisible(true);
    }

    updateTrail();
}

// Hover / state resolution
bool CursorManager::isCursorOverButton(CCPoint const& worldPos) const {
    auto* scene = CCDirector::get()->getRunningScene();
    if (!scene) return false;
    CursorContext ctx;
    scanCursorContext(scene, worldPos, 0, ctx);
    return ctx.overButton;
}

CursorState CursorManager::resolveActiveState(CCPoint const& mouseWorld) const {
    // Priority: Click > Disabled > Text > Hover > Move > Idle.
    // Un estado solo "gana" si tiene un sprite cargado; si no, seguimos bajando.

    // Click no depende del contexto de escena, asi que se evalua primero.
    if (m_config.clickEnabled && m_mouseDown && spriteForState(CursorState::Click)) {
        return CursorState::Click;
    }

    // Inspect the context under the cursor once (expensive at 60fps,
    // but only when a contextual state has a sprite loaded).
    bool needContext =
        (m_config.disabledEnabled && spriteForState(CursorState::Disabled)) ||
        (m_config.textEnabled     && spriteForState(CursorState::Text)) ||
        (m_config.hoverEnabled    && spriteForState(CursorState::Hover));

    if (needContext) {
        if (auto* scene = CCDirector::get()->getRunningScene()) {
            CursorContext ctx;
            scanCursorContext(scene, mouseWorld, 0, ctx);

            if (m_config.disabledEnabled && ctx.overDisabled &&
                spriteForState(CursorState::Disabled)) {
                return CursorState::Disabled;
            }
            if (m_config.textEnabled && ctx.overText &&
                spriteForState(CursorState::Text)) {
                return CursorState::Text;
            }
            if (m_config.hoverEnabled && ctx.overButton &&
                spriteForState(CursorState::Hover)) {
                return CursorState::Hover;
            }
        }
    }

    if (m_isMoving && spriteForState(CursorState::Move)) {
        return CursorState::Move;
    }
    return CursorState::Idle;
}

// Attach / Detach
void CursorManager::attachToOverlay() {
    log::debug("[CursorManager] attachToOverlay");
    if (!m_config.enabled) return;

    auto* overlay = OverlayManager::get();
    if (!overlay) return;

    detachFromScene();

    m_cursorNode = CCNode::create();
    m_cursorNode->setID("paimon-cursor-host"_spr);
    m_cursorNode->setZOrder(kCursorBaseZOrder);
    overlay->addChild(m_cursorNode);

    bool insideWindow = true;
    if (!sampleCursorPosition(m_currentPos, insideWindow)) {
        auto winSize = CCDirector::get()->getWinSize();
        m_currentPos = ccp(winSize.width / 2.f, winSize.height / 2.f);
    }
    m_velocity   = ccp(0.f, 0.f);
    m_isMoving   = false;
    m_moveTimer  = 0.f;

    reloadSprites();
    // Start hidden: update() makes the first correct visibility decision (scene
    // filter, gameplay state, window bounds) on the very next frame, which
    // avoids a one-frame flash at a stale position.
    m_cursorNode->setVisible(false);
    if (m_trail) m_trail->setPosition(m_currentPos);
}

void CursorManager::detachFromScene() {
    if (m_cursorNode) {
        m_cursorNode->removeFromParent();
        m_cursorNode  = nullptr;
        m_sprites.clear();
        m_trail       = nullptr;
    }
    syncSystemCursorVisibility(false);
}

// Re-render the cursor node on top of any ImGui pass.
//
// Why: gd-imgui-cocos hooks cocos2d::CCEGLView::swapBuffers and runs
//   ImGuiCocos::drawFrame() BEFORE the original swap, which means every ImGui
//   menu (EclipseMenu, OpenHack, GD Mega Overlay, ...) ends up painted on top
//   of the entire cocos2d scene graph -- including OverlayManager. Re-ordering
//   our cursor inside cocos2d (INT_MAX z-order, re-add as last child every
//   frame) cannot beat that, because ImGui is a separate render pass that
//   runs AFTER cocos2d's whole frame.
//
// How: this function is invoked from a CCEGLView::swapBuffers hook installed
//   with Priority::VeryLate (see src/hooks/CCEGLView.cpp). VeryLate runs
//   closer to the original than imgui-cocos's Early/default priority, so by
//   the time we get here:
//     1. cocos2d already drew the scene + OverlayManager (cursor first time).
//     2. imgui-cocos already drew its ImGui frame on top.
//     3. The original swapBuffers has NOT been called yet, so anything we draw
//        now lands on the back buffer above ImGui and is presented next swap.
//
// Cost: the cursor node is one sprite (or a few state sprites of which only
//   one is visible per frame) plus an optional CCMotionStreak. Visiting it
//   twice per frame is negligible, and we deliberately skip the call when
//   the cursor is disabled, hidden, or off-window so we do not waste a draw.
void CursorManager::renderOverlay() {
    // Cheap fast-paths first so this is essentially free when the feature is
    // off or the cursor is currently hidden by update().
    if (!m_config.enabled) return;
    if (!m_cursorNode) return;
    if (!m_cursorNode->getParent()) return;
    if (!m_cursorNode->isVisible()) return;
    if (!hasLoadedCursorVisual()) return;

    // m_cursorNode is parented to OverlayManager (a top-level CCNode at
    // identity transform), and its sprites use absolute window coordinates,
    // so calling visit() directly draws them at the correct screen position
    // using the current cocos2d projection matrix (which CCDirector left in
    // place after drawScene()). CCSprite::draw() and CCMotionStreak::draw()
    // both rebind their own shader, blend func, texture and vertex attribs,
    // so any GL state ImGui left behind is irrelevant to the result.
    m_cursorNode->visit();
}

void CursorManager::releaseSharedResources() {
    detachFromScene();
    (void)whiteTrailTexture().take();
    (void)fallbackCursorTexture().take();
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

// Update (every frame)
void CursorManager::update(float dt) {
    if (!m_config.enabled || !m_cursorNode) return;

    // Safety: lost parent during scene transition
    if (!m_cursorNode->getParent()) {
        detachFromScene();
        return;
    }

    CCPoint newPos;
    bool insideWindow = true;
    if (!sampleCursorPosition(newPos, insideWindow)) return;

    // Hide during gameplay if the user has disabled the native cursor or the mod
    // option. Mirrors Ecuet: only show in PlayLayer while paused or on the
    // retry/complete overlays.
    bool hideInGameplay = false;
    if (auto* pl = PlayLayer::get()) {
        bool nativeHide = !GameManager::get()->getGameVariable("0024"); // GameVar::ShowCursor
        bool modHide = paimon::settings::cursor::hideInGameplay();

        bool inMenuOverlay = pl->m_isPaused
            || pl->getChildByType<RetryLevelLayer>(0) != nullptr
            || pl->getChildByType<EndLevelLayer>(0) != nullptr;

        hideInGameplay = (nativeHide || modHide) && !inMenuOverlay;
    }

    // Single visibility decision per frame. The trail and all state sprites are
    // children of the host node, so toggling ONLY the host hides/shows the whole
    // cursor without destroying and rebuilding the CCMotionStreak — which is
    // what produced the per-frame flicker before.
    bool show = insideWindow && !hideInGameplay &&
                shouldShowOnCurrentScene() && hasLoadedCursorVisual();

    if (!show) {
        if (m_cursorNode->isVisible()) m_cursorNode->setVisible(false);
        syncSystemCursorVisibility(false);
        return;
    }

    if (!m_cursorNode->isVisible()) {
        m_cursorNode->setVisible(true);
        m_currentPos = newPos;          // snap on reappear (no lerp dash)
        if (m_trail) m_trail->reset();  // clear stale points (no streak jump)
    }

    // Re-hide the OS cursor every frame: GD/OS re-assert the native cursor on
    // focus/scene changes, so a one-shot hide lets the arrow bleed through.
    PlatformToolbox::hideCursor();
    m_systemCursorHidden = true;

    // Ensure cursor is always on top, even when mods like Eclipse Menu (which uses
    // ImGui) add overlays. We need to reorder every frame because:
    // 1. ImGui layers are added dynamically with high z-orders
    // 2. Other mods may add popups at any time
    // 3. Simply setting INT_MAX once isn't enough if something is added after us
    // Solution: Move cursor to the END of parent's children array every frame
    if (auto* parent = m_cursorNode->getParent()) {
        auto children = parent->getChildren();
        if (children && children->count() > 0) {
            auto* lastChild = static_cast<CCNode*>(children->lastObject());
            if (lastChild != m_cursorNode) {
                // Cursor is not the last child, move it to the end
                m_cursorNode->retain();  // Prevent deallocation during move
                m_cursorNode->removeFromParentAndCleanup(false);
                parent->addChild(m_cursorNode, INT_MAX);  // Re-add with max z-order
                m_cursorNode->release();  // Balance the retain
            }
        }
    }

    CCPoint prevPos = m_currentPos;
    m_targetPos     = newPos;

    // Follow delay: lerp towards the target (0.0 = instant, 1.0 = very slow)
    if (m_config.followDelayEnabled && m_config.followDelay > 0.f) {
        float lerpSpeed = (1.f - m_config.followDelay) * 25.f + 1.f;
        float t = std::min(1.f, lerpSpeed * dt);
        m_currentPos.x += (m_targetPos.x - m_currentPos.x) * t;
        m_currentPos.y += (m_targetPos.y - m_currentPos.y) * t;
    } else {
        m_currentPos = newPos;
    }

    m_velocity.x = (m_currentPos.x - prevPos.x) / std::max(dt, 0.001f);
    m_velocity.y = (m_currentPos.y - prevPos.y) / std::max(dt, 0.001f);
    float speed  = std::sqrt(m_velocity.x * m_velocity.x + m_velocity.y * m_velocity.y);

    // "Moving" stays true for 0.15s after the last movement above threshold
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

    // Decide el estado activo. Throttle context scan to every 2 frames
    // since hover detection doesn't need 60fps precision.
    static int s_contextFrame = 0;
    static CursorState s_cachedState = CursorState::Idle;
    if (++s_contextFrame % 2 == 0 || m_mouseDown) {
        s_cachedState = resolveActiveState(newPos);
    }
    CursorState active = s_cachedState;
    for (auto state : kAllStates) {
        if (auto* sprite = spriteForState(state)) {
            sprite->setVisible(state == active);
            sprite->setPosition(m_currentPos);
        }
    }

    if (m_trail) m_trail->setPosition(m_currentPos);
}

// Apply config live
void CursorManager::applyConfigLive() {
    m_config.scale = clampCursorScale(m_config.scale);

    for (auto& [state, sprite] : m_sprites) {
        applyCursorVisual(sprite, m_config.scale, m_config.opacity);
    }
    updateTrail();
    saveConfig();
}

// Trail
void CursorManager::updateTrail() {
    if (m_trail && m_cursorNode) {
        m_trail->removeFromParent();
        m_trail = nullptr;
    }

    if (!m_config.trailEnabled || !m_cursorNode || !hasLoadedCursorVisual()) return;

    auto& trailTex = whiteTrailTexture();
    if (!trailTex) {
        const int sz = 2;
        uint8_t pixels[sz * sz * 4];
        memset(pixels, 255, sizeof(pixels));
        auto* newTex = new CCTexture2D();
        if (newTex->initWithData(pixels, kCCTexture2DPixelFormat_RGBA8888, sz, sz, CCSizeMake(sz, sz))) {
            // Ref::adopt takes ownership of refcount=1 without an extra retain.
            // Without adopt, `trailTex = newTex` retains via Ref::operator=,
            // leaving refcount=2 — permanent leak.
            trailTex = geode::Ref<CCTexture2D>::adopt(newTex);
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
        // Keep the streak's own flag visible; the host node is the single
        // gate for show/hide, so the trail never needs to be rebuilt.
        m_trail->setVisible(true);
        m_trail->setPosition(m_currentPos);
    } else {
        m_trail = nullptr;
        log::warn("[CursorManager] Failed to create trail with valid texture");
    }
}
