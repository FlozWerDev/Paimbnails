#include "../../../utils/FilesystemPath.hpp"
#include "PetManager.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/ImageLoadHelper.hpp"
#include "../../../utils/AnimatedGIFSprite.hpp"
#include "../../../utils/LocalAssetStore.hpp"
#include "../../../utils/EditorContext.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../audio/services/PaimonAudio.hpp"
#include "../../capture/services/FramebufferCapture.hpp"
#include "../../cursor/services/CursorManager.hpp"
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/file.hpp>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <random>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/string.hpp>
#include <string_view>
#include <unordered_set>

using namespace geode::prelude;
using namespace cocos2d;

namespace {
geode::Ref<CCTexture2D>& whiteTrailTexture() {
    static geode::Ref<CCTexture2D> s_whiteTrailTex = nullptr;
    return s_whiteTrailTex;
}

constexpr int kPetHostZOrder = std::numeric_limits<int>::max() - 4096;

void ensurePetNodeIsFrontmost(CCScene* scene, CCNode* petNode) {
    if (!scene || !petNode || petNode->getParent() != scene) return;
    if (petNode->getZOrder() != kPetHostZOrder) {
        scene->reorderChild(petNode, kPetHostZOrder);
    }
}

bool layerNameMatchesNode(CCNode* node, std::string const& layerName) {
    if (!node) return false;
    std::string nodeID(node->getID());
    if (!nodeID.empty() && 
        nodeID.find(layerName) != std::string::npos) return true;
    auto className = geode::cocos::getObjectName(node);
    if (className == layerName) return true;
    return className.find(layerName) != std::string::npos;
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
    for (auto* child : CCArrayExt<CCNode*>(scene->getChildren())) {
        if (!child) continue;
        for (auto const& layerName : layerOptions) {
            if (layerNameMatchesNode(child, layerName)) return true;
        }
        if (auto* grandChildren = child->getChildren()) {
            for (auto* gc : CCArrayExt<CCNode*>(grandChildren)) {
                if (!gc) continue;
                for (auto const& layerName : layerOptions) {
                    if (layerNameMatchesNode(gc, layerName)) return true;
                }
            }
        }
    }
    return false;
}
}


PetManager& PetManager::get() {
    static PetManager inst;
    return inst;
}

PetManager::~PetManager() {
    stopActiveAudio();
    detachFromScene();
    (void)whiteTrailTexture().take();
    for (auto& [_, tex] : m_staticTextureCache) {
        (void)tex.take();
    }
    m_staticTextureCache.clear();
}


std::filesystem::path PetManager::configPath() const {
    return paimon::paths::toStdPath(Mod::get()->getSaveDir()) / "pet_config.json";
}

std::filesystem::path PetManager::galleryDir() const {
    auto dir = paimon::paths::toStdPath(Mod::get()->getSaveDir()) / "pet_gallery";
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) {
        std::filesystem::create_directories(dir, ec);
    }
    return dir;
}

std::filesystem::path PetManager::audioDir() const {
    auto dir = paimon::paths::toStdPath(Mod::get()->getSaveDir()) / "pet_audio";
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) {
        std::filesystem::create_directories(dir, ec);
    }
    return dir;
}


void PetManager::loadConfig() {
    log::debug("[PetManager] loadConfig");
    auto path = configPath();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return;

    auto rawRes = geode::utils::file::readString(paimon::paths::toSdkPath(path));
    if (!rawRes) {
        log::error("[PetManager] Failed to open config file");
        return;
    }
    auto res = matjson::parse(rawRes.unwrap());
    if (res.isErr()) return;
    loadConfig(res.unwrap());
}

void PetManager::loadConfig(matjson::Value const& j) {
    if (!j.isObject()) return;
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

        m_config.idleImage  = j["idleImage"].asString().unwrapOr("");
        m_config.walkImage  = j["walkImage"].asString().unwrapOr("");
        m_config.sleepImage = j["sleepImage"].asString().unwrapOr("");
        m_config.reactImage = j["reactImage"].asString().unwrapOr("");

        m_config.showShadow    = j["showShadow"].asBool().unwrapOr(true);
        m_config.shadowOffsetX = static_cast<float>(j["shadowOffsetX"].asDouble().unwrapOr(3.0));
        m_config.shadowOffsetY = static_cast<float>(j["shadowOffsetY"].asDouble().unwrapOr(-5.0));
        m_config.shadowOpacity = static_cast<int>(j["shadowOpacity"].asInt().unwrapOr(60));
        m_config.shadowScale   = static_cast<float>(j["shadowScale"].asDouble().unwrapOr(1.1));

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

        m_config.enableSpeech      = j["enableSpeech"].asBool().unwrapOr(false);
        m_config.speechInterval    = static_cast<float>(j["speechInterval"].asDouble().unwrapOr(30.0));
        m_config.speechDuration    = static_cast<float>(j["speechDuration"].asDouble().unwrapOr(3.0));
        m_config.speechBubbleScale = static_cast<float>(j["speechBubbleScale"].asDouble().unwrapOr(0.5));
        m_config.speechTypewriter  = j["speechTypewriter"].asBool().unwrapOr(true);

        auto loadColor = [&](char const* key, cocos2d::ccColor3B& target) {
            if (j.contains(key) && j[key].isObject()) {
                auto const& c = j[key];
                target = cocos2d::ccc3(
                    static_cast<GLubyte>(std::clamp<int>(static_cast<int>(c["r"].asInt().unwrapOr(target.r)), 0, 255)),
                    static_cast<GLubyte>(std::clamp<int>(static_cast<int>(c["g"].asInt().unwrapOr(target.g)), 0, 255)),
                    static_cast<GLubyte>(std::clamp<int>(static_cast<int>(c["b"].asInt().unwrapOr(target.b)), 0, 255))
                );
            }
        };
        loadColor("speechBgColor", m_config.speechBgColor);
        loadColor("speechBorderColor", m_config.speechBorderColor);
        loadColor("speechTextColor", m_config.speechTextColor);

        auto loadDialogueArr = [&](char const* key, std::vector<PetDialogueEntry>& target) {
            auto arr = j[key].asArray();
            if (arr.isOk()) {
                target.clear();
                for (auto const& v : arr.unwrap()) {
                    if (v.isString()) {
                        auto s = v.asString().unwrapOr("");
                        if (!s.empty()) target.push_back({s, 10.f, ""});
                    } else if (v.isObject()) {
                        auto text = v["text"].asString().unwrapOr("");
                        if (text.empty()) continue;
                        float weight = static_cast<float>(v["weight"].asDouble().unwrapOr(10.0));
                        weight = std::clamp(weight, 0.f, 100.f);
                        auto audio = v["audio"].asString().unwrapOr("");
                        target.push_back({text, weight, audio});
                    }
                }
            }
        };
        loadDialogueArr("idleMessages", m_config.idleMessages);
        loadDialogueArr("levelCompleteMessages", m_config.levelCompleteMessages);
        loadDialogueArr("deathMessages", m_config.deathMessages);
        loadDialogueArr("clickMessages", m_config.clickMessages);
        loadDialogueArr("pettingMessages", m_config.pettingMessages);
        loadDialogueArr("gestureMessages", m_config.gestureMessages);

        auto loadStrPool = [&](matjson::Value const& val, std::vector<std::string>& target) {
            auto arr = val.asArray();
            if (arr.isOk()) {
                target.clear();
                for (auto const& v : arr.unwrap()) {
                    auto s = v.asString().unwrapOr("");
                    if (!s.empty()) target.push_back(s);
                }
            }
        };

        m_config.enableAudio      = j["enableAudio"].asBool().unwrapOr(true);
        m_config.audioVolume      = std::clamp(static_cast<float>(j["audioVolume"].asDouble().unwrapOr(0.8)), 0.f, 1.f);
        m_config.audioPitchRandom = std::clamp(static_cast<float>(j["audioPitchRandom"].asDouble().unwrapOr(0.08)), 0.f, 0.35f);
        if (j.contains("clickAudioPool")) {
            loadStrPool(j["clickAudioPool"], m_config.clickAudioPool);
        }
        if (j.contains("gestureAudioPools") && j["gestureAudioPools"].isObject()) {
            auto const& pools = j["gestureAudioPools"];
            for (int gi = 0; gi < static_cast<int>(PetGesture::Count); ++gi) {
                auto g = static_cast<PetGesture>(gi);
                char const* gKey = petGestureKey(g);
                if (pools.contains(gKey)) {
                    loadStrPool(pools[gKey], m_config.audioPoolForGesture(g));
                }
            }
        }

        m_config.enableSleep       = j["enableSleep"].asBool().unwrapOr(true);
        m_config.sleepAfterSeconds = static_cast<float>(j["sleepAfterSeconds"].asDouble().unwrapOr(60.0));
        m_config.sleepBobAmount    = static_cast<float>(j["sleepBobAmount"].asDouble().unwrapOr(3.0));

        m_config.enableClickInteraction = j["enableClickInteraction"].asBool().unwrapOr(true);
        m_config.clickReactionDuration  = static_cast<float>(j["clickReactionDuration"].asDouble().unwrapOr(1.5));
        m_config.clickJumpHeight        = static_cast<float>(j["clickJumpHeight"].asDouble().unwrapOr(20.0));

        m_config.enablePetting       = j["enablePetting"].asBool().unwrapOr(true);
        m_config.enableAutoGestures  = j["enableAutoGestures"].asBool().unwrapOr(true);
        m_config.autoGestureInterval = std::clamp(static_cast<float>(j["autoGestureInterval"].asDouble().unwrapOr(45.0)), 8.f, 300.f);
        m_config.reactToMusic        = j["reactToMusic"].asBool().unwrapOr(false);
        m_config.musicBounceStrength = std::clamp(static_cast<float>(j["musicBounceStrength"].asDouble().unwrapOr(0.14)), 0.f, 0.4f);
        m_config.wanderMode          = j["wanderMode"].asBool().unwrapOr(false);
        m_config.affectionXp         = std::max(0, static_cast<int>(j["affectionXp"].asInt().unwrapOr(0)));
        m_config.totalClicks         = std::max(0, static_cast<int>(j["totalClicks"].asInt().unwrapOr(0)));
        m_config.totalPets           = std::max(0, static_cast<int>(j["totalPets"].asInt().unwrapOr(0)));

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

        matjson::Value layers = matjson::Value::array();
        for (auto& l : m_config.visibleLayers) {
            layers.push(l);
        }
        j["visibleLayers"] = layers;

        j["idleImage"]  = m_config.idleImage;
        j["walkImage"]  = m_config.walkImage;
        j["sleepImage"] = m_config.sleepImage;
        j["reactImage"] = m_config.reactImage;

        j["showShadow"]    = m_config.showShadow;
        j["shadowOffsetX"] = static_cast<double>(m_config.shadowOffsetX);
        j["shadowOffsetY"] = static_cast<double>(m_config.shadowOffsetY);
        j["shadowOpacity"] = m_config.shadowOpacity;
        j["shadowScale"]   = static_cast<double>(m_config.shadowScale);

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

        j["enableSpeech"]      = m_config.enableSpeech;
        j["speechInterval"]    = static_cast<double>(m_config.speechInterval);
        j["speechDuration"]    = static_cast<double>(m_config.speechDuration);
        j["speechBubbleScale"] = static_cast<double>(m_config.speechBubbleScale);
        j["speechTypewriter"]  = m_config.speechTypewriter;
        auto saveColor = [&](char const* key, cocos2d::ccColor3B col) {
            matjson::Value c = matjson::Value();
            c["r"] = static_cast<int>(col.r);
            c["g"] = static_cast<int>(col.g);
            c["b"] = static_cast<int>(col.b);
            j[key] = c;
        };
        saveColor("speechBgColor", m_config.speechBgColor);
        saveColor("speechBorderColor", m_config.speechBorderColor);
        saveColor("speechTextColor", m_config.speechTextColor);

        auto saveDialogueArr = [&](char const* key, std::vector<PetDialogueEntry> const& list) {
            matjson::Value arr = matjson::Value::array();
            for (auto const& e : list) {
                if (e.text.empty()) continue;
                matjson::Value obj = matjson::Value();
                obj["text"] = e.text;
                obj["weight"] = static_cast<double>(e.weight);
                if (!e.audioFile.empty()) obj["audio"] = e.audioFile;
                arr.push(obj);
            }
            j[key] = arr;
        };
        saveDialogueArr("idleMessages", m_config.idleMessages);
        saveDialogueArr("levelCompleteMessages", m_config.levelCompleteMessages);
        saveDialogueArr("deathMessages", m_config.deathMessages);
        saveDialogueArr("clickMessages", m_config.clickMessages);
        saveDialogueArr("pettingMessages", m_config.pettingMessages);
        saveDialogueArr("gestureMessages", m_config.gestureMessages);

        j["enableAudio"]      = m_config.enableAudio;
        j["audioVolume"]      = static_cast<double>(m_config.audioVolume);
        j["audioPitchRandom"] = static_cast<double>(m_config.audioPitchRandom);
        {
            matjson::Value arr = matjson::Value::array();
            for (auto const& s : m_config.clickAudioPool) {
                if (!s.empty()) arr.push(s);
            }
            j["clickAudioPool"] = arr;
        }
        {
            matjson::Value pools = matjson::Value();
            for (int gi = 0; gi < static_cast<int>(PetGesture::Count); ++gi) {
                auto g = static_cast<PetGesture>(gi);
                matjson::Value arr = matjson::Value::array();
                for (auto const& s : m_config.audioPoolForGesture(g)) {
                    if (!s.empty()) arr.push(s);
                }
                pools[petGestureKey(g)] = arr;
            }
            j["gestureAudioPools"] = pools;
        }

        j["enableSleep"]       = m_config.enableSleep;
        j["sleepAfterSeconds"] = static_cast<double>(m_config.sleepAfterSeconds);
        j["sleepBobAmount"]    = static_cast<double>(m_config.sleepBobAmount);

        j["enableClickInteraction"] = m_config.enableClickInteraction;
        j["clickReactionDuration"]  = static_cast<double>(m_config.clickReactionDuration);
        j["clickJumpHeight"]        = static_cast<double>(m_config.clickJumpHeight);

        j["enablePetting"]       = m_config.enablePetting;
        j["enableAutoGestures"]  = m_config.enableAutoGestures;
        j["autoGestureInterval"] = static_cast<double>(m_config.autoGestureInterval);
        j["reactToMusic"]        = m_config.reactToMusic;
        j["musicBounceStrength"] = static_cast<double>(m_config.musicBounceStrength);
        j["wanderMode"]          = m_config.wanderMode;
        j["affectionXp"]         = m_config.affectionXp;
        j["totalClicks"]         = m_config.totalClicks;
        j["totalPets"]           = m_config.totalPets;

        j["reactToLevelComplete"] = m_config.reactToLevelComplete;
        j["reactToDeath"]         = m_config.reactToDeath;
        j["reactToPracticeExit"]  = m_config.reactToPracticeExit;
        j["reactionDuration"]     = static_cast<double>(m_config.reactionDuration);
        j["reactionJumpHeight"]   = static_cast<double>(m_config.reactionJumpHeight);
        j["reactionSpinSpeed"]    = static_cast<double>(m_config.reactionSpinSpeed);

        auto str = j.dump();
        auto writeRes = geode::utils::file::writeString(paimon::paths::toSdkPath(configPath()), str);
        if (!writeRes) {
            log::error("[PetManager] Failed to write config file: {}", writeRes.unwrapErr());
            return;
        }
}


