#include "PetManager.hpp"
#include "../../../utils/ImageLoadHelper.hpp"
#include "../../../utils/AnimatedGIFSprite.hpp"
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/file.hpp>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/string.hpp>

#if defined(GEODE_IS_WINDOWS)
#include <windows.h>
#endif

using namespace geode::prelude;
using namespace cocos2d;

namespace {
geode::Ref<CCTexture2D>& whiteTrailTexture() {
    static geode::Ref<CCTexture2D> s_whiteTrailTex = nullptr;
    return s_whiteTrailTex;
}

constexpr int kPetBaseZOrder = 99999;
constexpr int kPetMaxZOrder = std::numeric_limits<int>::max() - 1024;

int desiredPetHostZ(CCScene* scene, CCNode* petNode) {
    if (!scene) return kPetBaseZOrder;
    int highest = kPetBaseZOrder;
    for (auto* child : CCArrayExt<CCNode*>(scene->getChildren())) {
        if (!child || child == petNode) continue;
        highest = std::max(highest, child->getZOrder());
    }
    if (highest >= kPetMaxZOrder) return kPetMaxZOrder;
    return highest + 1;
}

void ensurePetNodeIsFrontmost(CCScene* scene, CCNode* petNode) {
    if (!scene || !petNode || petNode->getParent() != scene) return;
    auto targetZ = desiredPetHostZ(scene, petNode);
    if (petNode->getZOrder() != targetZ) {
        scene->reorderChild(petNode, targetZ);
    }
}

bool layerNameMatchesNode(CCNode* node, std::string const& layerName) {
    if (!node) return false;

    std::string className = typeid(*node).name();
    if (className.rfind("class ", 0) == 0) {
        className = className.substr(6);
    }

    std::string const nodeID = node->getID();
    return className.find(layerName) != std::string::npos ||
           (!nodeID.empty() && nodeID.find(layerName) != std::string::npos);
}

bool allNonGameplayLayersSelected(std::set<std::string> const& selectedLayers) {
    for (auto const& opt : PET_LAYER_OPTIONS) {
        if (isPetGameplayLayer(opt)) continue;
        if (selectedLayers.count(opt) == 0) {
            return false;
        }
    }
    return true;
}

bool sceneMatchesAnyLayer(CCScene* scene, std::vector<std::string> const& layerOptions) {
    if (!scene) return false;
    auto children = scene->getChildren();
    if (!children) return false;

    for (auto* child : CCArrayExt<CCNode*>(children)) {
        if (!child) continue;
        for (auto const& layerName : layerOptions) {
            if (layerNameMatchesNode(child, layerName)) {
                return true;
            }
        }
    }

    return false;
}
}

// ════════════════════════════════════════════════════════════
// singleton
// ════════════════════════════════════════════════════════════

PetManager& PetManager::get() {
    static PetManager inst;
    return inst;
}

// ════════════════════════════════════════════════════════════
// paths
// ════════════════════════════════════════════════════════════

std::filesystem::path PetManager::configPath() const {
    return Mod::get()->getSaveDir() / "pet_config.json";
}

std::filesystem::path PetManager::galleryDir() const {
    auto dir = Mod::get()->getSaveDir() / "pet_gallery";
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) {
        std::filesystem::create_directories(dir, ec);
    }
    return dir;
}

// ════════════════════════════════════════════════════════════
// config persistence
// ════════════════════════════════════════════════════════════

