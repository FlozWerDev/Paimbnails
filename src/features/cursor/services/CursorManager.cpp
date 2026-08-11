#include "CursorManager.hpp"
#include <Geode/utils/string.hpp>
#include "../../../utils/ImageLoadHelper.hpp"
#include "../../../utils/LocalAssetStore.hpp"
#include "../../../utils/AnimatedGIFSprite.hpp"
#include "../../../utils/CursorIcoDecoder.hpp"
#include "../../../utils/GifEncoder.hpp"
#include "../../../utils/ImageConverter.hpp"
#include "../../../core/Settings.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
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

namespace {
geode::Ref<CCTexture2D>& fallbackCursorTexture() {
    static geode::Ref<CCTexture2D> s_tex = nullptr;
    return s_tex;
}

constexpr int kCursorBaseZOrder = INT_MAX;

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

paimon::cursorfx::TransitionFrame combineTransitionFrames(
    paimon::cursorfx::TransitionFrame const& base,
    paimon::cursorfx::TransitionFrame const& overlay) {
    return {
        ccp(base.offset.x + overlay.offset.x, base.offset.y + overlay.offset.y),
        base.scaleX * overlay.scaleX,
        base.scaleY * overlay.scaleY,
        base.rotation + overlay.rotation,
        base.skewX + overlay.skewX,
        base.skewY + overlay.skewY,
        base.opacity * overlay.opacity,
    };
}

paimon::cursorfx::TransitionFrame mixTransitionFrames(
    paimon::cursorfx::TransitionFrame const& from,
    paimon::cursorfx::TransitionFrame const& to, float t) {
    t = std::clamp(t, 0.f, 1.f);
    auto mix = [t](float a, float b) { return a + (b - a) * t; };
    return {
        ccp(mix(from.offset.x, to.offset.x), mix(from.offset.y, to.offset.y)),
        mix(from.scaleX, to.scaleX),
        mix(from.scaleY, to.scaleY),
        mix(from.rotation, to.rotation),
        mix(from.skewX, to.skewX),
        mix(from.skewY, to.skewY),
        mix(from.opacity, to.opacity),
    };
}

struct CursorContext {
    bool overButton   = false;
    bool overDisabled = false;
    bool overText     = false;
};

bool nodeContainsWorldPoint(CCNode* node, CCPoint const& worldPos) {
    auto* parent = node->getParent();
    CCPoint local = parent ? parent->convertToNodeSpace(worldPos) : worldPos;
    return node->boundingBox().containsPoint(local);
}

void scanCursorContext(CCNode* node, CCPoint const& worldPos, int depth,
                       CursorContext& ctx) {
    if (!node || !node->isVisible() || depth > 14) return;

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
}

CursorManager::~CursorManager() {
    detachClickOverlay();
    detachFromScene();
    paimon::cursorfx::CursorTrailNode::abandonSharedTextures();
    (void)fallbackCursorTexture().take();
}

void CursorManager::applyTrailPreset(int index) {
    if (index < 0 || index >= paimon::cursorfx::presetCount()) return;
    m_config.trailPreset = index;
    m_config.trail = paimon::cursorfx::presetAt(index).settings;
}

void CursorManager::applyClickPreset(int index) {
    if (index < 0 || index >= paimon::cursorfx::clickPresetCount()) return;
    auto burstTuning = m_config.click.burstTuning;
    auto holdTuning = m_config.click.holdTuning;
    m_config.clickPreset = index;
    m_config.click = paimon::cursorfx::clickPresetAt(index).settings;
    m_config.click.burstTuning = burstTuning;
    m_config.click.holdTuning = holdTuning;
}

void CursorManager::applyTransitionPreset(int index) {
    if (index < 0 || index >= paimon::cursorfx::transitionPresetCount()) return;
    m_config.transitionPreset = index;
    m_config.transition = paimon::cursorfx::transitionPresetAt(index).settings;
}

CursorManager& CursorManager::get() {
    static CursorManager inst;
    return inst;
}

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

    bool justEnabled = false;
    if (!filename.empty() && !m_config.enabled) {
        m_config.enabled = true;
        justEnabled = true;
    }

    saveConfig();

    if (justEnabled && !isAttached()) {
        attachToOverlay();
    } else {
        reloadSprites();
    }
}