bool PetManager::shouldShowOnCurrentScene() const {
    auto scene = CCDirector::get()->getRunningScene();
    if (!scene) return false;

    if (sceneMatchesAnyLayer(scene, PET_GAMEPLAY_LAYER_OPTIONS)) {
        return m_config.showInGameplay;
    }

    if (m_config.allLayers) {
        return true;
    }

    if (m_config.visibleLayers.empty()) return false;

    std::vector<std::string> visibleLayersVec(m_config.visibleLayers.begin(), m_config.visibleLayers.end());
    return sceneMatchesAnyLayer(scene, visibleLayersVec);
}


std::vector<std::string> PetManager::getGalleryImages() const {
    std::vector<std::string> result;
    auto dir = galleryDir();
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || ec) return result;

    for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        auto ext = paimon::paths::toUtf8(entry.path().extension());
        ext = geode::utils::string::toLower(ext);
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".gif" || ext == ".bmp" || ext == ".webp"
            || ext == ".tiff" || ext == ".tif" || ext == ".tga" || ext == ".psd" || ext == ".qoi" || ext == ".jxl") {
            result.push_back(paimon::paths::toUtf8(entry.path().filename()));
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::string PetManager::addToGallery(std::filesystem::path const& srcPath) {
    auto dir = galleryDir();
    auto filename = paimon::paths::toUtf8(srcPath.filename());

    auto dest = dir / filename;
    int counter = 1;
    std::error_code existsEc;
    while (std::filesystem::exists(dest, existsEc) && !existsEc) {
        auto stem = paimon::paths::toUtf8(srcPath.stem());
        auto ext = paimon::paths::toUtf8(srcPath.extension());
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

    purgeCachedImage(filename);

    if (m_config.selectedImage == filename) {
        m_config.selectedImage = "";
    }

    auto clearStateImage = [&](std::string& image) {
        if (image == filename) {
            image.clear();
        }
    };
    clearStateImage(m_config.idleImage);
    clearStateImage(m_config.walkImage);
    clearStateImage(m_config.sleepImage);
    clearStateImage(m_config.reactImage);

    saveConfig();
    reloadSprite();
}

void PetManager::removeAllFromGallery() {
    auto images = getGalleryImages();
    for (auto& img : images) {
        auto path = galleryDir() / img;
        std::error_code rmAllEc;
        if (std::filesystem::exists(path, rmAllEc)) {
            std::filesystem::remove(path, rmAllEc);
        }
        purgeCachedImage(img);
    }

    m_config.selectedImage.clear();
    m_config.idleImage.clear();
    m_config.walkImage.clear();
    m_config.sleepImage.clear();
    m_config.reactImage.clear();
    saveConfig();
    reloadSprite();
}

int PetManager::cleanupInvalidImages() {
    // magic signatures as {min bytes, offset, pattern, group}: a group with
    // several parts (webp) is valid only when every part matches.
    struct MagicPart {
        std::size_t minLen;
        std::size_t offset;
        std::string_view bytes;
        unsigned group;
    };
    static constexpr MagicPart kMagicParts[] = {
        {4, 0, "\x89PNG", 0},          // png: 89 50 4e 47
        {3, 0, "\xFF\xD8\xFF", 1},      // jpeg: ff d8 ff
        {4, 0, "GIF8", 2},             // gif: gif8
        {12, 0, "RIFF", 3},            // webp: riff....webp
        {12, 8, "WEBP", 3},
        {2, 0, "BM", 4},               // bmp: bm
        {4, 0, std::string_view("\x49\x49\x2A\x00", 4), 5}, // tiff le: ii*\0
        {4, 0, "\x4D\x4D\x00\x2A", 6},  // tiff be: mm\0*
        {4, 0, "qoif", 7},             // qoi: qoif
        {12, 0, std::string_view("\x00\x00\x00\x0C" "JXL" "\x20\x0C", 9), 8}, // jxl
    };
    static constexpr unsigned kMagicGroups = 9;
    auto matchesMagic = [](unsigned char const* header, std::size_t bytesRead) {
        for (unsigned g = 0; g < kMagicGroups; ++g) {
            bool any = false;
            bool ok = true;
            for (auto const& p : kMagicParts) {
                if (p.group != g) continue;
                any = true;
                if (bytesRead < p.minLen || bytesRead - p.offset < p.bytes.size() ||
                    std::string_view(reinterpret_cast<char const*>(header + p.offset), p.bytes.size()) != p.bytes) {
                    ok = false;
                    break;
                }
            }
            if (any && ok) return true;
        }
        return false;
    };

    auto images = getGalleryImages();
    int removed = 0;
    auto reject = [&](std::string const& img) {
        removeFromGallery(img);
        ++removed;
    };

    for (auto& img : images) {
        auto path = galleryDir() / paimon::assets::pathFromUtf8(img);
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) continue;

        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) {
            reject(img);
            continue;
        }

        unsigned char header[12] = {};
        f.read(reinterpret_cast<char*>(header), 12);
        auto bytesRead = static_cast<std::size_t>(f.gcount());
        f.close();

        if (bytesRead < 4) {
            log::warn("[PetManager] File too small, removing: {}", img);
            reject(img);
            continue;
        }

        if (!matchesMagic(header, bytesRead)) {
            log::warn("[PetManager] Invalid image file, removing: {} (bytes: {:02x} {:02x} {:02x} {:02x})",
                img, header[0], header[1], header[2], header[3]);
            reject(img);
        }
    }

    return removed;
}