void PetManager::loadConfig() {
    log::debug("[PetManager] loadConfig");
    auto path = configPath();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return;

    auto rawRes = file::readString(path);
    if (!rawRes) {
        log::error("[PetManager] Failed to open config file");
        return;
    }
    auto res = matjson::parse(rawRes.unwrap());
    if (res.isErr()) return;
    auto j = res.unwrap();
    bool const hasAllLayersKey = j.contains("allLayers");

        m_config.enabled         = j["enabled"].asBool().unwrapOr(false);
        m_config.scale           = static_cast<float>(j["scale"].asDouble().unwrapOr(0.5));
        m_config.sensitivity     = static_cast<float>(j["sensitivity"].asDouble().unwrapOr(0.12));
        m_config.opacity         = static_cast<int>(j["opacity"].asInt().unwrapOr(220));
        m_config.bounceHeight    = static_cast<float>(j["bounceHeight"].asDouble().unwrapOr(4.0));
        m_config.bounceSpeed     = static_cast<float>(j["bounceSpeed"].asDouble().unwrapOr(3.0));
        m_config.rotationDamping = static_cast<float>(j["rotationDamping"].asDouble().unwrapOr(0.3));
        m_config.maxTilt         = static_cast<float>(j["maxTilt"].asDouble().unwrapOr(15.0));
        m_config.flipOnDirection = j["flipOnDirection"].asBool().unwrapOr(true);
        m_config.showTrail       = j["showTrail"].asBool().unwrapOr(false);
        m_config.trailLength     = static_cast<float>(j["trailLength"].asDouble().unwrapOr(30.0));
        m_config.trailWidth      = static_cast<float>(j["trailWidth"].asDouble().unwrapOr(6.0));
        m_config.idleAnimation   = j["idleAnimation"].asBool().unwrapOr(true);
        m_config.bounce          = j["bounce"].asBool().unwrapOr(true);
        m_config.idleBreathScale = static_cast<float>(j["idleBreathScale"].asDouble().unwrapOr(0.04));
        m_config.idleBreathSpeed = static_cast<float>(j["idleBreathSpeed"].asDouble().unwrapOr(1.5));
        m_config.selectedImage   = j["selectedImage"].asString().unwrapOr("");
        m_config.squishOnLand    = j["squishOnLand"].asBool().unwrapOr(true);
        m_config.squishAmount    = static_cast<float>(j["squishAmount"].asDouble().unwrapOr(0.15));
        m_config.offsetX         = static_cast<float>(j["offsetX"].asDouble().unwrapOr(0.0));
        m_config.offsetY         = static_cast<float>(j["offsetY"].asDouble().unwrapOr(25.0));
        m_config.showInGameplay  = j["showInGameplay"].asBool().unwrapOr(true);

        // visible layers
        auto layersArr = j["visibleLayers"].asArray();
        if (layersArr.isOk()) {
            m_config.visibleLayers.clear();
            for (auto& v : layersArr.unwrap()) {
                auto s = v.asString().unwrapOr("");
                if (!s.empty()) m_config.visibleLayers.insert(s);
            }
        }
        m_config.allLayers = hasAllLayersKey
            ? j["allLayers"].asBool().unwrapOr(true)
            : allNonGameplayLayersSelected(m_config.visibleLayers);
        // if key not present, keep the default (all PET_LAYER_OPTIONS active)

        // ── icon states ──
        m_config.idleImage  = j["idleImage"].asString().unwrapOr("");
        m_config.walkImage  = j["walkImage"].asString().unwrapOr("");
        m_config.sleepImage = j["sleepImage"].asString().unwrapOr("");
        m_config.reactImage = j["reactImage"].asString().unwrapOr("");

        // ── shadow ──
        m_config.showShadow    = j["showShadow"].asBool().unwrapOr(true);
        m_config.shadowOffsetX = static_cast<float>(j["shadowOffsetX"].asDouble().unwrapOr(3.0));
        m_config.shadowOffsetY = static_cast<float>(j["shadowOffsetY"].asDouble().unwrapOr(-5.0));
        m_config.shadowOpacity = static_cast<int>(j["shadowOpacity"].asInt().unwrapOr(60));
        m_config.shadowScale   = static_cast<float>(j["shadowScale"].asDouble().unwrapOr(1.1));

        // ── particles ──
        m_config.showParticles    = j["showParticles"].asBool().unwrapOr(false);
        m_config.particleType     = static_cast<int>(j["particleType"].asInt().unwrapOr(0));
        m_config.particleRate     = static_cast<float>(j["particleRate"].asDouble().unwrapOr(5.0));
        m_config.particleSize     = static_cast<float>(j["particleSize"].asDouble().unwrapOr(3.0));
        m_config.particleGravity  = static_cast<float>(j["particleGravity"].asDouble().unwrapOr(-15.0));
        m_config.particleLifetime = static_cast<float>(j["particleLifetime"].asDouble().unwrapOr(1.5));
        if (j.contains("particleColor") && j["particleColor"].isObject()) {
            auto& pc = j["particleColor"];
            m_config.particleColor = cocos2d::ccc3(
                static_cast<GLubyte>(pc["r"].asInt().unwrapOr(255)),
                static_cast<GLubyte>(pc["g"].asInt().unwrapOr(255)),
                static_cast<GLubyte>(pc["b"].asInt().unwrapOr(255))
            );
        }

        // ── speech bubbles ──
        m_config.enableSpeech     = j["enableSpeech"].asBool().unwrapOr(false);
        m_config.speechInterval   = static_cast<float>(j["speechInterval"].asDouble().unwrapOr(30.0));
        m_config.speechDuration   = static_cast<float>(j["speechDuration"].asDouble().unwrapOr(3.0));
        m_config.speechBubbleScale = static_cast<float>(j["speechBubbleScale"].asDouble().unwrapOr(0.5));
        auto loadStrArr = [&](char const* key, std::vector<std::string>& target) {
            auto arr = j[key].asArray();
            if (arr.isOk()) {
                target.clear();
                for (auto& v : arr.unwrap()) {
                    auto s = v.asString().unwrapOr("");
                    if (!s.empty()) target.push_back(s);
                }
            }
        };
        loadStrArr("idleMessages", m_config.idleMessages);
        loadStrArr("levelCompleteMessages", m_config.levelCompleteMessages);
        loadStrArr("deathMessages", m_config.deathMessages);

        // ── sleep mode ──
        m_config.enableSleep      = j["enableSleep"].asBool().unwrapOr(true);
        m_config.sleepAfterSeconds = static_cast<float>(j["sleepAfterSeconds"].asDouble().unwrapOr(60.0));
        m_config.sleepBobAmount   = static_cast<float>(j["sleepBobAmount"].asDouble().unwrapOr(3.0));

        // ── click interaction ──
        m_config.enableClickInteraction = j["enableClickInteraction"].asBool().unwrapOr(true);
        m_config.clickReactionDuration  = static_cast<float>(j["clickReactionDuration"].asDouble().unwrapOr(1.5));
        m_config.clickJumpHeight        = static_cast<float>(j["clickJumpHeight"].asDouble().unwrapOr(20.0));
        loadStrArr("clickMessages", m_config.clickMessages);

        // ── game event reactions ──
        m_config.reactToLevelComplete = j["reactToLevelComplete"].asBool().unwrapOr(true);
        m_config.reactToDeath         = j["reactToDeath"].asBool().unwrapOr(true);
        m_config.reactToPracticeExit  = j["reactToPracticeExit"].asBool().unwrapOr(true);
        m_config.reactionDuration     = static_cast<float>(j["reactionDuration"].asDouble().unwrapOr(2.0));
        m_config.reactionJumpHeight   = static_cast<float>(j["reactionJumpHeight"].asDouble().unwrapOr(30.0));
        m_config.reactionSpinSpeed    = static_cast<float>(j["reactionSpinSpeed"].asDouble().unwrapOr(360.0));
}

void PetManager::saveConfig() {
    matjson::Value j = matjson::Value();
    j["enabled"]         = m_config.enabled;
        j["scale"]           = static_cast<double>(m_config.scale);
        j["sensitivity"]     = static_cast<double>(m_config.sensitivity);
        j["opacity"]         = m_config.opacity;
        j["bounceHeight"]    = static_cast<double>(m_config.bounceHeight);
        j["bounceSpeed"]     = static_cast<double>(m_config.bounceSpeed);
        j["rotationDamping"] = static_cast<double>(m_config.rotationDamping);
        j["maxTilt"]         = static_cast<double>(m_config.maxTilt);
        j["flipOnDirection"] = m_config.flipOnDirection;
        j["showTrail"]       = m_config.showTrail;
        j["trailLength"]     = static_cast<double>(m_config.trailLength);
        j["trailWidth"]      = static_cast<double>(m_config.trailWidth);
        j["idleAnimation"]   = m_config.idleAnimation;
        j["bounce"]          = m_config.bounce;
        j["idleBreathScale"] = static_cast<double>(m_config.idleBreathScale);
        j["idleBreathSpeed"] = static_cast<double>(m_config.idleBreathSpeed);
        j["selectedImage"]   = m_config.selectedImage;
        j["squishOnLand"]    = m_config.squishOnLand;
        j["squishAmount"]    = static_cast<double>(m_config.squishAmount);
        j["offsetX"]         = static_cast<double>(m_config.offsetX);
        j["offsetY"]         = static_cast<double>(m_config.offsetY);
        j["allLayers"]       = m_config.allLayers;
        j["showInGameplay"]  = m_config.showInGameplay;

        // visible layers
        matjson::Value layers = matjson::Value::array();
        for (auto& l : m_config.visibleLayers) {
            layers.push(l);
        }
        j["visibleLayers"] = layers;

        // ── icon states ──
        j["idleImage"]  = m_config.idleImage;
        j["walkImage"]  = m_config.walkImage;
        j["sleepImage"] = m_config.sleepImage;
        j["reactImage"] = m_config.reactImage;

        // ── shadow ──
        j["showShadow"]    = m_config.showShadow;
        j["shadowOffsetX"] = static_cast<double>(m_config.shadowOffsetX);
        j["shadowOffsetY"] = static_cast<double>(m_config.shadowOffsetY);
        j["shadowOpacity"] = m_config.shadowOpacity;
        j["shadowScale"]   = static_cast<double>(m_config.shadowScale);

        // ── particles ──
        j["showParticles"]    = m_config.showParticles;
        j["particleType"]     = m_config.particleType;
        j["particleRate"]     = static_cast<double>(m_config.particleRate);
        j["particleSize"]     = static_cast<double>(m_config.particleSize);
        j["particleGravity"]  = static_cast<double>(m_config.particleGravity);
        j["particleLifetime"] = static_cast<double>(m_config.particleLifetime);
        {
            matjson::Value pc = matjson::Value();
            pc["r"] = static_cast<int>(m_config.particleColor.r);
            pc["g"] = static_cast<int>(m_config.particleColor.g);
            pc["b"] = static_cast<int>(m_config.particleColor.b);
            j["particleColor"] = pc;
        }

        // ── speech bubbles ──
        j["enableSpeech"]      = m_config.enableSpeech;
        j["speechInterval"]    = static_cast<double>(m_config.speechInterval);
        j["speechDuration"]    = static_cast<double>(m_config.speechDuration);
        j["speechBubbleScale"] = static_cast<double>(m_config.speechBubbleScale);
        {
            matjson::Value arr = matjson::Value::array();
            for (auto& s : m_config.idleMessages) arr.push(s);
            j["idleMessages"] = arr;
        }
        {
            matjson::Value arr = matjson::Value::array();
            for (auto& s : m_config.levelCompleteMessages) arr.push(s);
            j["levelCompleteMessages"] = arr;
        }
        {
            matjson::Value arr = matjson::Value::array();
            for (auto& s : m_config.deathMessages) arr.push(s);
            j["deathMessages"] = arr;
        }

        // ── sleep mode ──
        j["enableSleep"]       = m_config.enableSleep;
        j["sleepAfterSeconds"] = static_cast<double>(m_config.sleepAfterSeconds);
        j["sleepBobAmount"]    = static_cast<double>(m_config.sleepBobAmount);

        // ── click interaction ──
        j["enableClickInteraction"] = m_config.enableClickInteraction;
        j["clickReactionDuration"]   = static_cast<double>(m_config.clickReactionDuration);
        j["clickJumpHeight"]        = static_cast<double>(m_config.clickJumpHeight);
        {
            matjson::Value arr = matjson::Value::array();
            for (auto& s : m_config.clickMessages) arr.push(s);
            j["clickMessages"] = arr;
        }

        // ── game event reactions ──
        j["reactToLevelComplete"] = m_config.reactToLevelComplete;
        j["reactToDeath"]         = m_config.reactToDeath;
        j["reactToPracticeExit"]  = m_config.reactToPracticeExit;
        j["reactionDuration"]     = static_cast<double>(m_config.reactionDuration);
        j["reactionJumpHeight"]   = static_cast<double>(m_config.reactionJumpHeight);
        j["reactionSpinSpeed"]    = static_cast<double>(m_config.reactionSpinSpeed);

        auto str = j.dump();
        auto writeRes = file::writeString(configPath(), str);
        if (!writeRes) {
            log::error("[PetManager] Failed to write config file: {}", writeRes.unwrapErr());
            return;
        }
}