void CursorManager::loadTrailConfig(matjson::Value const& j) {
    namespace fx = paimon::cursorfx;
    auto readInt = [&j](char const* key, int fallback, int lo, int hi) {
        return std::clamp(static_cast<int>(j[key].asInt().unwrapOr(fallback)), lo, hi);
    };

    if (!j.contains("trailEffect")) {
        static constexpr int kLegacyPresetMap[10] = {0, 7, 12, 1, 19, 10, 2, 21, 6, -1};
        int legacy = readInt("trailPreset", -1, -1, 9);
        if (legacy >= 0 && kLegacyPresetMap[legacy] >= 0) {
            applyTrailPreset(kLegacyPresetMap[legacy]);
            return;
        }

        m_config.trailPreset = -1;
        m_config.trail = fx::presetAt(0).settings;
        m_config.trail.colorMode = fx::TrailColorMode::Solid;
        m_config.trail.color1 = ccc3(
            static_cast<GLubyte>(readInt("trailR", 255, 0, 255)),
            static_cast<GLubyte>(readInt("trailG", 255, 0, 255)),
            static_cast<GLubyte>(readInt("trailB", 255, 0, 255)));
        m_config.trail.life = std::clamp(
            static_cast<float>(j["trailLength"].asDouble().unwrapOr(80.0)) / 60.f,
            fx::kLifeMin, fx::kLifeMax);
        m_config.trail.size = std::clamp(
            static_cast<float>(j["trailWidth"].asDouble().unwrapOr(4.0)),
            fx::kSizeMin, fx::kSizeMax);
        m_config.trail.opacity = readInt("trailOpacity", 200, 0, 255);
        return;
    }

    m_config.trailPreset = readInt("trailPreset", -1, -1, fx::presetCount() - 1);

    auto& t = m_config.trail;
    t.effect    = static_cast<fx::TrailEffect>(readInt("trailEffect", 0, 0, fx::kEffectCount - 1));
    t.colorMode = static_cast<fx::TrailColorMode>(readInt("trailColorMode", 0, 0, fx::kColorModeCount - 1));

    auto readColor = [&](char const* rk, char const* gk, char const* bk, ccColor3B fallback) {
        return ccc3(
            static_cast<GLubyte>(readInt(rk, fallback.r, 0, 255)),
            static_cast<GLubyte>(readInt(gk, fallback.g, 0, 255)),
            static_cast<GLubyte>(readInt(bk, fallback.b, 0, 255)));
    };
    t.color1 = readColor("trailR", "trailG", "trailB", ccc3(255, 255, 255));
    t.color2 = readColor("trailR2", "trailG2", "trailB2", ccc3(0, 190, 255));

    t.life     = std::clamp(static_cast<float>(j["trailLife"].asDouble().unwrapOr(0.55)),    fx::kLifeMin, fx::kLifeMax);
    t.size     = std::clamp(static_cast<float>(j["trailSize"].asDouble().unwrapOr(5.0)),     fx::kSizeMin, fx::kSizeMax);
    t.density  = std::clamp(static_cast<float>(j["trailDensity"].asDouble().unwrapOr(1.0)),  fx::kDensityMin, fx::kDensityMax);
    t.hueSpeed = std::clamp(static_cast<float>(j["trailHueSpeed"].asDouble().unwrapOr(1.0)), fx::kHueSpeedMin, fx::kHueSpeedMax);
    t.opacity  = readInt("trailOpacity", 205, 0, 255);
    t.glow     = j["trailGlow"].asBool().unwrapOr(true);
}