std::string PetManager::addAudioToLibrary(std::filesystem::path const& srcPath) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(srcPath, ec) || ec) return "";

    auto ext = geode::utils::string::toLower(paimon::paths::toUtf8(srcPath.extension()));
    static constexpr std::array<std::string_view, 7> kSupported = {
        ".mp3", ".ogg", ".wav", ".flac", ".oga", ".m4a", ".opus"};
    if (std::find(kSupported.begin(), kSupported.end(), ext) == kSupported.end()) {
        return "";
    }

    auto dir = audioDir();
    auto filename = paimon::paths::toUtf8(srcPath.filename());
    auto dest = dir / paimon::assets::pathFromUtf8(filename);

    // If source is already inside pet_audio, reuse its filename directly.
    std::error_code eqEc;
    if (std::filesystem::exists(dest, eqEc) && std::filesystem::equivalent(srcPath, dest, eqEc)) {
        return filename;
    }

    int counter = 1;
    std::error_code existsEc;
    while (std::filesystem::exists(dest, existsEc) && !existsEc) {
        auto stem = paimon::paths::toUtf8(srcPath.stem());
        filename = fmt::format("{}_{}{}", stem, counter++, ext);
        dest = dir / paimon::assets::pathFromUtf8(filename);
    }

    std::error_code copyEc;
    std::filesystem::copy_file(srcPath, dest, std::filesystem::copy_options::overwrite_existing, copyEc);
    if (copyEc) {
        log::error("[PetManager] Failed to copy audio to library: {}", copyEc.message());
        return "";
    }
    return filename;
}

std::vector<std::string> PetManager::getAudioLibraryFiles() const {
    std::vector<std::string> out;
    auto dir = audioDir();
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || ec) return out;
    for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        auto ext = geode::utils::string::toLower(paimon::paths::toUtf8(entry.path().extension()));
        if (ext == ".mp3" || ext == ".ogg" || ext == ".wav" || ext == ".flac"
            || ext == ".oga" || ext == ".m4a" || ext == ".opus") {
            out.push_back(paimon::paths::toUtf8(entry.path().filename()));
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

void PetManager::stopActiveAudio() {
    if (m_activeChannel && !paimon::isRuntimeShuttingDown()) {
        FMOD::Sound* currentSound = nullptr;
        if (m_activeSound &&
            m_activeChannel->getCurrentSound(&currentSound) == FMOD_OK &&
            currentSound == m_activeSound) {
            m_activeChannel->stop();
        }
    }
    m_activeChannel = nullptr;
    if (m_activeSound) {
        if (!paimon::isRuntimeShuttingDown()) {
            m_activeSound->release();
        }
        m_activeSound = nullptr;
    }
}

bool PetManager::playAudioEntry(std::string const& entry, bool applyPitchJitter) {
    if (entry.empty() || !m_config.enableAudio || paimon::isRuntimeShuttingDown()) return false;
    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine) return false;

    float const userVol = std::clamp(m_config.audioVolume, 0.f, 1.f);
    if (userVol <= 0.001f) return false;

    float pitch = 1.f;
    if (applyPitchJitter && m_config.audioPitchRandom > 0.001f) {
        static thread_local std::mt19937 rng{std::random_device{}()};
        std::uniform_real_distribution<float> dist(-m_config.audioPitchRandom, m_config.audioPitchRandom);
        pitch = std::clamp(1.f + dist(rng), 0.5f, 2.f);
    }

    // Native GD SFX entry: "gd:chestClick.ogg"
    // playEffect already multiplies by GD's internal SFX volume, so pass userVol directly.
    if (entry.rfind("gd:", 0) == 0) {
        std::string const sfxName = entry.substr(3);
        if (sfxName.empty()) return false;
        engine->playEffect(sfxName, pitch, 0.f, userVol);
        return true;
    }

    float const baseVol = userVol * engine->getEffectsVolume();
    if (baseVol <= 0.001f) return false;

    if (!engine->m_system) return false;

    std::filesystem::path resolved = audioDir() / paimon::assets::pathFromUtf8(entry);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(resolved, ec) || ec) {
        auto absCandidate = paimon::assets::pathFromUtf8(entry);
        if (std::filesystem::is_regular_file(absCandidate, ec) && !ec) {
            resolved = absCandidate;
        } else {
            return false;
        }
    }

    stopActiveAudio();
    std::string const pathStr = paimon::paths::toUtf8(resolved);
    FMOD::Sound* sound = nullptr;
    if (engine->m_system->createSound(pathStr.c_str(), FMOD_DEFAULT, nullptr, &sound) != FMOD_OK || !sound) {
        return false;
    }
    FMOD::Channel* channel = nullptr;
    if (engine->m_system->playSound(sound, nullptr, true, &channel) != FMOD_OK || !channel) {
        sound->release();
        return false;
    }
    channel->setVolume(baseVol);
    channel->setPitch(pitch);
    channel->setPaused(false);

    m_activeSound = sound;
    m_activeChannel = channel;
    return true;
}

bool PetManager::playRandomFromPool(std::vector<std::string> const& pool, std::string const& poolKey) {
    if (pool.empty() || !m_config.enableAudio) return false;
    static thread_local std::mt19937 rng{std::random_device{}()};
    int const n = static_cast<int>(pool.size());
    int startIdx = 0;
    if (n > 1) {
        int last = -1;
        if (auto it = m_lastAudioIndex.find(poolKey); it != m_lastAudioIndex.end()) {
            last = it->second;
        }
        std::uniform_int_distribution<int> dist(0, n - 1);
        startIdx = dist(rng);
        if (startIdx == last) startIdx = (startIdx + 1 + dist(rng) % (n - 1)) % n;
    }
    for (int offset = 0; offset < n; ++offset) {
        int const idx = (startIdx + offset) % n;
        if (playAudioEntry(pool[static_cast<size_t>(idx)], true)) {
            m_lastAudioIndex[poolKey] = idx;
            return true;
        }
    }
    return false;
}

int PetManager::affectionLevel() const {
    // Progressive XP curve: level 1 starts at 0 XP; each level needs 20 + (level-1)*15 XP.
    int xp = std::max(0, m_config.affectionXp);
    int lvl = 1;
    while (true) {
        int need = 20 + (lvl - 1) * 15;
        if (xp < need) break;
        xp -= need;
        ++lvl;
        if (lvl >= 99) break;
    }
    return lvl;
}

int PetManager::affectionLevelProgressXp() const {
    int xp = std::max(0, m_config.affectionXp);
    int lvl = 1;
    while (lvl < 99) {
        int need = 20 + (lvl - 1) * 15;
        if (xp < need) return xp;
        xp -= need;
        ++lvl;
    }
    return 0;
}

int PetManager::affectionLevelNeededXp() const {
    int lvl = affectionLevel();
    return 20 + (lvl - 1) * 15;
}

void PetManager::addAffectionXp(int amount) {
    if (amount <= 0) return;
    int const beforeLvl = affectionLevel();
    m_config.affectionXp += amount;
    int const afterLvl = affectionLevel();
    if (afterLvl > beforeLvl) {
        PaimonNotify::show(
            fmt::format("Tu mascota subio a Afinidad Nv.{}!", afterLvl),
            NotificationIcon::Success);
        triggerGesture(PetGesture::Celebrate, false, true);
        m_unsavedStatAdds = 0;
        saveConfig();
        return;
    }
    if (++m_unsavedStatAdds >= 5) {
        m_unsavedStatAdds = 0;
        saveConfig();
    }
}

CCTexture2D* PetManager::loadGalleryThumb(std::string const& filename) const {
    auto path = galleryDir() / paimon::assets::pathFromUtf8(filename);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) return nullptr;

    auto img = ImageLoadHelper::loadStaticImage(path);
    if (img.success && img.texture) {
        return img.texture; // caller manages lifetime
    }
    return nullptr;
}


void PetManager::init() {
    std::srand(static_cast<unsigned int>(std::time(nullptr)));
    log::info("[PetManager] init");
    loadConfig();
}

void PetManager::setImage(std::string const& galleryFilename) {
    log::info("[PetManager] setImage: {}", galleryFilename);
    m_config.selectedImage = galleryFilename;
    saveConfig();
    reloadSprite();
}