// ════════════════════════════════════════════════════════════
// layer visibility check
// ════════════════════════════════════════════════════════════

bool PetManager::shouldShowOnCurrentScene() const {
    auto scene = CCDirector::sharedDirector()->getRunningScene();
    if (!scene) return false;

    if (sceneMatchesAnyLayer(scene, PET_GAMEPLAY_LAYER_OPTIONS)) {
        return m_config.showInGameplay;
    }

    if (m_config.allLayers) {
        return true;
    }

    if (m_config.visibleLayers.empty()) return false;

    auto children = scene->getChildren();
    if (!children) return false;

    for (auto* child : CCArrayExt<CCNode*>(children)) {
        if (!child) continue;
        for (auto const& layer : m_config.visibleLayers) {
            if (layerNameMatchesNode(child, layer)) {
                return true;
            }
        }
    }
    return false;
}

// ════════════════════════════════════════════════════════════
// gallery
// ════════════════════════════════════════════════════════════

std::vector<std::string> PetManager::getGalleryImages() const {
    std::vector<std::string> result;
    auto dir = galleryDir();
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || ec) return result;

    for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        auto ext = geode::utils::string::pathToString(entry.path().extension());
        ext = geode::utils::string::toLower(ext);
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".gif" || ext == ".bmp" || ext == ".webp"
            || ext == ".tiff" || ext == ".tif" || ext == ".tga" || ext == ".psd" || ext == ".qoi" || ext == ".jxl") {
            result.push_back(geode::utils::string::pathToString(entry.path().filename()));
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::string PetManager::addToGallery(std::filesystem::path const& srcPath) {
    auto dir = galleryDir();
    auto filename = geode::utils::string::pathToString(srcPath.filename());

    // evitar colisiones de nombre
    auto dest = dir / filename;
    int counter = 1;
    std::error_code existsEc;
    while (std::filesystem::exists(dest, existsEc) && !existsEc) {
        auto stem = geode::utils::string::pathToString(srcPath.stem());
        auto ext = geode::utils::string::pathToString(srcPath.extension());
        filename = fmt::format("{}_{}{}", stem, counter++, ext);
        dest = dir / filename;
    }

    std::error_code copyEc;
    std::filesystem::copy_file(srcPath, dest, std::filesystem::copy_options::overwrite_existing, copyEc);
    if (copyEc) {
        log::error("[PetManager] Failed to copy to gallery: {}", copyEc.message());
        return "";
    }
    return filename;
}

void PetManager::removeFromGallery(std::string const& filename) {
    auto path = galleryDir() / filename;
    std::error_code rmEc;
    if (std::filesystem::exists(path, rmEc)) {
        std::filesystem::remove(path, rmEc);
    }

    // si es la imagen actual, deseleccionar
    if (m_config.selectedImage == filename) {
        m_config.selectedImage = "";
        saveConfig();
        reloadSprite();
    }
}

void PetManager::removeAllFromGallery() {
    auto images = getGalleryImages();
    for (auto& img : images) {
        auto path = galleryDir() / img;
        std::error_code rmAllEc;
        if (std::filesystem::exists(path, rmAllEc)) {
            std::filesystem::remove(path, rmAllEc);
        }
    }

    // deseleccionar imagen actual
    if (!m_config.selectedImage.empty()) {
        m_config.selectedImage = "";
        saveConfig();
        reloadSprite();
    }
}

int PetManager::cleanupInvalidImages() {
    auto images = getGalleryImages();
    int removed = 0;

    for (auto& img : images) {
        auto path = galleryDir() / img;
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) continue;

        // leer primeros bytes para verificar magic bytes
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) {
                removeFromGallery(img);
                removed++;
                continue;
            }

            unsigned char header[12] = {};
            f.read(reinterpret_cast<char*>(header), 12);
            auto bytesRead = f.gcount();
            f.close();

            if (bytesRead < 4) {
                log::warn("[PetManager] File too small, removing: {}", img);
                removeFromGallery(img);
                removed++;
                continue;
            }

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

            if (!valid) {
                log::warn("[PetManager] Invalid image file, removing: {} (bytes: {:02x} {:02x} {:02x} {:02x})",
                    img, header[0], header[1], header[2], header[3]);
                removeFromGallery(img);
                removed++;
            }
    }

    return removed;
}

CCTexture2D* PetManager::loadGalleryThumb(std::string const& filename) const {
    auto path = galleryDir() / filename;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) return nullptr;

    auto img = ImageLoadHelper::loadStaticImage(path);
    if (img.success && img.texture) {
        return img.texture; // caller manages lifetime
    }
    return nullptr;
}

// ════════════════════════════════════════════════════════════
// init / lifecycle
// ════════════════════════════════════════════════════════════