void CursorManager::loadClickConfig(matjson::Value const& j) {
    namespace fx = paimon::cursorfx;

    if (!j.contains("clickBurst")) {
        applyClickPreset(0);
        return;
    }

    auto readInt = [&j](char const* key, int fallback, int lo, int hi) {
        return std::clamp(static_cast<int>(j[key].asInt().unwrapOr(fallback)), lo, hi);
    };
    auto readFloat = [&j](char const* key, double fallback, float lo, float hi) {
        return std::clamp(static_cast<float>(j[key].asDouble().unwrapOr(fallback)), lo, hi);
    };

    m_config.clickPreset = readInt("clickPreset", -1, -1, fx::clickPresetCount() - 1);

    auto& c = m_config.click;
    c.press   = static_cast<fx::ClickBurst>(readInt("clickBurst", 1, 0, fx::kClickBurstCount - 1));
    c.release = static_cast<fx::ClickBurst>(readInt("clickReleaseBurst", 0, 0, fx::kClickBurstCount - 1));
    c.hold    = static_cast<fx::ClickHold>(readInt("clickHold", 0, 0, fx::kClickHoldCount - 1));
    c.anim    = static_cast<fx::ClickAnim>(readInt("clickAnim", 1, 0, fx::kClickAnimCount - 1));
    c.pressSound   = static_cast<fx::ClickSound>(readInt("clickSound", 0, 0, fx::kClickSoundCount - 1));
    c.releaseSound = static_cast<fx::ClickSound>(readInt("clickReleaseSound", 0, 0, fx::kClickSoundCount - 1));

    c.colorMode = static_cast<fx::TrailColorMode>(
        readInt("clickColorMode", 0, 0, fx::kColorModeCount - 1));
    c.color1 = ccc3(static_cast<GLubyte>(readInt("clickR", 255, 0, 255)),
                    static_cast<GLubyte>(readInt("clickG", 120, 0, 255)),
                    static_cast<GLubyte>(readInt("clickB", 170, 0, 255)));
    c.color2 = ccc3(static_cast<GLubyte>(readInt("clickR2", 110, 0, 255)),
                    static_cast<GLubyte>(readInt("clickG2", 200, 0, 255)),
                    static_cast<GLubyte>(readInt("clickB2", 255, 0, 255)));

    c.hueSpeed = readFloat("clickHueSpeed", 1.0, fx::kHueSpeedMin, fx::kHueSpeedMax);
    c.size     = readFloat("clickSize", 1.0, fx::kClickSizeMin, fx::kClickSizeMax);
    c.amount   = readFloat("clickAmount", 1.0, fx::kClickAmountMin, fx::kClickAmountMax);
    c.life     = readFloat("clickLife", 0.75, fx::kClickLifeMin, fx::kClickLifeMax);
    c.spread   = readFloat("clickSpread", 1.0, fx::kClickSpreadMin, fx::kClickSpreadMax);
    c.opacity  = readInt("clickOpacity", 235, 0, 255);
    c.glow     = j["clickGlow"].asBool().unwrapOr(true);

    c.animStrength = readFloat("clickAnimStrength", 1.0, fx::kClickAnimMin, fx::kClickAnimMax);
    c.animDuration = readFloat("clickAnimDuration", 0.18, fx::kClickAnimDurMin, fx::kClickAnimDurMax);

    c.volume      = readFloat("clickVolume", 0.55, 0.f, 1.f);
    c.pitch       = readFloat("clickPitch", 1.0, fx::kClickPitchMin, fx::kClickPitchMax);
    c.randomPitch = j["clickRandomPitch"].asBool().unwrapOr(false);
    c.rightClick  = j["clickRightButton"].asBool().unwrapOr(true);

    auto readTuning = [&j](char const* sizeKey, char const* speedKey, auto& slots) {
        auto apply = [&slots](char const* key, matjson::Value const& source, bool isSize) {
            auto arr = source[key].asArray();
            if (!arr.isOk()) return;
            auto const& values = arr.unwrap();
            size_t count = std::min(slots.size(), values.size());
            for (size_t i = 0; i < count; ++i) {
                float value = std::clamp(
                    static_cast<float>(values[i].asDouble().unwrapOr(1.0)),
                    fx::kClickTuneMin, fx::kClickTuneMax);
                if (isSize) slots[i].size = value;
                else        slots[i].speed = value;
            }
        };
        apply(sizeKey, j, true);
        apply(speedKey, j, false);
    };
    readTuning("clickBurstSizes", "clickBurstSpeeds", c.burstTuning);
    readTuning("clickHoldSizes", "clickHoldSpeeds", c.holdTuning);
}

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
    m_config.transitionEnabled = j["transitionEnabled"].asBool().unwrapOr(true);
    m_config.scale         = clampCursorScale(static_cast<float>(j["scale"].asDouble().unwrapOr(CURSOR_SCALE_DEFAULT)));
    m_config.opacity       = j["opacity"].asInt().unwrapOr(255);
    m_config.trailEnabled  = j["trailEnabled"].asBool().unwrapOr(false);
    loadTrailConfig(j);
    m_config.clickFxEnabled = j["clickFxEnabled"].asBool().unwrapOr(false);
    loadClickConfig(j);

    namespace fx = paimon::cursorfx;
    if (!j.contains("transitionEffect")) {
        applyTransitionPreset(0);
    } else {
        m_config.transitionPreset = std::clamp(
            static_cast<int>(j["transitionPreset"].asInt().unwrapOr(-1)),
            -1, fx::transitionPresetCount() - 1);
        m_config.transition.effect = static_cast<fx::TransitionEffect>(std::clamp(
            static_cast<int>(j["transitionEffect"].asInt().unwrapOr(3)),
            0, fx::kTransitionEffectCount - 1));
        m_config.transition.easing = static_cast<fx::TransitionEasing>(std::clamp(
            static_cast<int>(j["transitionEasing"].asInt().unwrapOr(1)),
            0, fx::kTransitionEasingCount - 1));
        m_config.transition.duration = std::clamp(
            static_cast<float>(j["transitionDuration"].asDouble().unwrapOr(0.16)),
            fx::kTransitionDurationMin, fx::kTransitionDurationMax);
        m_config.transition.intensity = std::clamp(
            static_cast<float>(j["transitionIntensity"].asDouble().unwrapOr(0.80)),
            fx::kTransitionIntensityMin, fx::kTransitionIntensityMax);
    }

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
    j["transitionEnabled"] = m_config.transitionEnabled;
    j["transitionPreset"] = m_config.transitionPreset;
    j["transitionEffect"] = static_cast<int>(m_config.transition.effect);
    j["transitionEasing"] = static_cast<int>(m_config.transition.easing);
    j["transitionDuration"] = static_cast<double>(m_config.transition.duration);
    j["transitionIntensity"] = static_cast<double>(m_config.transition.intensity);
    j["scale"]        = static_cast<double>(m_config.scale);
    j["opacity"]      = m_config.opacity;
    j["trailEnabled"]   = m_config.trailEnabled;
    j["trailPreset"]    = m_config.trailPreset;
    j["trailEffect"]    = static_cast<int>(m_config.trail.effect);
    j["trailColorMode"] = static_cast<int>(m_config.trail.colorMode);
    j["trailR"]         = static_cast<int>(m_config.trail.color1.r);
    j["trailG"]         = static_cast<int>(m_config.trail.color1.g);
    j["trailB"]         = static_cast<int>(m_config.trail.color1.b);
    j["trailR2"]        = static_cast<int>(m_config.trail.color2.r);
    j["trailG2"]        = static_cast<int>(m_config.trail.color2.g);
    j["trailB2"]        = static_cast<int>(m_config.trail.color2.b);
    j["trailLife"]      = static_cast<double>(m_config.trail.life);
    j["trailSize"]      = static_cast<double>(m_config.trail.size);
    j["trailDensity"]   = static_cast<double>(m_config.trail.density);
    j["trailOpacity"]   = m_config.trail.opacity;
    j["trailGlow"]      = m_config.trail.glow;
    j["trailHueSpeed"]  = static_cast<double>(m_config.trail.hueSpeed);

    auto const& click = m_config.click;
    j["clickFxEnabled"]      = m_config.clickFxEnabled;
    j["clickPreset"]         = m_config.clickPreset;
    j["clickBurst"]          = static_cast<int>(click.press);
    j["clickReleaseBurst"]   = static_cast<int>(click.release);
    j["clickHold"]           = static_cast<int>(click.hold);
    j["clickAnim"]           = static_cast<int>(click.anim);
    j["clickSound"]          = static_cast<int>(click.pressSound);
    j["clickReleaseSound"]   = static_cast<int>(click.releaseSound);
    j["clickColorMode"]      = static_cast<int>(click.colorMode);
    j["clickR"]              = static_cast<int>(click.color1.r);
    j["clickG"]              = static_cast<int>(click.color1.g);
    j["clickB"]              = static_cast<int>(click.color1.b);
    j["clickR2"]             = static_cast<int>(click.color2.r);
    j["clickG2"]             = static_cast<int>(click.color2.g);
    j["clickB2"]             = static_cast<int>(click.color2.b);
    j["clickHueSpeed"]       = static_cast<double>(click.hueSpeed);
    j["clickSize"]           = static_cast<double>(click.size);
    j["clickAmount"]         = static_cast<double>(click.amount);
    j["clickLife"]           = static_cast<double>(click.life);
    j["clickSpread"]         = static_cast<double>(click.spread);
    j["clickOpacity"]        = click.opacity;
    j["clickGlow"]           = click.glow;
    j["clickAnimStrength"]   = static_cast<double>(click.animStrength);
    j["clickAnimDuration"]   = static_cast<double>(click.animDuration);
    j["clickVolume"]         = static_cast<double>(click.volume);
    j["clickPitch"]          = static_cast<double>(click.pitch);
    j["clickRandomPitch"]    = click.randomPitch;
    j["clickRightButton"]    = click.rightClick;

    auto writeTuning = [&j](char const* sizeKey, char const* speedKey, auto const& slots) {
        matjson::Value sizes = matjson::Value::array();
        matjson::Value speeds = matjson::Value::array();
        for (auto const& slot : slots) {
            sizes.push(static_cast<double>(slot.size));
            speeds.push(static_cast<double>(slot.speed));
        }
        j[sizeKey] = sizes;
        j[speedKey] = speeds;
    };
    writeTuning("clickBurstSizes", "clickBurstSpeeds", click.burstTuning);
    writeTuning("clickHoldSizes", "clickHoldSpeeds", click.holdTuning);

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

    Mod::get()->setSettingValue<bool>("custom-cursor-enable", m_config.enabled);
}