std::string PetManager::resolveImageFileForState(PetIconState state) const {
    std::vector<std::string> candidates;
    if (auto stateImage = getIconStateImage(state); !stateImage.empty()) {
        candidates.push_back(stateImage);
    }
    if (!m_config.selectedImage.empty()) {
        candidates.push_back(m_config.selectedImage);
    }
    if (!m_config.idleImage.empty()) {
        candidates.push_back(m_config.idleImage);
    }
    if (!m_config.walkImage.empty()) {
        candidates.push_back(m_config.walkImage);
    }
    if (!m_config.sleepImage.empty()) {
        candidates.push_back(m_config.sleepImage);
    }
    if (!m_config.reactImage.empty()) {
        candidates.push_back(m_config.reactImage);
    }

    std::unordered_set<std::string> seen;
    for (auto const& imageFile : candidates) {
        if (imageFile.empty() || !seen.insert(imageFile).second) {
            continue;
        }

        auto path = galleryDir() / paimon::assets::pathFromUtf8(imageFile);
        std::error_code ec;
        if (std::filesystem::exists(path, ec) && !ec) {
            return imageFile;
        }
    }

    return "";
}

std::string PetManager::resolveCurrentImageFile() const {
    return resolveImageFileForState(m_iconState);
}

void PetManager::purgeCachedImage(std::string const& filename) {
    if (filename.empty()) return;
    m_staticTextureCache.erase(filename);
    AnimatedGIFSprite::remove(paimon::paths::toUtf8(galleryDir() / filename));
}

CCSprite* PetManager::createSpriteForImage(std::string const& imageFile) {
    if (imageFile.empty()) return nullptr;

    auto path = galleryDir() / paimon::assets::pathFromUtf8(imageFile);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) return nullptr;

    if (ImageLoadHelper::isAnimatedImage(path)) {
        auto pathStr = paimon::paths::toUtf8(path);
        if (auto* cached = AnimatedGIFSprite::createFromCache(pathStr)) {
            return cached;
        }
        return AnimatedGIFSprite::create(pathStr);
    }

    auto it = m_staticTextureCache.find(imageFile);
    if (it == m_staticTextureCache.end() || !it->second) {
        auto img = ImageLoadHelper::loadStaticImage(path);
        if (!img.success || !img.texture) {
            return nullptr;
        }
        it = m_staticTextureCache.emplace(imageFile, geode::Ref<CCTexture2D>::adopt(img.texture)).first;
    }

    if (!it->second) return nullptr;
    return CCSprite::createWithTexture(it->second.data());
}

bool PetManager::switchSpriteToImage(std::string const& imageFile) {
    if (!m_petNode || imageFile.empty()) return false;
    if (m_activeImageFile == imageFile && m_petSprite && m_petSprite->getParent() == m_petNode) {
        return true;
    }

    auto* sprite = createSpriteForImage(imageFile);
    if (!sprite) {
        return false;
    }

    auto prevPos = m_petSprite ? m_petSprite->getPosition() : m_currentPos;
    float prevRotation = m_petSprite ? m_petSprite->getRotation() : 0.f;

    if (m_petSprite && m_petSprite->getParent() == m_petNode) {
        m_petSprite->removeFromParent();
    }

    m_petSprite = sprite;
    m_activeImageFile = imageFile;
    m_petSprite->setScale(m_config.scale);
    m_petSprite->setOpacity(static_cast<GLubyte>(m_config.opacity));
    m_petSprite->setFlipX(!m_facingRight);
    m_petSprite->setRotation(prevRotation);
    m_petSprite->setPosition(prevPos);
    m_petNode->addChild(m_petSprite, 10);
    return true;
}

void PetManager::createPetSprite() {
    auto imageFile = resolveCurrentImageFile();
    if (imageFile.empty()) return;
    (void)switchSpriteToImage(imageFile);
}