void PetManager::init() {
    log::info("[PetManager] init");
    loadConfig();
}

void PetManager::setImage(std::string const& galleryFilename) {
    log::info("[PetManager] setImage: {}", galleryFilename);
    m_config.selectedImage = galleryFilename;
    saveConfig();
    reloadSprite();
}

void PetManager::createPetSprite() {
    if (m_config.selectedImage.empty()) return;

    auto path = galleryDir() / m_config.selectedImage;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) return;

    m_petSprite = ImageLoadHelper::loadAnimatedOrStatic(path, 10,
        [](std::string const& p) -> CCSprite* {
            return AnimatedGIFSprite::create(p);
        });
}

void PetManager::reloadSprite() {
    // remove old sprite
    if (m_petSprite && m_petNode) {
        m_petSprite->removeFromParent();
        m_petSprite = nullptr;
    }
    if (m_trail && m_petNode) {
        m_trail->removeFromParent();
        m_trail = nullptr;
    }
    if (m_shadowSprite && m_petNode) {
        m_shadowSprite->removeFromParent();
        m_shadowSprite = nullptr;
    }
    if (m_particleNode && m_petNode) {
        m_particleNode->removeFromParent();
        m_particleNode = nullptr;
    }
    if (m_speechNode && m_petNode) {
        m_speechNode->removeFromParent();
        m_speechNode = nullptr;
        m_speechLabel = nullptr;
        m_speechBg = nullptr;
    }
    if (m_sleepZzz && m_petNode) {
        m_sleepZzz->removeFromParent();
        m_sleepZzz = nullptr;
    }

    if (!m_config.enabled || m_config.selectedImage.empty()) return;
    if (!m_petNode) return;

    createPetSprite();
    if (m_petSprite) {
        m_petSprite->setScale(m_config.scale);
        m_petSprite->setOpacity(static_cast<GLubyte>(m_config.opacity));
        m_petNode->addChild(m_petSprite, 10);

        updateTrail();
        createShadow();
        createParticleNode();
        createSpeechBubbleNode();
    }

    // reset icon state
    m_iconState = PetIconState::Default;
    m_sleeping = false;
    m_idleDuration = 0.f;
    m_reactionTimer = 0.f;
    m_clickReactionTimer = 0.f;
    m_speechTimer = 0.f;
    m_speechIdleAccum = 0.f;
}

void PetManager::attachToScene(CCScene* scene) {
    log::debug("[PetManager] attachToScene: enabled={}", m_config.enabled);
    if (!scene) return;

    // remove from old scene
    detachFromScene();

    if (!m_config.enabled) return;

    m_petNode = CCNode::create();
    m_petNode->setID("paimon-pet-host"_spr);
    m_petNode->setZOrder(99999); // always on top
    scene->addChild(m_petNode);

    // init position to center
    auto winSize = CCDirector::sharedDirector()->getWinSize();
    m_currentPos = ccp(winSize.width / 2.f, winSize.height / 2.f);
    m_targetPos = m_currentPos;
    m_velocity = ccp(0, 0);

    reloadSprite();
}

void PetManager::detachFromScene() {
    if (m_petNode) {
        m_petNode->removeFromParent();
        m_petNode = nullptr;
        m_petSprite = nullptr;
        m_trail = nullptr;
        m_shadowSprite = nullptr;
        m_particleNode = nullptr;
        m_speechNode = nullptr;
        m_speechLabel = nullptr;
        m_speechBg = nullptr;
        m_sleepZzz = nullptr;
    }
    m_sleeping = false;
    m_reactionTimer = 0.f;
    m_clickReactionTimer = 0.f;
    m_speechTimer = 0.f;
}

void PetManager::releaseSharedResources() {
    whiteTrailTexture() = nullptr;
}

// ════════════════════════════════════════════════════════════
// update (called every frame)
// ════════════════════════════════════════════════════════════