bool CursorManager::shouldShowOnCurrentScene() const {
    auto scene = CCDirector::get()->getRunningScene();
    if (!scene) return false;

    if (auto* transition = typeinfo_cast<CCTransitionScene*>(scene)) {
        bool inOk  = transition->m_pInScene  && sceneMatchesVisibleLayers(transition->m_pInScene);
        bool outOk = transition->m_pOutScene && sceneMatchesVisibleLayers(transition->m_pOutScene);
        return inOk || outOk;
    }

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
}

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
        dir = galleryDir();
        prefix = "";
    } else {
        dir = packsDir() / packName;
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

namespace {
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
            if (!lastSpace && !out.empty()) {
                out.push_back('_');
                lastSpace = true;
            }
        }
    }
    while (!out.empty() && out.front() == '_') out.erase(out.begin());
    while (!out.empty() && out.back() == '_') out.pop_back();
    if (out.size() > 48) out.resize(48);
    return out;
}

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
}

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

    if (paimon::cursor_ico::isSupported(data.data(), data.size())) {
        auto decoded = paimon::cursor_ico::decode(data.data(), data.size());
        if (!decoded.success || decoded.frames.empty()) {
            log::warn("[CursorManager] Failed to decode cursor '{}': {}", displayName, decoded.error);
            return "";
        }
        log::debug("[CursorManager] decoded '{}': {} frame(s), animated={}",
            displayName, decoded.frames.size(), decoded.animated);

        if (decoded.animated && decoded.frames.size() > 1) {
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

    auto ext = geode::utils::string::toLower(
        geode::utils::string::pathToString(namePath.extension()));
    if (ext.empty() || !extensionLooksLikeImage(ext)) {
        using paimon::format::ImageFormat;
        switch (paimon::format::detect(data.data(), data.size())) {
            case ImageFormat::PNG:  ext = ".png";  break;
            case ImageFormat::JPEG: ext = ".jpg";  break;
            case ImageFormat::GIF:  ext = ".gif";  break;
            case ImageFormat::WebP: ext = ".webp"; break;
            case ImageFormat::BMP:  ext = ".bmp";  break;
            default: return "";
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

    if (ext == ".zip") {
        auto unzipRes = file::Unzip::create(srcPath);
        if (!unzipRes) {
            log::error("[CursorManager] Failed to open zip: {}", unzipRes.unwrapErr());
            m_lastImportError = "Couldn't open the .zip file.";
            return imported;
        }
        auto& unzip = unzipRes.unwrap();

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

    auto name = addToGallery(srcPath);
    if (!name.empty()) imported.push_back(name);
    else m_lastImportError = "Couldn't import that image.";
    return imported;
}

std::string CursorManager::createPack(std::string const& baseName) {
    auto name = uniquePackName(packsDir(), baseName);
    std::error_code ec;
    std::filesystem::create_directories(packsDir() / name, ec);
    if (ec) {
        log::error("[CursorManager] Failed to create pack '{}': {}", name, ec.message());
        return "";
    }
    return name;
}

std::string CursorManager::importData(std::vector<uint8_t> const& data,
                                      std::string const& displayName,
                                      std::string const& packName) {
    m_lastImportError.clear();
    if (data.empty()) {
        m_lastImportError = "La descarga llego vacia.";
        return "";
    }

    std::filesystem::path destDir = packName.empty() ? galleryDir() : packsDir() / packName;
    std::string relPrefix = packName.empty() ? "" : "packs/" + packName + "/";

    auto name = importSingleData(data, displayName, destDir, relPrefix);
    if (name.empty()) {
        m_lastImportError = "No se pudo decodificar ese cursor.";
    } else if (!packName.empty()) {
        m_lastImportedPack = packName;
    }
    return name;
}

std::vector<std::string> CursorManager::importZipData(std::vector<uint8_t> const& data,
                                                      std::string const& displayName) {
    m_lastImportError.clear();
    if (data.empty()) {
        m_lastImportError = "La descarga llego vacia.";
        return {};
    }

    // file::Unzip solo abre ficheros, asi que el .zip pasa por disco.
    auto tmpPath = Mod::get()->getSaveDir() / "cursor_shop_download.zip";
    auto writeRes = file::writeBinary(tmpPath, geode::ByteVector(data.begin(), data.end()));
    if (!writeRes) {
        log::error("[CursorManager] Failed to stage downloaded zip: {}", writeRes.unwrapErr());
        m_lastImportError = "No se pudo guardar el .zip descargado.";
        return {};
    }

    // El nombre del pack sale del stem del fichero, asi que conviene renombrarlo.
    auto stem = sanitizeAsciiStem(displayName);
    if (stem.empty()) stem = "pack";

    std::filesystem::path named = tmpPath;
    named.replace_filename(stem + ".zip");
    std::error_code renameEc;
    if (named != tmpPath) {
        std::filesystem::rename(tmpPath, named, renameEc);
        if (renameEc) named = tmpPath;
    }

    auto imported = importFromFile(named);

    std::error_code rmEc;
    std::filesystem::remove(named, rmEc);
    return imported;
}

void CursorManager::removeFromGallery(std::string const& filename) {
    auto path = galleryDir() / paimon::assets::pathFromUtf8(filename);
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
    for (auto& img : getImagesInPack("")) {
        auto path = galleryDir() / paimon::assets::pathFromUtf8(img);
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
    auto path = galleryDir() / paimon::assets::pathFromUtf8(filename);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) return nullptr;

    auto img = ImageLoadHelper::loadStaticImage(path);
    if (img.success && img.texture) return img.texture;

    if (ImageLoadHelper::isAnimatedImage(path)) {
        auto bin = ImageLoadHelper::readBinaryFile(path);
        if (!bin.empty() && GIFDecoder::isGIF(bin.data(), bin.size())) {
            auto gif = GIFDecoder::decode(bin.data(), bin.size(), 1);
            if (!gif.frames.empty()) {
                auto const& f = gif.frames.front();
                auto* tex = new CCTexture2D();
                if (tex->initWithData(f.pixels.data(), kCCTexture2DPixelFormat_RGBA8888,
                                      f.width, f.height, CCSize(f.width, f.height))) {
                    tex->setAntiAliasTexParameters();
                    return tex;
                }
                tex->release();
            }
        }
    }
    return nullptr;
}

void CursorManager::init() {
    log::info("[CursorManager] init");
    loadConfig();
    m_config.scale = clampCursorScale(m_config.scale);

    Mod::get()->setSettingValue<bool>("custom-cursor-enable", m_config.enabled);
}

CCSprite* CursorManager::loadSprite(std::string const& filename) {
    if (filename.empty()) return nullptr;
    auto path = galleryDir() / paimon::assets::pathFromUtf8(filename);
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

CCSprite* CursorManager::createPreviewSprite() {
    return createPreviewSprite(CursorState::Idle);
}

CCSprite* CursorManager::createPreviewSprite(CursorState state) {
    auto filename = imageForState(state);
    if (filename.empty() && state != CursorState::Idle) {
        filename = m_config.idleImage;
    }
    CCSprite* spr = loadSprite(filename);
    if (!spr) spr = createFallbackSprite();
    if (spr) applyCursorVisual(spr, m_config.scale, 255);
    return spr;
}

void CursorManager::reloadSprites() {
    for (auto& [state, sprite] : m_sprites) {
        if (sprite && m_cursorNode) sprite->removeFromParent();
    }
    m_sprites.clear();
    m_spriteBaseScales.clear();

    if (!m_config.enabled || !m_cursorNode) return;

    m_config.scale = clampCursorScale(m_config.scale);

    auto attachSprite = [this](CursorState state, CCSprite* sprite) {
        if (!sprite) return;
        applyCursorVisual(sprite, m_config.scale, m_config.opacity);
        sprite->setZOrder(10);
        sprite->setVisible(false);
        m_cursorNode->addChild(sprite);
        m_sprites[state] = sprite;
        m_spriteBaseScales[state] = ccp(sprite->getScaleX(), sprite->getScaleY());
    };

    CCSprite* idle = loadSprite(m_config.idleImage);
    if (!idle) idle = createFallbackSprite();
    attachSprite(CursorState::Idle, idle);

    attachSprite(CursorState::Move,     loadSprite(m_config.moveImage));
    attachSprite(CursorState::Hover,    loadSprite(m_config.hoverImage));
    attachSprite(CursorState::Click,    loadSprite(m_config.clickImage));
    attachSprite(CursorState::Text,     loadSprite(m_config.textImage));
    attachSprite(CursorState::Disabled, loadSprite(m_config.disabledImage));

    if (auto* base = spriteForState(CursorState::Idle)) {
        base->setVisible(true);
    }

    m_activeState = CursorState::Idle;
    m_previousState = CursorState::Idle;
    m_cachedState = CursorState::Idle;
    m_contextFrame = 0;
    m_stateTransitionTime = 0.f;
    m_stateTransitioning = false;
    m_stateTransitionInterrupted = false;
    m_transitionStartFrames.clear();
    m_renderedTransitionFrames.clear();

    updateTrail();
}

bool CursorManager::isCursorOverButton(CCPoint const& worldPos) const {
    auto* scene = CCDirector::get()->getRunningScene();
    if (!scene) return false;
    CursorContext ctx;
    scanCursorContext(scene, worldPos, 0, ctx);
    return ctx.overButton;
}

CursorState CursorManager::resolveActiveState(CCPoint const& mouseWorld) const {
    // Priority: Click > Disabled > Text > Hover > Move > Idle.
    if (m_config.clickEnabled && m_mouseDown && spriteForState(CursorState::Click)) {
        return CursorState::Click;
    }

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
    m_cursorNode->setVisible(false);
}

void CursorManager::setMouseDown(bool down) {
    m_mouseDown = down;
    refreshClickHold();
}

void CursorManager::setSecondaryMouseDown(bool down) {
    m_rightDown = down;
    refreshClickHold();
}

void CursorManager::refreshClickHold() {
    bool held = m_mouseDown || (m_config.click.rightClick && m_rightDown);
    if (held == m_fxHeld) return;
    m_fxHeld = held;
    if (m_clickQueue.size() < 8) {
        m_clickQueue.push_back(held ? uint8_t{1} : uint8_t{0});
    }
}

void CursorManager::detachFromScene() {
    if (m_cursorNode) {
        m_cursorNode->removeFromParent();
        m_cursorNode  = nullptr;
        m_sprites.clear();
        m_spriteBaseScales.clear();
        m_trail       = nullptr;
        m_stateTransitioning = false;
        m_stateTransitionInterrupted = false;
        m_transitionStartFrames.clear();
        m_renderedTransitionFrames.clear();
    }
    syncSystemCursorVisibility(false);
}

void CursorManager::renderOverlay() {
    if (m_clickFx && m_clickHost && m_clickHost->getParent() && m_clickHost->isVisible()) {
        m_clickFx->beginOverlayPass();
        m_clickHost->visit();
        m_clickFx->endOverlayPass();
    }

    if (!m_config.enabled) return;
    if (!m_cursorNode) return;
    if (!m_cursorNode->getParent()) return;
    if (!m_cursorNode->isVisible()) return;
    if (!hasLoadedCursorVisual()) return;

    // The host uses window coordinates and restores its own GL state.
    if (m_trail) m_trail->beginOverlayPass();
    m_cursorNode->visit();
    if (m_trail) m_trail->endOverlayPass();
}

void CursorManager::releaseSharedResources() {
    detachClickOverlay();
    detachFromScene();
    paimon::cursorfx::CursorTrailNode::abandonSharedTextures();
    (void)fallbackCursorTexture().take();
}

void CursorManager::onGLContextReload() {
    detachClickOverlay();
    detachFromScene();
    // Release while the old GL context is still active.
    paimon::cursorfx::CursorTrailNode::releaseSharedTextures();
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

void CursorManager::update(float dt) {
    m_sceneVisible = shouldShowOnCurrentScene();
    updateClickOverlay(dt);

    if (!m_config.enabled || !m_cursorNode) return;

    if (!m_cursorNode->getParent()) {
        detachFromScene();
        return;
    }

    CCPoint newPos;
    bool insideWindow = true;
    if (!sampleCursorPosition(newPos, insideWindow)) return;

    bool hideInGameplay = false;
    if (auto* pl = PlayLayer::get()) {
        static int s_hideFlagsCooldown = 0;
        static bool s_nativeHide = false;
        static bool s_modHide = false;
        if (s_hideFlagsCooldown-- <= 0) {
            s_hideFlagsCooldown = 30;
            s_nativeHide = !GameManager::get()->getGameVariable("0024"); // GameVar::ShowCursor
            s_modHide = paimon::settings::cursor::hideInGameplay();
        }
        bool nativeHide = s_nativeHide;
        bool modHide = s_modHide;

        bool inMenuOverlay = pl->m_isPaused
            || pl->getChildByType<RetryLevelLayer>(0) != nullptr
            || pl->getChildByType<EndLevelLayer>(0) != nullptr;

        hideInGameplay = (nativeHide || modHide) && !inMenuOverlay;
    }

    // cursor without destroying and rebuilding the trail — which is what
    // produced the per-frame flicker before.
    bool show = insideWindow && !hideInGameplay &&
                m_sceneVisible && hasLoadedCursorVisual();

    if (!show) {
        if (m_cursorNode->isVisible()) m_cursorNode->setVisible(false);
        syncSystemCursorVisibility(false);
        m_clickQueue.clear();
        return;
    }

    if (!m_cursorNode->isVisible()) {
        m_cursorNode->setVisible(true);
        m_currentPos = newPos;          // snap on reappear (no lerp dash)
        if (m_trail) m_trail->reset();  // clear stale points (no streak jump)
        if (m_clickFx) m_clickFx->reset();
        m_clickQueue.clear();
    }

    PlatformToolbox::hideCursor();
    m_systemCursorHidden = true;

    if (auto* parent = m_cursorNode->getParent()) {
        auto children = parent->getChildren();
        if (children && children->count() > 0) {
            auto* lastChild = static_cast<CCNode*>(children->lastObject());
            if (lastChild != m_cursorNode) {
                m_cursorNode->retain();
                m_cursorNode->removeFromParentAndCleanup(false);
                parent->addChild(m_cursorNode, INT_MAX);  // Re-add with max z-order
                m_cursorNode->release();
            }
        }
    }

    CCPoint prevPos = m_currentPos;
    m_targetPos     = newPos;

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

    if (++m_contextFrame % 2 == 0 || m_mouseDown) {
        m_cachedState = resolveActiveState(newPos);
    }
    CursorState active = m_cachedState;
    updateStateSprites(dt, active);

    if (m_trail) {
        m_trail->setEchoSource(spriteForState(active));
        m_trail->step(dt, m_currentPos);
    }
}

void CursorManager::applyConfigLive() {
    m_config.scale = clampCursorScale(m_config.scale);

    for (auto& [state, sprite] : m_sprites) {
        applyCursorVisual(sprite, m_config.scale, m_config.opacity);
        m_spriteBaseScales[state] = ccp(sprite->getScaleX(), sprite->getScaleY());
    }
    finishStateTransition();
    updateTrail();
    saveConfig();
}

void CursorManager::applyTrailLive() {
    updateTrail();
}

void CursorManager::applyClickLive() {
    m_clickModuleCooldown = 0;
    updateClickFx();
}

void CursorManager::processClickEvents(CCPoint const& at) {
    if (m_clickQueue.empty()) return;

    auto queue = std::move(m_clickQueue);
    m_clickQueue.clear();
    if (!m_clickFx) return;

    for (uint8_t event : queue) {
        bool pressed = event != 0;
        m_clickAnimTime = 0.f;
        m_clickAnimHeld = pressed;
        if (pressed) {
            m_clickFx->press(at);
            paimon::cursorfx::playClickSound(
                m_config.click.pressSound, m_config.click.volume,
                m_config.click.pitch, m_config.click.randomPitch);
        } else {
            m_clickFx->release(at);
            paimon::cursorfx::playClickSound(
                m_config.click.releaseSound, m_config.click.volume,
                m_config.click.pitch, m_config.click.randomPitch);
        }
    }
}

void CursorManager::applyTransitionLive() {
    m_config.transition.duration = std::clamp(
        m_config.transition.duration,
        paimon::cursorfx::kTransitionDurationMin,
        paimon::cursorfx::kTransitionDurationMax);
    m_config.transition.intensity = std::clamp(
        m_config.transition.intensity,
        paimon::cursorfx::kTransitionIntensityMin,
        paimon::cursorfx::kTransitionIntensityMax);
    finishStateTransition();
}

void CursorManager::finishStateTransition() {
    m_stateTransitioning = false;
    m_stateTransitionInterrupted = false;
    m_stateTransitionTime = 0.f;
    m_transitionStartFrames.clear();
    m_renderedTransitionFrames.clear();
    for (auto state : kAllStates) {
        auto* sprite = spriteForState(state);
        if (!sprite) continue;
        auto scale = m_spriteBaseScales.contains(state)
            ? m_spriteBaseScales.at(state) : ccp(sprite->getScaleX(), sprite->getScaleY());
        sprite->setVisible(state == m_activeState);
        paimon::cursorfx::applyTransitionFrame(
            sprite, m_currentPos, scale, m_config.opacity, m_clickFrame);
    }
    m_renderedTransitionFrames[m_activeState] = {};
}

void CursorManager::updateStateSprites(float dt, CursorState active) {
    namespace fx = paimon::cursorfx;
    if (m_transitionModuleCooldown-- <= 0) {
        m_transitionModuleCooldown = 30;
        m_transitionModuleOn =
            paimon::modules::isEnabled("paimbnails.cursortransition.global");
    }
    bool transitionsOn = m_transitionModuleOn;

    if (active != m_activeState) {
        m_stateTransitionInterrupted = m_stateTransitioning;
        m_transitionStartFrames.clear();
        if (m_stateTransitionInterrupted) {
            m_transitionStartFrames = m_renderedTransitionFrames;
        } else {
            m_transitionStartFrames[m_activeState] = {};
        }
        m_previousState = m_activeState;
        m_activeState = active;
        m_stateTransitionTime = 0.f;
        m_stateTransitioning = transitionsOn &&
            m_config.transition.effect != fx::TransitionEffect::Instant &&
            spriteForState(m_previousState) && spriteForState(m_activeState);
    }

    if (!transitionsOn || !m_stateTransitioning) {
        finishStateTransition();
        return;
    }

    float duration = std::clamp(
        m_config.transition.duration,
        fx::kTransitionDurationMin,
        fx::kTransitionDurationMax);
    m_stateTransitionTime += std::max(dt, 0.f);
    float progress = std::min(m_stateTransitionTime / duration, 1.f);

    for (auto state : kAllStates) {
        if (auto* sprite = spriteForState(state)) sprite->setVisible(false);
    }

    m_renderedTransitionFrames.clear();

    auto apply = [&](CursorState state, fx::TransitionFrame const& frame) {
        auto* sprite = spriteForState(state);
        if (!sprite) return;
        auto scale = m_spriteBaseScales.contains(state)
            ? m_spriteBaseScales.at(state) : ccp(sprite->getScaleX(), sprite->getScaleY());
        sprite->setVisible(true);
        fx::applyTransitionFrame(
            sprite, m_currentPos, scale, m_config.opacity,
            combineTransitionFrames(frame, m_clickFrame));
        m_renderedTransitionFrames[state] = frame;
    };

    for (auto const& [state, start] : m_transitionStartFrames) {
        if (state == m_activeState) continue;
        auto frame = fx::sampleTransition(m_config.transition, progress, false);
        if (m_stateTransitionInterrupted) {
            frame = mixTransitionFrames(start, frame, progress);
        }
        apply(state, frame);
    }

    auto incoming = fx::sampleTransition(m_config.transition, progress, true);
    if (m_stateTransitionInterrupted) {
        if (auto it = m_transitionStartFrames.find(m_activeState);
            it != m_transitionStartFrames.end()) {
            incoming = mixTransitionFrames(it->second, incoming, progress);
        }
    }
    apply(m_activeState, incoming);

    if (progress >= 1.f) finishStateTransition();
}

void CursorManager::updateTrail() {
    bool wanted = m_config.trailEnabled && m_cursorNode && hasLoadedCursorVisual();

    if (!wanted) {
        if (m_trail) {
            m_trail->removeFromParent();
            m_trail = nullptr;
        }
        return;
    }

    if (!m_trail) {
        m_trail = paimon::cursorfx::CursorTrailNode::create();
        if (!m_trail) {
            log::warn("[CursorManager] no se pudo crear el nodo de estela");
            return;
        }
        m_trail->setZOrder(5);
        m_cursorNode->addChild(m_trail);
        m_trail->reset();
    }

    m_trail->applySettings(m_config.trail);
}

void CursorManager::detachClickOverlay() {
    if (m_clickHost) {
        m_clickHost->removeFromParent();
        m_clickHost = nullptr;
    }
    m_clickFx = nullptr;
    m_clickQueue.clear();
    m_clickFrame = {};
    m_clickAnimTime = 999.f;
    m_clickAnimHeld = false;
}

void CursorManager::updateClickFx() {
    if (m_clickModuleCooldown-- <= 0) {
        m_clickModuleCooldown = 30;
        m_clickModuleOn = paimon::modules::isEnabled("paimbnails.cursorclick.global");
    }

    if (!m_config.clickFxEnabled || !m_clickModuleOn) {
        if (m_clickHost) detachClickOverlay();
        return;
    }

    if (!m_clickHost) {
        auto* overlay = OverlayManager::get();
        if (!overlay) return;

        m_clickHost = CCNode::create();
        m_clickHost->setID("paimon-cursor-click-host"_spr);
        m_clickHost->setZOrder(kCursorBaseZOrder - 1);
        overlay->addChild(m_clickHost);

        m_clickFx = paimon::cursorfx::CursorClickNode::create();
        if (!m_clickFx) {
            log::warn("[CursorManager] no se pudo crear el nodo de efectos de click");
            detachClickOverlay();
            return;
        }
        m_clickHost->addChild(m_clickFx);
        m_clickFx->reset();
        m_clickHost->setVisible(false);
        m_clickQueue.clear();
    }

    if (!m_clickHost->getParent()) {
        detachClickOverlay();
        return;
    }

    if (m_clickFx) m_clickFx->applySettings(m_config.click);
}

CCPoint CursorManager::pointerPos() const {
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    return m_touchPoint;
#else
    auto winSize = CCDirector::get()->getWinSize();
    auto mouse = geode::cocos::getMousePos();
    return ccp(std::clamp(mouse.x, 0.f, winSize.width),
               std::clamp(mouse.y, 0.f, winSize.height));
#endif
}

void CursorManager::updateClickOverlay(float dt) {
    updateClickFx();
    if (!m_clickFx || !m_clickHost) {
        m_clickFrame = {};
        return;
    }

    bool inGameplay = false;
    if (auto* pl = PlayLayer::get()) {
        inGameplay = !(pl->m_isPaused
            || pl->getChildByType<RetryLevelLayer>(0) != nullptr
            || pl->getChildByType<EndLevelLayer>(0) != nullptr);
    }

    if (inGameplay || !m_sceneVisible) {
        if (m_clickHost->isVisible()) {
            m_clickHost->setVisible(false);
            m_clickFx->reset();
        }
        m_clickQueue.clear();
        m_clickFrame = {};
        return;
    }

    if (!m_clickHost->isVisible()) {
        m_clickHost->setVisible(true);
        m_clickFx->reset();
        m_clickQueue.clear();
    }

    if (auto* parent = m_clickHost->getParent()) {
        auto* children = parent->getChildren();
        if (children && children->count() > 0 &&
            static_cast<CCNode*>(children->lastObject()) != m_clickHost.data()) {
            m_clickHost->retain();
            m_clickHost->removeFromParentAndCleanup(false);
            parent->addChild(m_clickHost, kCursorBaseZOrder - 1);
            m_clickHost->release();
        }
    }

    CCPoint at = pointerPos();
    processClickEvents(at);
    m_clickAnimTime += dt;
    m_clickFrame = paimon::cursorfx::sampleClickAnim(
        m_config.click.anim, m_clickAnimTime, m_config.click.animDuration,
        m_config.click.animStrength, m_clickAnimHeld);
    m_clickFx->step(dt, at, m_fxHeld);
}