void PetManager::reloadSprite() {
    if (m_petSprite && m_petNode) {
        m_petSprite->removeFromParent();
        m_petSprite = nullptr;
    }
    if (m_trail) {
        if (m_trail->getParent()) {
            m_trail->removeFromParent();
        }
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
    m_activeImageFile.clear();

    m_iconState = PetIconState::Default;
    m_sleeping = false;
    m_idleDuration = 0.f;
    m_reactionTimer = 0.f;
    m_reactionJumpVel = 0.f;
    m_reactionJumpOffset = 0.f;
    m_clickReactionTimer = 0.f;
    m_clickCooldown = 0.f;
    m_clickJumpVel = 0.f;
    m_clickJumpOffset = 0.f;
    m_gestureTimer = 0.f;
    m_speechTimer = 0.f;
    m_speechIdleAccum = 0.f;
    m_speechFullText.clear();
    m_speechVisibleChars = 0;

    if (!m_config.enabled) return;
    if (!m_petNode) return;

    createPetSprite();
    if (m_petSprite) {
        m_petSprite->setScale(m_config.scale);
        m_petSprite->setOpacity(static_cast<GLubyte>(m_config.opacity));

        updateTrail();
        createShadow();
        createParticleNode();
        createSpeechBubbleNode();
    }
}

void PetManager::attachToScene(CCScene* scene) {
    log::debug("[PetManager] attachToScene: enabled={}", m_config.enabled);
    if (!scene) return;

    if (paimon::isEditorScene()) {
        detachFromScene();
        return;
    }

    // already on this scene — refresh visibility only.
    if (m_petNode && m_petNode->getParent() == scene) {
        refreshVisibility();
        return;
    }

    detachFromScene();

    if (!m_config.enabled) return;

    m_petNode = CCNode::create();
    m_petNode->setID("paimon-pet-host"_spr);
    m_petNode->setZOrder(kPetHostZOrder);
    scene->addChild(m_petNode);
    m_petNode->setVisible(shouldShowOnCurrentScene());

    auto winSize = CCDirector::get()->getWinSize();
    m_currentPos = ccp(winSize.width / 2.f, winSize.height / 2.f);
    m_targetPos = m_currentPos;
    m_velocity = ccp(0, 0);

    reloadSprite();
}

void PetManager::detachFromScene() {
    if (m_trail) {
        if (m_trail->getParent()) {
            m_trail->removeFromParent();
        }
        m_trail = nullptr;
    }

    if (m_petNode) {
        m_petNode->removeFromParent();
        m_petNode = nullptr;
        m_petSprite = nullptr;
        m_shadowSprite = nullptr;
        m_particleNode = nullptr;
        m_speechNode = nullptr;
        m_speechLabel = nullptr;
        m_speechBg = nullptr;
        m_sleepZzz = nullptr;
        m_activeImageFile.clear();
    }
    m_sleeping = false;
    m_reactionTimer = 0.f;
    m_reactionJumpVel = 0.f;
    m_reactionJumpOffset = 0.f;
    m_clickReactionTimer = 0.f;
    m_clickCooldown = 0.f;
    m_clickJumpVel = 0.f;
    m_clickJumpOffset = 0.f;
    m_gestureTimer = 0.f;
    m_speechTimer = 0.f;
    m_speechFullText.clear();
    m_speechVisibleChars = 0;
    m_pendingClicks.clear();
}

void PetManager::refreshVisibility() {
    if (!m_petNode) return;
    m_petNode->setVisible(m_config.enabled && shouldShowOnCurrentScene());
}

void PetManager::releaseSharedResources() {
    stopActiveAudio();
    detachFromScene();
    (void)whiteTrailTexture().take();
    for (auto& [_, tex] : m_staticTextureCache) {
        (void)tex.take();
    }
    m_staticTextureCache.clear();
}

void PetManager::onGLContextReload() {
    detachFromScene();
    // release (no take): old context still live, so gldelete is clean.
    whiteTrailTexture() = nullptr;
    m_staticTextureCache.clear();
}


void PetManager::update(float dt) {
    if (!m_config.enabled || !m_petNode || !m_petSprite) return;

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

    // a capture in flight owns the pet's visibility: re-asserting it here would
    // undo the hide pass and leak the pet into level thumbnails.
    bool const captureOwnsVisibility =
        FramebufferCapture::isCapturing() || FramebufferCapture::hasPendingCapture();

    bool shouldShow = shouldShowOnCurrentScene();
    if (!captureOwnsVisibility && m_petNode->isVisible() != shouldShow) {
        m_petNode->setVisible(shouldShow);
    }
    if (!shouldShow) return;

    if (auto* scene = m_petNode->getParentByType<CCScene>()) {
        ensurePetNodeIsFrontmost(scene, m_petNode);
    }

    if (m_audioCooldown > 0.f) m_audioCooldown = std::max(0.f, m_audioCooldown - dt);
    if (m_clickCooldown > 0.f) m_clickCooldown = std::max(0.f, m_clickCooldown - dt);
    if (m_pettingCooldown > 0.f) m_pettingCooldown = std::max(0.f, m_pettingCooldown - dt);
    if (m_pettingSquishTimer > 0.f) m_pettingSquishTimer = std::max(0.f, m_pettingSquishTimer - dt);

    auto mousePos = geode::cocos::getMousePos();
#if defined(GEODE_IS_MOBILE)
    // touch has no mouse: cursor service tracks the finger; negative means no touch yet.
    CCPoint touchPos = CursorManager::get().pointerPos();
    if (touchPos.x >= 0.f && touchPos.y >= 0.f) mousePos = touchPos;
#endif
    if (m_config.wanderMode) {
        auto winSize = CCDirector::get()->getWinSize();
        if (!m_wanderInitialized) {
            m_wanderTarget = m_currentPos;
            m_wanderHoldTimer = 1.5f;
            m_wanderInitialized = true;
        }
        float const dxW = m_wanderTarget.x - m_currentPos.x;
        float const dyW = m_wanderTarget.y - m_currentPos.y;
        if (dxW * dxW + dyW * dyW < 100.f) {
            m_wanderHoldTimer -= dt;
            if (m_wanderHoldTimer <= 0.f) {
                static thread_local std::mt19937 rng{std::random_device{}()};
                float padX = std::min(55.f, winSize.width * 0.15f);
                float padY = std::min(45.f, winSize.height * 0.15f);
                std::uniform_real_distribution<float> rx(padX, std::max(padX + 1.f, winSize.width - padX));
                std::uniform_real_distribution<float> ry(padY, std::max(padY + 1.f, winSize.height - padY));
                std::uniform_real_distribution<float> holdDist(2.5f, 7.0f);
                m_wanderTarget = ccp(rx(rng), ry(rng));
                m_wanderHoldTimer = holdDist(rng);
            }
        }
        m_targetPos = m_wanderTarget;
    } else {
        m_wanderInitialized = false;
        m_targetPos.x = mousePos.x + m_config.offsetX;
        m_targetPos.y = mousePos.y + m_config.offsetY;
    }

    if (m_config.enableClickInteraction && !m_pendingClicks.empty()) {
        auto clicks = std::move(m_pendingClicks);
        m_pendingClicks.clear();
        for (auto const& clickPos : clicks) {
            triggerClickReaction(clickPos);
            if (m_clickCooldown > 0.f) {
                break;
            }
        }
    }

    if (m_config.enablePetting) {
        updatePetting(dt, mousePos);
    }

    float lerpFactor = 1.f - std::pow(1.f - m_config.sensitivity, dt * 60.f);
    lerpFactor = std::max(0.001f, std::min(1.f, lerpFactor));

    CCPoint prevPos = m_currentPos;
    m_currentPos.x += (m_targetPos.x - m_currentPos.x) * lerpFactor;
    m_currentPos.y += (m_targetPos.y - m_currentPos.y) * lerpFactor;

    m_velocity.x = (m_currentPos.x - prevPos.x) / std::max(dt, 0.001f);
    m_velocity.y = (m_currentPos.y - prevPos.y) / std::max(dt, 0.001f);

    float speed = std::sqrt(m_velocity.x * m_velocity.x + m_velocity.y * m_velocity.y);

    m_wasWalking = m_walking;
    m_walking = speed > 8.f;

    if (m_walking || m_reactionTimer > 0.f || m_clickReactionTimer > 0.f || m_gestureTimer > 0.f) {
        m_idleDuration = 0.f;
        if (m_sleeping) {
            m_sleeping = false;
            updateIconState();
            if (m_sleepZzz) m_sleepZzz->setVisible(false);
        }
    } else {
        m_idleDuration += dt;
    }

    if (m_config.enableSleep && !m_sleeping && !m_walking &&
        m_reactionTimer <= 0.f && m_clickReactionTimer <= 0.f && m_gestureTimer <= 0.f &&
        m_idleDuration >= m_config.sleepAfterSeconds) {
        m_sleeping = true;
        updateIconState();
        createSleepZzz();
    }

    if (m_wasWalking && !m_walking && m_config.squishOnLand) {
        m_landSquishTimer = 0.2f;
    }

    if (m_config.flipOnDirection && std::abs(m_velocity.x) > 5.f) {
        m_facingRight = m_velocity.x > 0;
    }
    m_petSprite->setFlipX(!m_facingRight);

    if (m_reactionTimer <= 0.f && m_clickReactionTimer <= 0.f && m_gestureTimer <= 0.f) {
        PetIconState desiredState = PetIconState::Default;
        if (m_sleeping) desiredState = PetIconState::Sleep;
        else if (m_walking) desiredState = PetIconState::Walk;
        else desiredState = PetIconState::Idle;

        if (desiredState != m_iconState) {
            m_iconState = desiredState;
            updateIconState();
        }
    }

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

    if (m_reactionTimer > 0.f) {
        baseRotation += m_reactionSpinVel * (m_reactionTimer / std::max(0.01f, m_config.reactionDuration));
    }

    float gesturePosY = 0.f;
    float gestureScaleX = 1.f;
    float gestureScaleY = 1.f;
    if (m_gestureTimer > 0.f && m_gestureDuration > 0.001f) {
        float const p = std::clamp(1.f - (m_gestureTimer / m_gestureDuration), 0.f, 1.f);
        constexpr float kPi = 3.14159265f;
        switch (m_activeGesture) {
            case PetGesture::Happy: {
                gesturePosY = std::abs(std::sin(p * kPi * 2.f)) * 18.f;
                float s = 1.f + std::sin(p * kPi * 4.f) * 0.08f;
                gestureScaleX = s;
                gestureScaleY = 2.f - s;
                break;
            }
            case PetGesture::Spin: {
                baseRotation += p * 360.f;
                gesturePosY = std::sin(p * kPi) * 14.f;
                float s = 1.f + std::sin(p * kPi) * 0.12f;
                gestureScaleX = s;
                gestureScaleY = s;
                break;
            }
            case PetGesture::Wave: {
                baseRotation += std::sin(p * kPi * 6.f) * 20.f * (1.f - p * 0.3f);
                gesturePosY = std::abs(std::sin(p * kPi * 3.f)) * 5.f;
                break;
            }
            case PetGesture::Surprised: {
                gesturePosY = std::sin(std::min(1.f, p * 2.2f) * kPi) * 24.f;
                float stretch = (p < 0.35f) ? (0.18f * (1.f - p / 0.35f)) : 0.f;
                gestureScaleX = 1.f - stretch;
                gestureScaleY = 1.f + stretch;
                break;
            }
            case PetGesture::Love: {
                float pulse = std::sin(p * kPi * 4.f) * 0.12f;
                gestureScaleX = 1.f + pulse;
                gestureScaleY = 1.f + pulse * 0.7f;
                baseRotation += std::sin(p * kPi * 4.f) * 10.f;
                break;
            }
            case PetGesture::Celebrate: {
                gesturePosY = std::abs(std::sin(p * kPi * 3.f)) * 26.f;
                baseRotation += p * 720.f;
                float s = 1.f + std::sin(p * kPi * 3.f) * 0.14f;
                gestureScaleX = s;
                gestureScaleY = s;
                break;
            }
            default: break;
        }
    }

    m_petSprite->setRotation(baseRotation);

    CCPoint finalPos = m_currentPos;

    if (m_reactionTimer > 0.f) {
        m_reactionJumpVel -= 480.f * dt;
        m_reactionJumpOffset += m_reactionJumpVel * dt;
        if (m_reactionJumpOffset < 0.f) {
            m_reactionJumpOffset = 0.f;
            if (m_reactionJumpVel < -35.f) {
                m_reactionJumpVel = -m_reactionJumpVel * 0.45f;
            } else {
                m_reactionJumpVel = 0.f;
            }
        }
        finalPos.y += m_reactionJumpOffset;
    }
    else if (m_clickReactionTimer > 0.f) {
        m_clickJumpVel -= 480.f * dt;
        m_clickJumpOffset += m_clickJumpVel * dt;
        if (m_clickJumpOffset < 0.f) {
            m_clickJumpOffset = 0.f;
            if (m_clickJumpVel < -30.f) {
                m_clickJumpVel = -m_clickJumpVel * 0.42f;
            } else {
                m_clickJumpVel = 0.f;
            }
        }
        finalPos.y += m_clickJumpOffset;
    }
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
    finalPos.y += gesturePosY;

    float musicScaleMul = 1.f;
    if (m_config.reactToMusic && !m_sleeping) {
        auto& audio = PaimonAudio::get();
        if (!audio.isActive()) audio.activate();
        audio.update(dt);
        float pulse = std::clamp(audio.beatPulse(), 0.f, 1.f);
        musicScaleMul = 1.f + pulse * m_config.musicBounceStrength;
        finalPos.y += pulse * 6.f * (m_config.musicBounceStrength / 0.14f);
    }

    float scaleX = m_config.scale;
    float scaleY = m_config.scale;
    if (m_landSquishTimer > 0.f) {
        m_landSquishTimer -= dt;
        float t = std::max(0.f, m_landSquishTimer) / 0.2f;
        scaleX *= (1.f + m_config.squishAmount * t);
        scaleY *= (1.f - m_config.squishAmount * t);
    } else if (m_pettingSquishTimer > 0.f) {
        float t = m_pettingSquishTimer / 0.25f;
        scaleX *= (1.f + 0.16f * t);
        scaleY *= (1.f - 0.12f * t);
    } else {
        if (m_config.idleAnimation && !m_walking && !m_sleeping) {
            float breath = 1.f + std::sin(m_idleTimer * 3.14159f * 2.f) * m_config.idleBreathScale;
            scaleX *= breath;
            scaleY *= (2.f - breath);
        } else if (m_sleeping) {
            float breath = 1.f + std::sin(m_idleTimer * 1.5f) * m_config.idleBreathScale * 0.5f;
            scaleX *= breath;
            scaleY *= (2.f - breath);
        }
    }
    m_petSprite->setScaleX(scaleX * gestureScaleX * musicScaleMul);
    m_petSprite->setScaleY(scaleY * gestureScaleY * musicScaleMul);

    m_petSprite->setPosition(finalPos);

    if (m_trail) {
        m_trail->setPosition(finalPos);
    }

    updateShadow();

    updateParticles(dt);

    updateSpeechBubble(dt);

    updateSleepZzz(dt);

    updateReaction(dt);

    updateClickReaction(dt);

    updateGesture(dt);

    if (m_config.enableSpeech && !m_sleeping && m_speechTimer <= 0.f &&
        m_reactionTimer <= 0.f && m_clickReactionTimer <= 0.f && m_gestureTimer <= 0.f) {
        m_speechIdleAccum += dt;
        if (m_speechIdleAccum >= m_config.speechInterval && !m_config.idleMessages.empty()) {
            m_speechIdleAccum = 0.f;
            showWeightedSpeech(PetDialogueCategory::Idle);
        }
    }

    if (m_config.enableAutoGestures && !m_sleeping && !m_walking &&
        m_reactionTimer <= 0.f && m_clickReactionTimer <= 0.f && m_gestureTimer <= 0.f) {
        m_autoGestureAccum += dt;
        if (m_autoGestureAccum >= m_config.autoGestureInterval) {
            m_autoGestureAccum = 0.f;
            static constexpr PetGesture kIdleGestures[] = {
                PetGesture::Happy, PetGesture::Spin, PetGesture::Wave
            };
            static thread_local std::mt19937 rng{std::random_device{}()};
            std::uniform_int_distribution<size_t> dist(0, std::size(kIdleGestures) - 1);
            triggerGesture(kIdleGestures[dist(rng)], m_config.enableSpeech, true);
        }
    }
}

void PetManager::applyConfigLive() {
    saveConfig();

    if (!m_petNode) return;

    auto imageFile = resolveCurrentImageFile();
    if (!imageFile.empty()) {
        (void)switchSpriteToImage(imageFile);
    }

    if (!m_petSprite) return;

    m_petSprite->setScale(m_config.scale);
    m_petSprite->setOpacity(static_cast<GLubyte>(m_config.opacity));

    if (!m_shadowSprite) createShadow();
    if (!m_particleNode) createParticleNode();
    if (!m_speechNode) createSpeechBubbleNode();

    updateTrail();
    updateShadow();
    if (m_particleNode) {
        m_particleNode->setVisible(m_config.showParticles);
        m_particleNode->removeAllChildren();
        m_particleAccum = 0.f;
    }
    if (m_speechLabel) {
        m_speechLabel->setScale(m_config.speechBubbleScale);
        if (m_speechTimer > 0.f) {
            redrawSpeechBackground();
        }
    }
    if (m_sleepZzz) {
        m_sleepZzz->setVisible(m_sleeping && m_config.enableSleep);
    }
}

void PetManager::updateTrail() {
    if (m_trail) {
        if (m_trail->getParent()) {
            m_trail->removeFromParent();
        }
        m_trail = nullptr;
    }

    if (!m_config.showTrail || !m_petNode || !m_petSprite) return;

    // use a generated white texture for the trail.
    auto& trailTex = whiteTrailTexture();
    if (!trailTex) {
        const int sz = 2;
        uint8_t pixels[sz * sz * 4];
        memset(pixels, 255, sizeof(pixels));
        auto* newTex = new CCTexture2D();
        if (newTex->initWithData(pixels, kCCTexture2DPixelFormat_RGBA8888, sz, sz, CCSizeMake(sz, sz))) {
            trailTex = geode::Ref<CCTexture2D>::adopt(newTex);
        } else {
            newTex->release();
        }
    }

    if (!trailTex) return;

    m_trail = CCMotionStreak::create(
         m_config.trailLength / 60.f,
        1.f,
        m_config.trailWidth,
        ccc3(255, 255, 255),
        trailTex.data()
    );

    if (m_trail && m_trail->getTexture()) {
        m_trail->setOpacity(static_cast<GLubyte>(m_config.opacity * 0.4f));
        ccBlendFunc blend = {GL_SRC_ALPHA, GL_ONE};
        m_trail->setBlendFunc(blend);
        m_petNode->addChild(m_trail, -1);
    } else {
        m_trail = nullptr;
        log::warn("[PetManager] Failed to create trail with valid texture");
    }
}


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
    auto oldFile = getIconStateImage(state);
    switch (state) {
        case PetIconState::Idle:   m_config.idleImage  = galleryFilename; break;
        case PetIconState::Walk:   m_config.walkImage  = galleryFilename; break;
        case PetIconState::Sleep:  m_config.sleepImage = galleryFilename; break;
        case PetIconState::React:  m_config.reactImage = galleryFilename; break;
        default: break;
    }
    if (!oldFile.empty() && oldFile != galleryFilename) {
        purgeCachedImage(oldFile);
    }
    saveConfig();
    if (m_iconState == state) updateIconState();
}