void PetManager::update(float dt) {
    if (!m_config.enabled || !m_petNode || !m_petSprite) return;

    // safety: if petNode lost its parent (scene transition), clean up
    if (!m_petNode->getParent()) {
        m_petNode = nullptr;
        m_petSprite = nullptr;
        m_trail = nullptr;
        m_shadowSprite = nullptr;
        m_particleNode = nullptr;
        m_speechNode = nullptr;
        m_speechLabel = nullptr;
        m_speechBg = nullptr;
        m_sleepZzz = nullptr;
        return;
    }

    // ensure pet is always on top
    if (auto* scene = m_petNode->getParentByType<CCScene>()) {
        ensurePetNodeIsFrontmost(scene, m_petNode);
    }

    // ── get cursor/touch position (Geode API) ──
    auto mousePos = geode::cocos::getMousePos();
    m_targetPos.x = mousePos.x + m_config.offsetX;
    m_targetPos.y = mousePos.y + m_config.offsetY;

    // ── click detection (mouse button polling) ──
    if (m_config.enableClickInteraction && m_petSprite) {
#if defined(GEODE_IS_WINDOWS)
        bool mouseDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
#else
        // macOS / mobile: click detection not supported via polling
        bool mouseDown = false;
#endif
        if (mouseDown && !m_mouseWasDown) {
            // mouse just pressed — check if click is near pet
            triggerClickReaction(m_targetPos);
        }
        m_mouseWasDown = mouseDown;
    }

    // ── smooth interpolation (lerp with sensitivity) ──
    float lerpFactor = 1.f - std::pow(1.f - m_config.sensitivity, dt * 60.f);
    lerpFactor = std::max(0.001f, std::min(1.f, lerpFactor));

    CCPoint prevPos = m_currentPos;
    m_currentPos.x += (m_targetPos.x - m_currentPos.x) * lerpFactor;
    m_currentPos.y += (m_targetPos.y - m_currentPos.y) * lerpFactor;

    m_velocity.x = (m_currentPos.x - prevPos.x) / std::max(dt, 0.001f);
    m_velocity.y = (m_currentPos.y - prevPos.y) / std::max(dt, 0.001f);

    float speed = std::sqrt(m_velocity.x * m_velocity.x + m_velocity.y * m_velocity.y);

    // ── walking detection ──
    m_wasWalking = m_walking;
    m_walking = speed > 8.f;

    // ── idle duration tracking ──
    if (m_walking || m_reactionTimer > 0.f || m_clickReactionTimer > 0.f) {
        m_idleDuration = 0.f;
        if (m_sleeping) {
            m_sleeping = false;
            updateIconState();
            if (m_sleepZzz) m_sleepZzz->setVisible(false);
        }
    } else {
        m_idleDuration += dt;
    }

    // ── sleep mode ──
    if (m_config.enableSleep && !m_sleeping && !m_walking &&
        m_reactionTimer <= 0.f && m_clickReactionTimer <= 0.f &&
        m_idleDuration >= m_config.sleepAfterSeconds) {
        m_sleeping = true;
        updateIconState();
        createSleepZzz();
    }

    // ── squish on stop ──
    if (m_wasWalking && !m_walking && m_config.squishOnLand) {
        m_landSquishTimer = 0.2f;
    }

    // ── flip based on direction ──
    if (m_config.flipOnDirection && std::abs(m_velocity.x) > 5.f) {
        m_facingRight = m_velocity.x > 0;
    }
    m_petSprite->setFlipX(!m_facingRight);

    // ── icon state switching ──
    if (m_reactionTimer <= 0.f && m_clickReactionTimer <= 0.f) {
        PetIconState desiredState = PetIconState::Default;
        if (m_sleeping) desiredState = PetIconState::Sleep;
        else if (m_walking) desiredState = PetIconState::Walk;
        else desiredState = PetIconState::Idle;

        if (desiredState != m_iconState) {
            m_iconState = desiredState;
            updateIconState();
        }
    }

    // ── tilt based on horizontal velocity ──
    float baseRotation = 0.f;
    if (m_config.rotationDamping > 0.f) {
        float targetTilt = 0.f;
        if (m_walking) {
            targetTilt = std::max(-m_config.maxTilt, std::min(m_config.maxTilt,
                -m_velocity.x * 0.02f * m_config.maxTilt));
        }
        float tiltLerp = 1.f - std::pow(1.f - m_config.rotationDamping, dt * 60.f);
        m_currentTilt += (targetTilt - m_currentTilt) * tiltLerp;
        baseRotation = m_currentTilt;
    }

    // ── reaction spin override ──
    if (m_reactionTimer > 0.f) {
        baseRotation += m_reactionSpinVel * (m_reactionTimer / m_config.reactionDuration);
    }

    m_petSprite->setRotation(baseRotation);

    // ── position with bounce ──
    CCPoint finalPos = m_currentPos;

    // reaction jump
    if (m_reactionTimer > 0.f) {
        m_reactionJumpVel -= 400.f * dt; // gravity
        m_reactionBaseY += m_reactionJumpVel * dt;
        finalPos.y = m_reactionBaseY;
    }
    // click reaction jump
    else if (m_clickReactionTimer > 0.f) {
        m_clickJumpVel -= 400.f * dt;
        m_clickBaseY += m_clickJumpVel * dt;
        finalPos.y = m_clickBaseY;
    }
    // normal bounce
    else if (m_config.bounce && m_config.bounceHeight > 0.f) {
        if (m_walking) {
            m_walkTimer += dt * m_config.bounceSpeed;
            float bounceOffset = std::abs(std::sin(m_walkTimer * 3.14159f)) * m_config.bounceHeight;
            finalPos.y += bounceOffset;
        } else if (m_config.idleAnimation) {
            m_idleTimer += dt * m_config.idleBreathSpeed;
            finalPos.y += std::sin(m_idleTimer * 3.14159f) * m_config.bounceHeight * 0.3f;
        }
    } else if (m_config.idleAnimation && !m_walking) {
        m_idleTimer += dt * m_config.idleBreathSpeed;
        finalPos.y += std::sin(m_idleTimer * 3.14159f) * 2.f;
    }

    // ── squish effect ──
    if (m_landSquishTimer > 0.f) {
        m_landSquishTimer -= dt;
        float t = m_landSquishTimer / 0.2f;
        float squishX = 1.f + m_config.squishAmount * t;
        float squishY = 1.f - m_config.squishAmount * t;
        m_petSprite->setScaleX(m_config.scale * squishX);
        m_petSprite->setScaleY(m_config.scale * squishY);
    } else {
        // idle breathing scale (only when not sleeping)
        if (m_config.idleAnimation && !m_walking && !m_sleeping) {
            float breath = 1.f + std::sin(m_idleTimer * 3.14159f * 2.f) * m_config.idleBreathScale;
            m_petSprite->setScaleX(m_config.scale * breath);
            m_petSprite->setScaleY(m_config.scale * (2.f - breath));
        } else if (m_sleeping) {
            // gentle breathing while asleep
            float breath = 1.f + std::sin(m_idleTimer * 1.5f) * m_config.idleBreathScale * 0.5f;
            m_petSprite->setScaleX(m_config.scale * breath);
            m_petSprite->setScaleY(m_config.scale * (2.f - breath));
        } else {
            m_petSprite->setScale(m_config.scale);
        }
    }

    m_petSprite->setPosition(finalPos);

    // ── trail ──
    if (m_trail) {
        m_trail->setPosition(finalPos);
    }

    // ── shadow ──
    updateShadow();

    // ── particles ──
    updateParticles(dt);

    // ── speech bubble ──
    updateSpeechBubble(dt);

    // ── sleep Zzz ──
    updateSleepZzz(dt);

    // ── reaction timer ──
    updateReaction(dt);

    // ── click reaction timer ──
    updateClickReaction(dt);

    // ── random idle speech ──
    if (m_config.enableSpeech && !m_sleeping && m_speechTimer <= 0.f &&
        m_reactionTimer <= 0.f && m_clickReactionTimer <= 0.f) {
        m_speechIdleAccum += dt;
        if (m_speechIdleAccum >= m_config.speechInterval && !m_config.idleMessages.empty()) {
            m_speechIdleAccum = 0.f;
            showRandomSpeech(m_config.idleMessages);
        }
    }
}

void PetManager::applyConfigLive() {
    if (!m_petSprite) return;

    m_petSprite->setScale(m_config.scale);
    m_petSprite->setOpacity(static_cast<GLubyte>(m_config.opacity));

    updateTrail();
    saveConfig();
}

void PetManager::updateTrail() {
    // remove old trail
    if (m_trail && m_petNode) {
        m_trail->removeFromParent();
        m_trail = nullptr;
    }

    if (!m_config.showTrail || !m_petNode || !m_petSprite) return;

    // create a small white texture programmatically (2x2 RGBA white)
    // this avoids depending on texture files that may not exist in GD
    auto& trailTex = whiteTrailTexture();
    if (!trailTex) {
        const int sz = 2;
        uint8_t pixels[sz * sz * 4];
        memset(pixels, 255, sizeof(pixels)); // all white, full alpha
        auto* newTex = new CCTexture2D();
        if (newTex->initWithData(pixels, kCCTexture2DPixelFormat_RGBA8888, sz, sz, CCSizeMake(sz, sz))) {
            trailTex = newTex;
        } else {
            newTex->release();
        }
    }

    if (!trailTex) return;

    m_trail = CCMotionStreak::create(
        m_config.trailLength / 60.f,  // fade time
        1.f,                           // min seg
        m_config.trailWidth,           // stroke width
        ccc3(255, 255, 255),           // color
        trailTex.data()                // safe texture ptr
    );

    if (m_trail && m_trail->getTexture()) {
        m_trail->setOpacity(static_cast<GLubyte>(m_config.opacity * 0.4f));
        ccBlendFunc blend = {GL_SRC_ALPHA, GL_ONE};
        m_trail->setBlendFunc(blend);
        m_petNode->addChild(m_trail, 5);
    } else {
        // trail was created but has no valid texture — don't add to scene
        m_trail = nullptr;
        log::warn("[PetManager] Failed to create trail with valid texture");
    }
}

// ════════════════════════════════════════════════════════════
// icon state management
// ════════════════════════════════════════════════════════════

std::string PetManager::getIconStateImage(PetIconState state) const {
    switch (state) {
        case PetIconState::Idle:   return m_config.idleImage;
        case PetIconState::Walk:   return m_config.walkImage;
        case PetIconState::Sleep:  return m_config.sleepImage;
        case PetIconState::React:  return m_config.reactImage;
        default:                   return m_config.selectedImage;
    }
}

void PetManager::setIconStateImage(PetIconState state, std::string const& galleryFilename) {
    switch (state) {
        case PetIconState::Idle:   m_config.idleImage  = galleryFilename; break;
        case PetIconState::Walk:   m_config.walkImage  = galleryFilename; break;
        case PetIconState::Sleep:  m_config.sleepImage = galleryFilename; break;
        case PetIconState::React:  m_config.reactImage = galleryFilename; break;
        default: break;
    }
    saveConfig();
    // refresh if currently in this state
    if (m_iconState == state) updateIconState();
}

void PetManager::switchToIconState(PetIconState state) {
    m_iconState = state;
    updateIconState();
}

void PetManager::updateIconState() {
    if (!m_petSprite || !m_petNode) return;

    std::string imageFile = getIconStateImage(m_iconState);
    if (imageFile.empty()) imageFile = m_config.selectedImage;
    if (imageFile.empty()) return;

    auto path = galleryDir() / imageFile;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        // fallback to default selectedImage if state image missing
        if (m_iconState != PetIconState::Default && !m_config.selectedImage.empty()) {
            path = galleryDir() / m_config.selectedImage;
            if (!std::filesystem::exists(path, ec) || ec) return;
        } else return;
    }

    // remove old sprite
    if (m_petSprite) {
        m_petSprite->removeFromParent();
        m_petSprite = nullptr;
    }

    m_petSprite = ImageLoadHelper::loadAnimatedOrStatic(path, 10,
        [](std::string const& p) -> CCSprite* {
            return AnimatedGIFSprite::create(p);
        });

    if (m_petSprite) {
        m_petSprite->setScale(m_config.scale);
        m_petSprite->setOpacity(static_cast<GLubyte>(m_config.opacity));
        m_petSprite->setFlipX(!m_facingRight);
        m_petNode->addChild(m_petSprite, 10);
    }
}

// ════════════════════════════════════════════════════════════
// shadow
// ════════════════════════════════════════════════════════════

void PetManager::createShadow() {
    if (!m_petNode || !m_petSprite) return;

    // shadow is a dark ellipse beneath the pet
    m_shadowSprite = CCSprite::create();
    if (!m_shadowSprite) return;

    // create a small dark texture for the shadow
    const int sz = 16;
    auto* pixels = new unsigned char[sz * sz * 4];
    memset(pixels, 0, sz * sz * 4);
    // draw a soft circle
    for (int y = 0; y < sz; y++) {
        for (int x = 0; x < sz; x++) {
            float dx = (x - sz / 2.f) / (sz / 2.f);
            float dy = (y - sz / 2.f) / (sz / 2.f);
            float dist = dx * dx + dy * dy;
            if (dist <= 1.f) {
                float alpha = (1.f - dist) * (m_config.shadowOpacity / 255.f);
                auto idx = (y * sz + x) * 4;
                pixels[idx] = 0;
                pixels[idx + 1] = 0;
                pixels[idx + 2] = 0;
                pixels[idx + 3] = static_cast<unsigned char>(alpha * 255);
            }
        }
    }

    auto* tex = new CCTexture2D();
    if (tex->initWithData(pixels, kCCTexture2DPixelFormat_RGBA8888, sz, sz, CCSizeMake(sz, sz))) {
        m_shadowSprite = CCSprite::createWithTexture(tex);
        tex->release();
    } else {
        tex->release();
    }
    delete[] pixels;

    if (m_shadowSprite) {
        m_shadowSprite->setScale(m_config.scale * m_config.shadowScale);
        m_shadowSprite->setOpacity(static_cast<GLubyte>(m_config.shadowOpacity));
        m_shadowSprite->setVisible(m_config.showShadow);
        m_petNode->addChild(m_shadowSprite, 1);
    }
}

void PetManager::updateShadow() {
    if (!m_shadowSprite || !m_petSprite) return;

    m_shadowSprite->setVisible(m_config.showShadow);
    if (!m_config.showShadow) return;

    auto petPos = m_petSprite->getPosition();
    m_shadowSprite->setPosition({
        petPos.x + m_config.shadowOffsetX,
        petPos.y + m_config.shadowOffsetY
    });
    m_shadowSprite->setScale(m_config.scale * m_config.shadowScale);
    m_shadowSprite->setOpacity(static_cast<GLubyte>(m_config.shadowOpacity));
}

// ════════════════════════════════════════════════════════════
// particles
// ════════════════════════════════════════════════════════════

void PetManager::createParticleNode() {
    if (!m_petNode) return;

    m_particleNode = CCNode::create();
    if (m_particleNode) {
        m_particleNode->setID("pet-particles"_spr);
        m_particleNode->setVisible(m_config.showParticles);
        m_petNode->addChild(m_particleNode, 8);
    }
}

void PetManager::updateParticles(float dt) {
    if (!m_particleNode) return;

    m_particleNode->setVisible(m_config.showParticles);
    if (!m_config.showParticles) {
        m_particleAccum = 0.f;
        return;
    }

    m_particleAccum += dt;
    float interval = 1.f / std::max(m_config.particleRate, 0.1f);

    while (m_particleAccum >= interval) {
        m_particleAccum -= interval;
        emitParticle();
    }

    // update existing particles (fade + move + remove)
    if (m_particleNode->getChildren()) {
        auto toRemove = std::vector<CCNode*>();
        for (auto* child : CCArrayExt<CCNode*>(m_particleNode->getChildren())) {
            if (!child) continue;

            // move by stored velocity
            auto vx = static_cast<CCString*>(child->getUserObject());
            if (vx) {
                // parse "vx|vy|life" from user object
                std::string data = vx->getCString();
                auto p1 = data.find('|');
                auto p2 = data.find('|', p1 + 1);
                if (p1 != std::string::npos && p2 != std::string::npos) {
                    float velX = geode::utils::numFromString<float>(data.substr(0, p1)).unwrapOr(0.f);
                    float velY = geode::utils::numFromString<float>(data.substr(p1 + 1, p2 - p1 - 1)).unwrapOr(0.f);
                    float life = geode::utils::numFromString<float>(data.substr(p2 + 1)).unwrapOr(0.f);
                    life -= dt;
                    if (life <= 0.f) {
                        toRemove.push_back(child);
                    } else {
                        auto pos = child->getPosition();
                        velY += m_config.particleGravity * dt;
                        child->setPosition({pos.x + velX * dt, pos.y + velY * dt});

                        // fade out
                        float fadeRatio = life / m_config.particleLifetime;
                        if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(child)) {
                            rgba->setOpacity(static_cast<GLubyte>(255 * fadeRatio));
                        }
                        child->setScale(m_config.particleSize / 10.f * fadeRatio);

                        // update stored life
                        auto* newData = CCString::createWithFormat("%.2f|%.2f|%.3f", velX, velY, life);
                        child->setUserObject(newData);
                    }
                }
            }
        }
        for (auto* n : toRemove) n->removeFromParent();
    }
}