void PetManager::updateIconState() {
    if (!m_petNode) return;

    auto imageFile = resolveCurrentImageFile();
    if (imageFile.empty()) {
        return;
    }

    if (switchSpriteToImage(imageFile)) {
        m_petSprite->setScale(m_config.scale);
        m_petSprite->setOpacity(static_cast<GLubyte>(m_config.opacity));
        m_petSprite->setFlipX(!m_facingRight);
    }
}


void PetManager::createShadow() {
    if (!m_petNode || !m_petSprite) return;

    m_shadowSprite = CCSprite::create();
    if (!m_shadowSprite) return;

    const int sz = 16;
    auto* pixels = new unsigned char[sz * sz * 4];
    memset(pixels, 0, sz * sz * 4);
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

    bool const hasLiveChildren = m_particleNode->getChildrenCount() > 0;
    m_particleNode->setVisible(m_config.showParticles || hasLiveChildren);
    if (!m_config.showParticles && !hasLiveChildren) {
        m_particleAccum = 0.f;
        return;
    }

    if (m_config.showParticles) {
        m_particleAccum += dt;
        float interval = 1.f / std::max(m_config.particleRate, 0.1f);
        while (m_particleAccum >= interval) {
            m_particleAccum -= interval;
            emitParticle();
        }
    } else {
        m_particleAccum = 0.f;
    }

    if (m_particleNode->getChildren()) {
        auto toRemove = std::vector<CCNode*>();
        for (auto* child : CCArrayExt<CCNode*>(m_particleNode->getChildren())) {
            if (!child) continue;

            auto vx = static_cast<CCString*>(child->getUserObject());
            if (vx) {
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

                        float fadeRatio = life / m_config.particleLifetime;
                        if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(child)) {
                            rgba->setOpacity(static_cast<GLubyte>(255 * fadeRatio));
                        }
                        child->setScale(m_config.particleSize / 10.f * fadeRatio);

                        auto* newData = CCString::createWithFormat("%.2f|%.2f|%.3f", velX, velY, life);
                        child->setUserObject(newData);
                    }
                }
            }
        }
        for (auto* n : toRemove) n->removeFromParent();
    }
}

ccColor4F PetManager::particleColor() const {
    return {
        m_config.particleColor.r / 255.f,
        m_config.particleColor.g / 255.f,
        m_config.particleColor.b / 255.f, 1.f
    };
}

void PetManager::attachParticle(CCDrawNode* draw, CCPoint const& pos, float vx, float vy) {
    if (!draw) return;
    draw->setAnchorPoint({0.5f, 0.5f});
    draw->setPosition(pos);
    auto* data = CCString::createWithFormat("%.2f|%.2f|%.3f", vx, vy, m_config.particleLifetime);
    draw->setUserObject(data);
    m_particleNode->addChild(draw);
}

void PetManager::emitParticle() {
    if (!m_particleNode || !m_petSprite) return;

    auto petPos = m_petSprite->getPosition();

    float rx = (rand() % 200 - 100) / 100.f * 15.f;
    float ry = (rand() % 200 - 100) / 100.f * 10.f;

    float vx = (rand() % 200 - 100) / 100.f * 20.f;
    float vy = (rand() % 100) / 100.f * 30.f + 10.f;

    float s = m_config.particleSize;
    ccColor4F col = particleColor();
    CCPoint pos{petPos.x + rx, petPos.y + ry};

    auto draw = CCDrawNode::create();
    if (!draw) return;

    auto type = static_cast<PetParticleType>(m_config.particleType);

    switch (type) {
        case PetParticleType::Hearts: {
            CCPoint pts[] = {
                ccp(-s * 0.5f, 0), ccp(0, -s * 0.7f), ccp(s * 0.5f, 0),
                ccp(s * 0.25f, s * 0.3f), ccp(0, s * 0.15f), ccp(-s * 0.25f, s * 0.3f)
            };
            draw->drawPolygon(pts, 6, col, 0, col);
            draw->setContentSize(CCSizeMake(s, s));
            break;
        }
        case PetParticleType::Stars: {
            std::vector<CCPoint> starPts;
            for (int i = 0; i < 10; i++) {
                float angle = (i * 36.f) * 3.14159f / 180.f - 90.f * 3.14159f / 180.f;
                float r = (i % 2 == 0) ? s : s * 0.4f;
                starPts.push_back(ccp(cosf(angle) * r, sinf(angle) * r));
            }
            draw->drawPolygon(starPts.data(), static_cast<unsigned int>(starPts.size()), col, 0, col);
            draw->setContentSize(CCSizeMake(s * 2, s * 2));
            break;
        }
        case PetParticleType::Bubbles:
        case PetParticleType::Sparkles:
        default: {
            draw->drawDot(ccp(0, 0), s, col);
            draw->setContentSize(CCSizeMake(s * 2, s * 2));
            break;
        }
    }

    attachParticle(draw, pos, vx, vy);
}

void PetManager::emitBurstParticles(PetParticleType type, ccColor4F const& col, int count) {
    if (!m_particleNode || !m_petSprite || count <= 0) return;
    m_particleNode->setVisible(true);
    auto petPos = m_petSprite->getPosition();
    float s = std::max(4.f, m_config.particleSize);
    for (int n = 0; n < count; ++n) {
        float angle = (static_cast<float>(n) / static_cast<float>(count)) * 6.283185f + ((rand() % 60 - 30) / 100.f);
        float speed = 22.f + static_cast<float>(rand() % 30);
        float vx = std::cos(angle) * speed;
        float vy = std::sin(angle) * speed + 24.f;
        CCPoint pos{petPos.x + (rand() % 20 - 10) * 0.8f, petPos.y + 8.f + (rand() % 12) * 0.6f};
        auto draw = CCDrawNode::create();
        if (!draw) continue;
        switch (type) {
            case PetParticleType::Hearts: {
                CCPoint pts[] = {
                    ccp(-s * 0.55f, 0), ccp(0, -s * 0.75f), ccp(s * 0.55f, 0),
                    ccp(s * 0.28f, s * 0.35f), ccp(0, s * 0.16f), ccp(-s * 0.28f, s * 0.35f)
                };
                draw->drawPolygon(pts, 6, col, 0, col);
                draw->setContentSize(CCSizeMake(s, s));
                break;
            }
            case PetParticleType::Stars: {
                std::vector<CCPoint> starPts;
                for (int i = 0; i < 10; i++) {
                    float a = (i * 36.f) * 3.14159f / 180.f - 90.f * 3.14159f / 180.f;
                    float r = (i % 2 == 0) ? s : s * 0.42f;
                    starPts.push_back(ccp(cosf(a) * r, sinf(a) * r));
                }
                draw->drawPolygon(starPts.data(), static_cast<unsigned int>(starPts.size()), col, 0, col);
                draw->setContentSize(CCSizeMake(s * 2, s * 2));
                break;
            }
            default: {
                draw->drawDot(ccp(0, 0), s, col);
                draw->setContentSize(CCSizeMake(s * 2, s * 2));
                break;
            }
        }
        attachParticle(draw, pos, vx, vy);
    }
}