void PetManager::emitParticle() {
    if (!m_particleNode || !m_petSprite) return;

    auto petPos = m_petSprite->getPosition();

    // random offset around pet center
    float rx = (rand() % 200 - 100) / 100.f * 15.f;
    float ry = (rand() % 200 - 100) / 100.f * 10.f;

    // random velocity
    float vx = (rand() % 200 - 100) / 100.f * 20.f;
    float vy = (rand() % 100) / 100.f * 30.f + 10.f;

    // create particle based on type
    auto type = static_cast<PetParticleType>(m_config.particleType);
    CCSprite* particle = nullptr;

    switch (type) {
        case PetParticleType::Hearts: {
            // draw a small heart using CCDrawNode
            auto draw = CCDrawNode::create();
            if (draw) {
                float s = m_config.particleSize;
                CCPoint pts[] = {
                    ccp(-s * 0.5f, 0), ccp(0, -s * 0.7f), ccp(s * 0.5f, 0),
                    ccp(s * 0.25f, s * 0.3f), ccp(0, s * 0.15f), ccp(-s * 0.25f, s * 0.3f)
                };
                ccColor4F col = {
                    m_config.particleColor.r / 255.f,
                    m_config.particleColor.g / 255.f,
                    m_config.particleColor.b / 255.f, 1.f
                };
                draw->drawPolygon(pts, 6, col, 0, col);
                draw->setContentSize(CCSizeMake(s, s));
                draw->setAnchorPoint({0.5f, 0.5f});
                particle = CCSprite::create(); // wrapper
                if (particle) {
                    particle->setContentSize(CCSizeMake(s, s));
                    // just use the draw node directly
                    particle->removeFromParent();
                    draw->setPosition({petPos.x + rx, petPos.y + ry});
                    auto* data = CCString::createWithFormat("%.2f|%.2f|%.3f", vx, vy, m_config.particleLifetime);
                    draw->setUserObject(data);
                    m_particleNode->addChild(draw);
                    return;
                }
            }
            break;
        }
        case PetParticleType::Stars: {
            auto draw = CCDrawNode::create();
            if (draw) {
                float s = m_config.particleSize;
                // 5-point star
                std::vector<CCPoint> starPts;
                for (int i = 0; i < 10; i++) {
                    float angle = (i * 36.f) * 3.14159f / 180.f - 90.f * 3.14159f / 180.f;
                    float r = (i % 2 == 0) ? s : s * 0.4f;
                    starPts.push_back(ccp(cosf(angle) * r, sinf(angle) * r));
                }
                ccColor4F col = {
                    m_config.particleColor.r / 255.f,
                    m_config.particleColor.g / 255.f,
                    m_config.particleColor.b / 255.f, 1.f
                };
                draw->drawPolygon(starPts.data(), static_cast<unsigned int>(starPts.size()), col, 0, col);
                draw->setContentSize(CCSizeMake(s * 2, s * 2));
                draw->setAnchorPoint({0.5f, 0.5f});
                draw->setPosition({petPos.x + rx, petPos.y + ry});
                auto* data = CCString::createWithFormat("%.2f|%.2f|%.3f", vx, vy, m_config.particleLifetime);
                draw->setUserObject(data);
                m_particleNode->addChild(draw);
                return;
            }
            break;
        }
        case PetParticleType::Snowflakes:
        case PetParticleType::Bubbles:
        case PetParticleType::Sparkles:
        default: {
            // simple circle particle using CCDrawNode
            auto draw = CCDrawNode::create();
            if (draw) {
                float s = m_config.particleSize;
                ccColor4F col = {
                    m_config.particleColor.r / 255.f,
                    m_config.particleColor.g / 255.f,
                    m_config.particleColor.b / 255.f, 1.f
                };
                draw->drawDot(ccp(0, 0), s, col);
                draw->setContentSize(CCSizeMake(s * 2, s * 2));
                draw->setAnchorPoint({0.5f, 0.5f});
                draw->setPosition({petPos.x + rx, petPos.y + ry});
                auto* data = CCString::createWithFormat("%.2f|%.2f|%.3f", vx, vy, m_config.particleLifetime);
                draw->setUserObject(data);
                m_particleNode->addChild(draw);
                return;
            }
            break;
        }
    }
}

// ════════════════════════════════════════════════════════════
// speech bubbles
// ════════════════════════════════════════════════════════════

void PetManager::createSpeechBubbleNode() {
    if (!m_petNode) return;

    m_speechNode = CCNode::create();
    if (!m_speechNode) return;
    m_speechNode->setID("pet-speech"_spr);
    m_speechNode->setVisible(false);
    m_petNode->addChild(m_speechNode, 20);

    // background bubble
    m_speechBg = CCDrawNode::create();
    if (m_speechBg) {
        m_speechBg->setVisible(false);
        m_speechNode->addChild(m_speechBg);
    }

    // text label
    m_speechLabel = CCLabelBMFont::create("", "chatFont.fnt");
    if (m_speechLabel) {
        m_speechLabel->setScale(m_config.speechBubbleScale);
        m_speechLabel->setVisible(false);
        m_speechNode->addChild(m_speechLabel);
    }
}

void PetManager::showSpeechBubble(std::string const& message) {
    if (!m_speechNode || !m_speechLabel || !m_speechBg || !m_petSprite) return;
    if (!m_config.enableSpeech) return;

    m_speechLabel->setString(message.c_str());
    m_speechLabel->setVisible(true);

    // position above pet
    auto petPos = m_petSprite->getPosition();
    float labelWidth = m_speechLabel->getContentSize().width * m_config.speechBubbleScale;
    float labelHeight = m_speechLabel->getContentSize().height * m_config.speechBubbleScale;

    float bubbleW = labelWidth + 16.f;
    float bubbleH = labelHeight + 10.f;
    float bubbleY = petPos.y + m_petSprite->getContentSize().height * m_config.scale * 0.5f + bubbleH + 5.f;

    m_speechLabel->setPosition({petPos.x, bubbleY});
    m_speechLabel->setAnchorPoint({0.5f, 0.5f});

    // draw bubble background
    if (m_speechBg) {
        m_speechBg->clear();
        ccColor4F bgCol = {0.f, 0.f, 0.f, 0.7f};
        ccColor4F borderCol = {1.f, 1.f, 1.f, 0.5f};

        // rounded rect
        float w = bubbleW, h = bubbleH;
        float radius = 4.f;
        CCPoint rect[4] = {
            ccp(-w/2.f, -h/2.f), ccp(w/2.f, -h/2.f),
            ccp(w/2.f, h/2.f), ccp(-w/2.f, h/2.f)
        };
        m_speechBg->drawPolygon(rect, 4, bgCol, 1.f, borderCol);

        // small triangle pointing down
        CCPoint tri[3] = {
            ccp(-4.f, -h/2.f), ccp(4.f, -h/2.f), ccp(0.f, -h/2.f - 5.f)
        };
        m_speechBg->drawPolygon(tri, 3, bgCol, 1.f, borderCol);

        m_speechBg->setPosition({petPos.x, bubbleY});
        m_speechBg->setVisible(true);
    }

    m_speechNode->setVisible(true);
    m_speechTimer = m_config.speechDuration;

    // pop-in animation
    m_speechNode->setScale(0.f);
    m_speechNode->stopAllActions();
    m_speechNode->runAction(CCEaseBackOut::create(CCScaleTo::create(0.2f, 1.f)));
}

void PetManager::showRandomSpeech(std::vector<std::string> const& messages) {
    if (messages.empty()) return;
    int idx = rand() % static_cast<int>(messages.size());
    showSpeechBubble(messages[idx]);
}

void PetManager::hideSpeechBubble() {
    if (!m_speechNode) return;

    m_speechTimer = 0.f;

    // pop-out animation
    m_speechNode->stopAllActions();
    m_speechNode->runAction(CCSequence::create(
        CCEaseBackIn::create(CCScaleTo::create(0.15f, 0.f)),
        CCHide::create(),
        nullptr
    ));
}

void PetManager::updateSpeechBubble(float dt) {
    if (!m_speechNode || m_speechTimer <= 0.f) return;

    m_speechTimer -= dt;

    // follow pet position
    if (m_petSprite && m_speechLabel && m_speechBg) {
        auto petPos = m_petSprite->getPosition();
        float labelHeight = m_speechLabel->getContentSize().height * m_config.speechBubbleScale;
        float bubbleH = labelHeight + 10.f;
        float bubbleY = petPos.y + m_petSprite->getContentSize().height * m_config.scale * 0.5f + bubbleH + 5.f;

        m_speechLabel->setPosition({petPos.x, bubbleY});
        m_speechBg->setPosition({petPos.x, bubbleY});
    }

    if (m_speechTimer <= 0.f) {
        hideSpeechBubble();
    }
}

// ════════════════════════════════════════════════════════════
// sleep Zzz
// ════════════════════════════════════════════════════════════

void PetManager::createSleepZzz() {
    if (!m_petNode || m_sleepZzz) return;

    m_sleepZzz = CCNode::create();
    if (!m_sleepZzz) return;
    m_sleepZzz->setID("pet-sleep-zzz"_spr);

    // create "Zzz" text
    auto zLabel = CCLabelBMFont::create("Zzz", "goldFont.fnt");
    if (zLabel) {
        zLabel->setScale(0.3f);
        zLabel->setColor({100, 150, 255});
        zLabel->setAnchorPoint({0.5f, 0.5f});
        zLabel->setPosition({0, 0});
        m_sleepZzz->addChild(zLabel);
    }

    m_sleepZzz->setVisible(m_sleeping);
    m_petNode->addChild(m_sleepZzz, 15);
    m_sleepZzzTimer = 0.f;
}

void PetManager::updateSleepZzz(float dt) {
    if (!m_sleepZzz) return;

    m_sleepZzz->setVisible(m_sleeping);
    if (!m_sleeping || !m_petSprite) return;

    // bob above pet
    m_sleepZzzTimer += dt;
    auto petPos = m_petSprite->getPosition();
    float baseY = petPos.y + m_petSprite->getContentSize().height * m_config.scale * 0.5f + 15.f;
    float bobY = std::sin(m_sleepZzzTimer * 2.f) * m_config.sleepBobAmount;
    m_sleepZzz->setPosition({petPos.x + 10.f, baseY + bobY});

    // gentle fade in/out
    float alpha = 128 + 60 * std::sin(m_sleepZzzTimer * 1.5f);
    // set opacity on the child label (CCNode doesn't have setOpacity)
    if (auto* label = typeinfo_cast<CCRGBAProtocol*>(m_sleepZzz->getChildren()->objectAtIndex(0))) {
        label->setOpacity(static_cast<GLubyte>(alpha));
    }
}

// ════════════════════════════════════════════════════════════
// game event reactions
// ════════════════════════════════════════════════════════════

void PetManager::triggerReaction(std::string const& eventType) {
    if (!m_petSprite || !m_config.enabled) return;

    // check if this event type is enabled
    if (eventType == "level_complete" && !m_config.reactToLevelComplete) return;
    if (eventType == "death" && !m_config.reactToDeath) return;
    if (eventType == "practice_exit" && !m_config.reactToPracticeExit) return;

    // wake up if sleeping
    if (m_sleeping) {
        m_sleeping = false;
        m_idleDuration = 0.f;
        if (m_sleepZzz) m_sleepZzz->setVisible(false);
    }

    // switch to react icon
    m_iconState = PetIconState::React;
    updateIconState();

    // start reaction jump + spin
    m_reactionTimer = m_config.reactionDuration;
    m_reactionJumpVel = m_config.reactionJumpHeight * 3.f; // initial upward velocity
    m_reactionBaseY = m_currentPos.y;
    m_reactionSpinVel = m_config.reactionSpinSpeed;

    // show speech bubble for the event
    if (m_config.enableSpeech) {
        if (eventType == "level_complete" && !m_config.levelCompleteMessages.empty()) {
            showRandomSpeech(m_config.levelCompleteMessages);
        } else if (eventType == "death" && !m_config.deathMessages.empty()) {
            showRandomSpeech(m_config.deathMessages);
        }
    }
}

void PetManager::updateReaction(float dt) {
    if (m_reactionTimer <= 0.f) return;

    m_reactionTimer -= dt;
    if (m_reactionTimer <= 0.f) {
        m_reactionTimer = 0.f;
        // restore normal icon state
        updateIconState();
    }
}

// ════════════════════════════════════════════════════════════
// click interaction
// ════════════════════════════════════════════════════════════

void PetManager::triggerClickReaction(cocos2d::CCPoint clickPos) {
    if (!m_petSprite || !m_config.enabled || !m_config.enableClickInteraction) return;
    if (m_clickReactionTimer > 0.f) return; // already reacting to click

    // check if click is near the pet
    auto petPos = m_petSprite->getPosition();
    float dist = std::sqrt((clickPos.x - petPos.x) * (clickPos.x - petPos.x) +
                           (clickPos.y - petPos.y) * (clickPos.y - petPos.y));
    float hitRadius = m_petSprite->getContentSize().width * m_config.scale * 0.6f;
    if (dist > hitRadius) return;

    // wake up if sleeping
    if (m_sleeping) {
        m_sleeping = false;
        m_idleDuration = 0.f;
        if (m_sleepZzz) m_sleepZzz->setVisible(false);
    }

    // start click jump
    m_clickReactionTimer = m_config.clickReactionDuration;
    m_clickJumpVel = m_config.clickJumpHeight * 3.f;
    m_clickBaseY = m_currentPos.y;

    // show click speech bubble
    if (m_config.enableSpeech && !m_config.clickMessages.empty()) {
        showRandomSpeech(m_config.clickMessages);
    }
}

void PetManager::updateClickReaction(float dt) {
    if (m_clickReactionTimer <= 0.f) return;

    m_clickReactionTimer -= dt;
    if (m_clickReactionTimer <= 0.f) {
        m_clickReactionTimer = 0.f;
    }
}

// ════════════════════════════════════════════════════════════
// unused stubs (animations managed in update)
// ════════════════════════════════════════════════════════════

void PetManager::updateIdleAnimation(float dt) {}
void PetManager::updateWalkAnimation(float dt) {}