void PetManager::createSpeechBubbleNode() {
    if (!m_petNode) return;

    m_speechNode = CCNode::create();
    if (!m_speechNode) return;
    m_speechNode->setID("pet-speech"_spr);
    m_speechNode->setVisible(false);
    m_petNode->addChild(m_speechNode, 20);

    m_speechBg = CCDrawNode::create();
    if (m_speechBg) {
        m_speechBg->setVisible(false);
        m_speechNode->addChild(m_speechBg);
    }

    m_speechLabel = CCLabelBMFont::create("", "chatFont.fnt");
    if (m_speechLabel) {
        m_speechLabel->setScale(m_config.speechBubbleScale);
        m_speechLabel->setVisible(false);
        m_speechNode->addChild(m_speechLabel);
    }
}

void PetManager::redrawSpeechBackground() {
    if (!m_speechNode || !m_speechLabel || !m_speechBg || !m_petSprite) return;

    auto const winSize = CCDirector::get()->getWinSize();
    auto const petPos = m_petSprite->getPosition();
    float const petHalfH = m_petSprite->getContentSize().height * std::abs(m_config.scale) * 0.5f;

    float const labelWidth = m_speechLabel->getContentSize().width * m_config.speechBubbleScale;
    float const labelHeight = m_speechLabel->getContentSize().height * m_config.speechBubbleScale;

    float const bubbleW = std::max(32.f, labelWidth + 16.f);
    float const bubbleH = std::max(18.f, labelHeight + 10.f);

    float bubbleX = petPos.x;
    if (winSize.width > bubbleW + 12.f) {
        bubbleX = std::clamp(bubbleX, bubbleW * 0.5f + 6.f, winSize.width - bubbleW * 0.5f - 6.f);
    }

    bool placeBelow = false;
    float bubbleY = petPos.y + petHalfH + bubbleH * 0.5f + 12.f;
    if (winSize.height > 0.f && bubbleY + bubbleH * 0.5f > winSize.height - 6.f) {
        placeBelow = true;
        bubbleY = petPos.y - petHalfH - bubbleH * 0.5f - 12.f;
        bubbleY = std::max(bubbleH * 0.5f + 6.f, bubbleY);
    }

    m_speechNode->setPosition({bubbleX, bubbleY});
    m_speechLabel->setPosition({0.f, 0.f});
    m_speechLabel->setAnchorPoint({0.5f, 0.5f});
    m_speechLabel->setColor(m_config.speechTextColor);

    m_speechBg->clear();
    ccColor4F bgCol = {
        m_config.speechBgColor.r / 255.f,
        m_config.speechBgColor.g / 255.f,
        m_config.speechBgColor.b / 255.f,
        0.84f
    };
    ccColor4F borderCol = {
        m_config.speechBorderColor.r / 255.f,
        m_config.speechBorderColor.g / 255.f,
        m_config.speechBorderColor.b / 255.f,
        0.72f
    };

    float const w = bubbleW;
    float const h = bubbleH;
    CCPoint rect[4] = {
        ccp(-w / 2.f, -h / 2.f), ccp(w / 2.f, -h / 2.f),
        ccp(w / 2.f, h / 2.f), ccp(-w / 2.f, h / 2.f)
    };
    m_speechBg->drawPolygon(rect, 4, bgCol, 1.f, borderCol);

    float const tailX = std::clamp(petPos.x - bubbleX, -w * 0.35f, w * 0.35f);
    if (placeBelow) {
        CCPoint tri[3] = {
            ccp(tailX - 4.f, h / 2.f), ccp(tailX + 4.f, h / 2.f), ccp(tailX, h / 2.f + 5.f)
        };
        m_speechBg->drawPolygon(tri, 3, bgCol, 1.f, borderCol);
    } else {
        CCPoint tri[3] = {
            ccp(tailX - 4.f, -h / 2.f), ccp(tailX + 4.f, -h / 2.f), ccp(tailX, -h / 2.f - 5.f)
        };
        m_speechBg->drawPolygon(tri, 3, bgCol, 1.f, borderCol);
    }

    m_speechBg->setPosition({0.f, 0.f});
    m_speechBg->setVisible(true);
}

void PetManager::showSpeechBubble(std::string const& message, bool bypassSpeechToggle) {
    if (!m_petNode || !m_petSprite) return;
    if (!m_speechNode || !m_speechLabel || !m_speechBg) {
        createSpeechBubbleNode();
    }
    if (!m_speechNode || !m_speechLabel || !m_speechBg) return;
    if ((!m_config.enableSpeech && !bypassSpeechToggle) || message.empty()) return;

    m_speechFullText = message;
    m_speechRevealProgress = 0.f;
    m_speechLabel->setScale(m_config.speechBubbleScale);
    if (m_config.speechTypewriter && message.size() > 1) {
        size_t firstEnd = 1;
        while (firstEnd < message.size() &&
               (static_cast<unsigned char>(message[firstEnd]) & 0xC0) == 0x80) {
            ++firstEnd;
        }
        m_speechVisibleChars = firstEnd;
        m_speechLabel->setString(message.substr(0, m_speechVisibleChars).c_str());
    } else {
        m_speechVisibleChars = message.size();
        m_speechLabel->setString(message.c_str());
    }
    m_speechLabel->setVisible(true);

    redrawSpeechBackground();

    m_speechNode->setVisible(true);
    m_speechTimer = std::max(1.f, m_config.speechDuration);

    m_speechNode->setScale(0.01f);
    m_speechNode->stopAllActions();
    m_speechNode->runAction(CCEaseBackOut::create(CCScaleTo::create(0.2f, 1.f)));
}

int PetManager::showWeightedSpeech(
    PetDialogueCategory category,
    bool* outPlayedOverrideAudio,
    bool bypassSpeechToggle
) {
    if (outPlayedOverrideAudio) *outPlayedOverrideAudio = false;
    auto const& entries = m_config.dialoguesFor(category);
    if (entries.empty()) return -1;

    float totalWeight = 0.f;
    int validCount = 0;
    for (auto const& e : entries) {
        if (!e.text.empty() && e.weight > 0.f) {
            totalWeight += e.weight;
            ++validCount;
        }
    }
    if (validCount == 0 || totalWeight <= 0.f) return -1;

    int const catKey = static_cast<int>(category);
    int lastIdx = -1;
    if (auto it = m_lastDialogueIndex.find(catKey); it != m_lastDialogueIndex.end()) {
        lastIdx = it->second;
    }

    static thread_local std::mt19937 rng{std::random_device{}()};
    int chosen = -1;
    for (int attempt = 0; attempt < 4; ++attempt) {
        std::uniform_real_distribution<float> dist(0.f, totalWeight);
        float pick = dist(rng);
        float accum = 0.f;
        for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
            auto const& e = entries[static_cast<size_t>(i)];
            if (e.text.empty() || e.weight <= 0.f) continue;
            accum += e.weight;
            if (pick <= accum) {
                chosen = i;
                break;
            }
        }
        if (chosen < 0) {
            for (int i = static_cast<int>(entries.size()) - 1; i >= 0; --i) {
                if (!entries[static_cast<size_t>(i)].text.empty() && entries[static_cast<size_t>(i)].weight > 0.f) {
                    chosen = i;
                    break;
                }
            }
        }
        if (validCount <= 1 || chosen != lastIdx) break;
    }
    if (chosen < 0) return -1;

    m_lastDialogueIndex[catKey] = chosen;
    auto const& entry = entries[static_cast<size_t>(chosen)];
    if (m_config.enableSpeech || bypassSpeechToggle) {
        showSpeechBubble(entry.text, bypassSpeechToggle);
    }
    if (!entry.audioFile.empty() && m_config.enableAudio && m_audioCooldown <= 0.f) {
        if (playAudioEntry(entry.audioFile, true)) {
            m_audioCooldown = 0.1f;
            if (outPlayedOverrideAudio) *outPlayedOverrideAudio = true;
        }
    }
    return chosen;
}

void PetManager::hideSpeechBubble() {
    if (!m_speechNode) return;

    m_speechTimer = 0.f;
    m_speechFullText.clear();
    m_speechVisibleChars = 0;

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

    if (m_config.speechTypewriter && m_speechLabel && m_speechVisibleChars < m_speechFullText.size()) {
        // ~32 characters per second, stepping on UTF-8 codepoint boundaries.
        m_speechRevealProgress += dt * 32.f;
        size_t target = std::min(m_speechFullText.size(),
            static_cast<size_t>(1.f + m_speechRevealProgress));
        while (target < m_speechFullText.size()
               && (static_cast<unsigned char>(m_speechFullText[target]) & 0xC0) == 0x80) {
            ++target;
        }
        if (target > m_speechVisibleChars) {
            m_speechVisibleChars = target;
            m_speechLabel->setString(m_speechFullText.substr(0, m_speechVisibleChars).c_str());
            redrawSpeechBackground();
        }
    }

    if (m_petSprite && m_speechLabel && m_speechBg) {
        redrawSpeechBackground();
    }

    if (m_speechTimer <= 0.f) {
        hideSpeechBubble();
    }
}


void PetManager::createSleepZzz() {
    if (!m_petNode || m_sleepZzz) return;

    m_sleepZzz = CCNode::create();
    if (!m_sleepZzz) return;
    m_sleepZzz->setID("pet-sleep-zzz"_spr);

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

    m_sleepZzzTimer += dt;
    auto petPos = m_petSprite->getPosition();
    float baseY = petPos.y + m_petSprite->getContentSize().height * m_config.scale * 0.5f + 15.f;
    float bobY = std::sin(m_sleepZzzTimer * 2.f) * m_config.sleepBobAmount;
    m_sleepZzz->setPosition({petPos.x + 10.f, baseY + bobY});

    float alpha = 128 + 60 * std::sin(m_sleepZzzTimer * 1.5f);
    auto* zzzChildren = m_sleepZzz->getChildren();
    if (zzzChildren && zzzChildren->count() > 0) {
        if (auto* label = typeinfo_cast<CCRGBAProtocol*>(zzzChildren->objectAtIndex(0))) {
            label->setOpacity(static_cast<GLubyte>(alpha));
        }
    }
}


void PetManager::triggerReaction(std::string const& eventType) {
    if (!m_petSprite || !m_config.enabled) return;

    if (eventType == "level_complete" && !m_config.reactToLevelComplete) return;
    if (eventType == "death" && !m_config.reactToDeath) return;
    if (eventType == "practice_exit" && !m_config.reactToPracticeExit) return;

    if (m_sleeping) {
        m_sleeping = false;
        m_idleDuration = 0.f;
        if (m_sleepZzz) m_sleepZzz->setVisible(false);
    }

    m_iconState = PetIconState::React;
    updateIconState();

    m_reactionTimer = m_config.reactionDuration;
    m_reactionJumpOffset = 0.f;
    m_reactionJumpVel = std::sqrt(2.f * 480.f * std::max(4.f, m_config.reactionJumpHeight));
    m_reactionSpinVel = m_config.reactionSpinSpeed;

    bool playedOverride = false;
    if (eventType == "level_complete") {
        if (m_config.enableSpeech) showWeightedSpeech(PetDialogueCategory::LevelComplete, &playedOverride);
        if (!playedOverride) {
            playRandomFromPool(m_config.audioPoolForGesture(PetGesture::Celebrate), "gesture:celebrate");
        }
        emitBurstParticles(PetParticleType::Stars, {1.f, 0.9f, 0.3f, 1.f}, 10);
        addAffectionXp(5);
    } else if (eventType == "death") {
        if (m_config.enableSpeech) showWeightedSpeech(PetDialogueCategory::Death, &playedOverride);
        if (!playedOverride) {
            playRandomFromPool(m_config.audioPoolForGesture(PetGesture::Surprised), "gesture:surprised");
        }
    } else if (eventType == "practice_exit") {
        playRandomFromPool(m_config.audioPoolForGesture(PetGesture::Wave), "gesture:wave");
    }
}

void PetManager::triggerGesture(PetGesture gesture, bool showSpeech, bool playAudio) {
    if (!m_config.enabled) return;

    if (m_sleeping) {
        m_sleeping = false;
        m_idleDuration = 0.f;
        if (m_sleepZzz) m_sleepZzz->setVisible(false);
    }

    m_activeGesture = gesture;
    switch (gesture) {
        case PetGesture::Happy:     m_gestureDuration = 0.95f; break;
        case PetGesture::Spin:      m_gestureDuration = 0.85f; break;
        case PetGesture::Wave:      m_gestureDuration = 1.15f; break;
        case PetGesture::Surprised: m_gestureDuration = 0.75f; break;
        case PetGesture::Love:      m_gestureDuration = 1.10f; break;
        case PetGesture::Celebrate: m_gestureDuration = 1.45f; break;
        default:                    m_gestureDuration = 1.0f;  break;
    }
    m_gestureTimer = m_gestureDuration;
    m_iconState = PetIconState::React;
    updateIconState();

    // Expressive particle burst per gesture
    switch (gesture) {
        case PetGesture::Love:
            emitBurstParticles(PetParticleType::Hearts, {1.f, 0.35f, 0.6f, 1.f}, 8);
            break;
        case PetGesture::Celebrate:
            emitBurstParticles(PetParticleType::Stars, {1.f, 0.88f, 0.25f, 1.f}, 12);
            break;
        case PetGesture::Happy:
            emitBurstParticles(PetParticleType::Stars, {0.5f, 1.f, 0.65f, 1.f}, 6);
            break;
        case PetGesture::Spin:
            emitBurstParticles(PetParticleType::Sparkles, {0.45f, 0.85f, 1.f, 1.f}, 7);
            break;
        default:
            break;
    }

    bool playedOverride = false;
    if (showSpeech && m_config.enableSpeech) {
        auto cat = (gesture == PetGesture::Love) ? PetDialogueCategory::Petting : PetDialogueCategory::Gesture;
        showWeightedSpeech(cat, &playedOverride);
    }
    if (playAudio && !playedOverride) {
        playRandomFromPool(
            m_config.audioPoolForGesture(gesture),
            std::string("gesture:") + petGestureKey(gesture));
    }
}

void PetManager::updateGesture(float dt) {
    if (m_gestureTimer <= 0.f) return;
    m_gestureTimer -= dt;
    if (m_gestureTimer <= 0.f) {
        m_gestureTimer = 0.f;
        m_iconState = m_sleeping ? PetIconState::Sleep : (m_walking ? PetIconState::Walk : PetIconState::Idle);
        updateIconState();
    }
}

void PetManager::updatePetting(float dt, CCPoint const& mousePos) {
    if (!m_petSprite || m_pettingCooldown > 0.f) {
        m_lastPettingMousePos = mousePos;
        return;
    }

    auto petPos = m_petSprite->getPosition();
    float const hitRadius = std::max(24.f, m_petSprite->getContentSize().width * m_config.scale * 0.65f);
    float const dx = mousePos.x - petPos.x;
    float const dy = mousePos.y - petPos.y;
    bool const overPet = (dx * dx + dy * dy) <= hitRadius * hitRadius;

    if (overPet && m_lastPettingMousePos.x > -9000.f) {
        float const moveDist = std::hypot(mousePos.x - m_lastPettingMousePos.x,
                                          mousePos.y - m_lastPettingMousePos.y);
        // Count deliberate rubbing motion (not tiny jitter or huge jumps)
        if (moveDist > 2.f && moveDist < 120.f) {
            m_pettingRubAccum += moveDist;
        }
    } else {
        m_pettingRubAccum = std::max(0.f, m_pettingRubAccum - dt * 160.f);
    }
    m_lastPettingMousePos = mousePos;

    if (m_pettingRubAccum >= 190.f) {
        m_pettingRubAccum = 0.f;
        m_pettingCooldown = 2.6f;
        m_pettingSquishTimer = 0.25f;
        m_config.totalPets += 1;
        triggerGesture(PetGesture::Love, m_config.enableSpeech, true);
        addAffectionXp(3);
    }
}

void PetManager::registerClick(cocos2d::CCPoint clickPos) {
    if (!m_config.enabled || !m_config.enableClickInteraction) {
        return;
    }
    if (m_pendingClicks.size() >= 8) {
        m_pendingClicks.erase(m_pendingClicks.begin());
    }
    m_pendingClicks.push_back(clickPos);
}

void PetManager::updateReaction(float dt) {
    if (m_reactionTimer <= 0.f) return;

    m_reactionTimer -= dt;
    if (m_reactionTimer <= 0.f) {
        m_reactionTimer = 0.f;
        m_iconState = m_sleeping ? PetIconState::Sleep : (m_walking ? PetIconState::Walk : PetIconState::Idle);
        updateIconState();
    }
}


void PetManager::triggerClickReaction(cocos2d::CCPoint clickPos) {
    if (!m_petSprite || !m_config.enabled || !m_config.enableClickInteraction) return;
    if (m_clickCooldown > 0.f) return;

    auto const petPos = m_petSprite->getPosition();
    auto const sz = m_petSprite->getContentSize();
    float const halfW = std::max(24.f, sz.width * std::abs(m_config.scale) * 0.5f + 8.f);
    float const halfH = std::max(24.f, sz.height * std::abs(m_config.scale) * 0.5f + 8.f);
    float const dx = std::abs(clickPos.x - petPos.x);
    float const dy = std::abs(clickPos.y - petPos.y);
    if (dx > halfW || dy > halfH) return;

    if (m_sleeping) {
        m_sleeping = false;
        m_idleDuration = 0.f;
        if (m_sleepZzz) m_sleepZzz->setVisible(false);
    }

    m_clickCooldown = 0.35f;
    m_clickReactionTimer = m_config.clickReactionDuration;
    m_clickJumpOffset = 0.f;
    m_clickJumpVel = std::sqrt(2.f * 480.f * std::max(4.f, m_config.clickJumpHeight));
    m_config.totalClicks += 1;

    emitBurstParticles(PetParticleType::Sparkles, {1.f, 0.92f, 0.45f, 1.f}, 6);

    bool playedOverride = false;
    if (!m_config.clickMessages.empty()) {
        showWeightedSpeech(PetDialogueCategory::Click, &playedOverride, true);
    }
    if (!playedOverride && m_config.enableAudio) {
        playRandomFromPool(m_config.clickAudioPool, "click");
    }
    addAffectionXp(1);
}

void PetManager::updateClickReaction(float dt) {
    if (m_clickReactionTimer <= 0.f) return;

    m_clickReactionTimer -= dt;
    if (m_clickReactionTimer <= 0.f) {
        m_clickReactionTimer = 0.f;
    }
}
