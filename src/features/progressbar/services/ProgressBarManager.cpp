#include "ProgressBarManager.hpp"
#include "../../../utils/AnimatedGIFSprite.hpp"
#include "../../../utils/ImageLoadHelper.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/cocos.hpp>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <fstream>
#include <typeinfo>

using namespace geode::prelude;
using namespace cocos2d;

// ──────────────────────────────────────────────────────────────
// helpers
// ──────────────────────────────────────────────────────────────

namespace {
CCNode* findProgressBarNode(CCNode* root) {
    if (!root) return nullptr;
    // Fast path: geode.node-ids assigns ID "progress-bar"
    if (auto* direct = root->getChildByIDRecursive("progress-bar")) {
        return direct;
    }
    // Fallback: walk the tree looking for a node whose ID contains
    // "progress-bar" or which holds a child named "percentage-label".
    auto* children = root->getChildren();
    if (!children) return nullptr;
    for (auto* obj : CCArrayExt<CCNode*>(children)) {
        if (!obj) continue;
        std::string id = obj->getID();
        if (id.find("progress-bar") != std::string::npos) return obj;
        if (auto* found = findProgressBarNode(obj)) return found;
    }
    return nullptr;
}

int clampColor(int c) { return std::clamp(c, 0, 255); }

// Linear interpolation between two RGB colors (0..1 factor).
ccColor3B lerpColor(ccColor3B const& a, ccColor3B const& b, float t) {
    t = std::clamp(t, 0.f, 1.f);
    auto mix = [&](GLubyte x, GLubyte y) {
        return static_cast<GLubyte>(std::round(x + (y - x) * t));
    };
    return { mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b) };
}

// HSV → RGB conversion. h in [0,1), s,v in [0,1].
ccColor3B hsvToRgb(float h, float s, float v) {
    h = h - std::floor(h); // wrap to 0..1
    float i = std::floor(h * 6.f);
    float f = h * 6.f - i;
    float p = v * (1.f - s);
    float q = v * (1.f - f * s);
    float t = v * (1.f - (1.f - f) * s);
    float r, g, b;
    switch (static_cast<int>(i) % 6) {
        case 0: r=v; g=t; b=p; break;
        case 1: r=q; g=v; b=p; break;
        case 2: r=p; g=v; b=t; break;
        case 3: r=p; g=q; b=v; break;
        case 4: r=t; g=p; b=v; break;
        default:r=v; g=p; b=q; break;
    }
    return {
        static_cast<GLubyte>(std::round(r * 255)),
        static_cast<GLubyte>(std::round(g * 255)),
        static_cast<GLubyte>(std::round(b * 255)),
    };
}

// Compute the color for a slot given its mode and the shared animation
// clock. For Solid, returns c1 unchanged. For Pulse, sine-wave blends
// c1↔c2 at `speed` cycles/sec. For Rainbow, cycles HSV hue at speed.
ccColor3B resolveAnimatedColor(BarColorMode mode, ccColor3B const& c1,
                                ccColor3B const& c2, float animTime, float speed) {
    switch (mode) {
        case BarColorMode::Pulse: {
            // sine 0..1, one full cycle per 1/speed seconds.
            float t = 0.5f + 0.5f * std::sin(animTime * speed * 2.f * static_cast<float>(M_PI));
            return lerpColor(c1, c2, t);
        }
        case BarColorMode::Rainbow: {
            float hue = animTime * speed * 0.25f; // 0.25 → 1 rotation per 4s at speed=1
            return hsvToRgb(hue, 1.f, 1.f);
        }
        case BarColorMode::Solid:
        default:
            return c1;
    }
}
} // namespace

// ──────────────────────────────────────────────────────────────
// singleton + persistence
// ──────────────────────────────────────────────────────────────

ProgressBarManager& ProgressBarManager::get() {
    static ProgressBarManager inst;
    return inst;
}

std::filesystem::path ProgressBarManager::configPath() const {
    return Mod::get()->getSaveDir() / "progressbar_config.json";
}

void ProgressBarManager::loadConfig() {
    auto path = configPath();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return;

    auto rawRes = file::readString(path);
    if (!rawRes) return;

    auto res = matjson::parse(rawRes.unwrap());
    if (res.isErr()) return;
    auto j = res.unwrap();

    m_config.enabled          = j["enabled"].asBool().unwrapOr(false);
    m_config.vertical         = j["vertical"].asBool().unwrapOr(false);
    m_config.useCustomPosition= j["useCustomPosition"].asBool().unwrapOr(false);
    m_config.posX             = static_cast<float>(j["posX"].asDouble().unwrapOr(0.0));
    m_config.posY             = static_cast<float>(j["posY"].asDouble().unwrapOr(0.0));
    m_config.scaleLength      = static_cast<float>(j["scaleLength"].asDouble().unwrapOr(1.0));
    m_config.scaleThickness   = static_cast<float>(j["scaleThickness"].asDouble().unwrapOr(1.0));
    m_config.freeDragMode     = j["freeDragMode"].asBool().unwrapOr(false);
    m_config.opacity          = j["opacity"].asInt().unwrapOr(255);
    m_config.useCustomFillColor = j["useCustomFillColor"].asBool().unwrapOr(false);
    m_config.fillColor.r = clampColor(j["fillR"].asInt().unwrapOr(80));
    m_config.fillColor.g = clampColor(j["fillG"].asInt().unwrapOr(220));
    m_config.fillColor.b = clampColor(j["fillB"].asInt().unwrapOr(255));
    m_config.useCustomBgColor = j["useCustomBgColor"].asBool().unwrapOr(false);
    m_config.bgColor.r = clampColor(j["bgR"].asInt().unwrapOr(255));
    m_config.bgColor.g = clampColor(j["bgG"].asInt().unwrapOr(255));
    m_config.bgColor.b = clampColor(j["bgB"].asInt().unwrapOr(255));
    m_config.showPercentage    = j["showPercentage"].asBool().unwrapOr(true);
    m_config.percentageScale   = static_cast<float>(j["percentageScale"].asDouble().unwrapOr(1.0));
    m_config.percentageOffsetX = static_cast<float>(j["percentageOffsetX"].asDouble().unwrapOr(0.0));
    m_config.percentageOffsetY = static_cast<float>(j["percentageOffsetY"].asDouble().unwrapOr(0.0));
    m_config.useCustomPercentageColor = j["useCustomPercentageColor"].asBool().unwrapOr(false);
    m_config.percentageColor.r = clampColor(j["pctR"].asInt().unwrapOr(255));
    m_config.percentageColor.g = clampColor(j["pctG"].asInt().unwrapOr(255));
    m_config.percentageColor.b = clampColor(j["pctB"].asInt().unwrapOr(255));
    m_config.percentageFont = j["percentageFont"].asString().unwrapOr("");
    m_config.useCustomLabelPosition = j["useCustomLabelPosition"].asBool().unwrapOr(false);
    m_config.labelPosX = static_cast<float>(j["labelPosX"].asDouble().unwrapOr(0.0));
    m_config.labelPosY = static_cast<float>(j["labelPosY"].asDouble().unwrapOr(0.0));

    m_config.fillColorMode = static_cast<BarColorMode>(
        std::clamp(static_cast<int>(j["fillColorMode"].asInt().unwrapOr(0)), 0, 2));
    m_config.bgColorMode = static_cast<BarColorMode>(
        std::clamp(static_cast<int>(j["bgColorMode"].asInt().unwrapOr(0)), 0, 2));
    m_config.pctColorMode = static_cast<BarColorMode>(
        std::clamp(static_cast<int>(j["pctColorMode"].asInt().unwrapOr(0)), 0, 2));
    m_config.fillColor2.r = clampColor(j["fill2R"].asInt().unwrapOr(255));
    m_config.fillColor2.g = clampColor(j["fill2G"].asInt().unwrapOr( 64));
    m_config.fillColor2.b = clampColor(j["fill2B"].asInt().unwrapOr( 64));
    m_config.bgColor2.r   = clampColor(j["bg2R"].asInt().unwrapOr( 64));
    m_config.bgColor2.g   = clampColor(j["bg2G"].asInt().unwrapOr( 64));
    m_config.bgColor2.b   = clampColor(j["bg2B"].asInt().unwrapOr(255));
    m_config.pctColor2.r  = clampColor(j["pct2R"].asInt().unwrapOr(255));
    m_config.pctColor2.g  = clampColor(j["pct2G"].asInt().unwrapOr(255));
    m_config.pctColor2.b  = clampColor(j["pct2B"].asInt().unwrapOr( 64));
    m_config.colorAnimSpeed = static_cast<float>(
        j["colorAnimSpeed"].asDouble().unwrapOr(1.0));

    m_config.useFillTexture   = j["useFillTexture"].asBool().unwrapOr(false);
    m_config.useBgTexture     = j["useBgTexture"].asBool().unwrapOr(false);
    m_config.fillTexturePath  = j["fillTexturePath"].asString().unwrapOr("");
    m_config.bgTexturePath    = j["bgTexturePath"].asString().unwrapOr("");
    m_config.userRotation     = static_cast<float>(j["userRotation"].asDouble().unwrapOr(0.0));

    m_config.decorations.clear();
    auto decArrRes = j["decorations"].asArray();
    if (decArrRes.isOk()) {
        for (auto const& item : decArrRes.unwrap()) {
            BarDecoration d;
            d.path     = item["path"].asString().unwrapOr("");
            d.posX     = static_cast<float>(item["posX"].asDouble().unwrapOr(0.0));
            d.posY     = static_cast<float>(item["posY"].asDouble().unwrapOr(0.0));
            d.scale    = static_cast<float>(item["scale"].asDouble().unwrapOr(1.0));
            d.rotation = static_cast<float>(item["rotation"].asDouble().unwrapOr(0.0));
            if (!d.path.empty()) m_config.decorations.push_back(std::move(d));
        }
    }

    log::info("[ProgressBar] Config loaded");
}

void ProgressBarManager::saveConfig() {
    matjson::Value j;
    j["enabled"]            = m_config.enabled;
    j["vertical"]           = m_config.vertical;
    j["useCustomPosition"]  = m_config.useCustomPosition;
    j["posX"]               = m_config.posX;
    j["posY"]               = m_config.posY;
    j["scaleLength"]        = m_config.scaleLength;
    j["scaleThickness"]     = m_config.scaleThickness;
    j["freeDragMode"]       = m_config.freeDragMode;
    j["opacity"]            = m_config.opacity;
    j["useCustomFillColor"] = m_config.useCustomFillColor;
    j["fillR"]              = static_cast<int>(m_config.fillColor.r);
    j["fillG"]              = static_cast<int>(m_config.fillColor.g);
    j["fillB"]              = static_cast<int>(m_config.fillColor.b);
    j["useCustomBgColor"]   = m_config.useCustomBgColor;
    j["bgR"]                = static_cast<int>(m_config.bgColor.r);
    j["bgG"]                = static_cast<int>(m_config.bgColor.g);
    j["bgB"]                = static_cast<int>(m_config.bgColor.b);
    j["showPercentage"]     = m_config.showPercentage;
    j["percentageScale"]    = m_config.percentageScale;
    j["percentageOffsetX"]  = m_config.percentageOffsetX;
    j["percentageOffsetY"]  = m_config.percentageOffsetY;
    j["useCustomPercentageColor"] = m_config.useCustomPercentageColor;
    j["pctR"]               = static_cast<int>(m_config.percentageColor.r);
    j["pctG"]               = static_cast<int>(m_config.percentageColor.g);
    j["pctB"]               = static_cast<int>(m_config.percentageColor.b);
    j["percentageFont"]     = m_config.percentageFont;
    j["useCustomLabelPosition"] = m_config.useCustomLabelPosition;
    j["labelPosX"]          = m_config.labelPosX;
    j["labelPosY"]          = m_config.labelPosY;

    j["fillColorMode"]      = static_cast<int>(m_config.fillColorMode);
    j["bgColorMode"]        = static_cast<int>(m_config.bgColorMode);
    j["pctColorMode"]       = static_cast<int>(m_config.pctColorMode);
    j["fill2R"]             = static_cast<int>(m_config.fillColor2.r);
    j["fill2G"]             = static_cast<int>(m_config.fillColor2.g);
    j["fill2B"]             = static_cast<int>(m_config.fillColor2.b);
    j["bg2R"]               = static_cast<int>(m_config.bgColor2.r);
    j["bg2G"]               = static_cast<int>(m_config.bgColor2.g);
    j["bg2B"]               = static_cast<int>(m_config.bgColor2.b);
    j["pct2R"]              = static_cast<int>(m_config.pctColor2.r);
    j["pct2G"]              = static_cast<int>(m_config.pctColor2.g);
    j["pct2B"]              = static_cast<int>(m_config.pctColor2.b);
    j["colorAnimSpeed"]     = m_config.colorAnimSpeed;

    j["useFillTexture"]     = m_config.useFillTexture;
    j["useBgTexture"]       = m_config.useBgTexture;
    j["fillTexturePath"]    = m_config.fillTexturePath;
    j["bgTexturePath"]      = m_config.bgTexturePath;
    j["userRotation"]       = m_config.userRotation;

    matjson::Value decArr = matjson::Value::array();
    for (auto const& d : m_config.decorations) {
        matjson::Value item;
        item["path"]     = d.path;
        item["posX"]     = d.posX;
        item["posY"]     = d.posY;
        item["scale"]    = d.scale;
        item["rotation"] = d.rotation;
        decArr.push(item);
    }
    j["decorations"] = decArr;

    auto path = configPath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        log::error("[ProgressBar] Failed to write config");
        return;
    }
    auto txt = j.dump();
    out.write(txt.data(), static_cast<std::streamsize>(txt.size()));
}

void ProgressBarManager::resetToDefaults() {
    m_config = ProgressBarConfig{};
    // Preserve baseline + wasActive so the next tick's "was active, now
    // disabled" branch in applyToPlayLayer actually restores the vanilla
    // look. If we cleared these, nothing would repaint the bar.
    saveConfig();
}

// ──────────────────────────────────────────────────────────────
// baseline capture
// ──────────────────────────────────────────────────────────────

void ProgressBarManager::captureVanillaBaseline(CCNode* bar) {
    if (!bar || m_baselineCaptured) return;
    m_baselinePos     = bar->getPosition();
    m_baselineScaleX  = bar->getScaleX();
    m_baselineScaleY  = bar->getScaleY();
    m_baselineRotation= bar->getRotation();

    // percentage label lives in PlayLayer (sibling of UILayer) but the
    // bar itself is typically inside m_uiLayer. We keep no direct ref –
    // we re-lookup each frame by ID "percentage-label".

    m_baselineCaptured = true;
}

// ──────────────────────────────────────────────────────────────
// free-drag helpers
// ──────────────────────────────────────────────────────────────

bool ProgressBarManager::isFreeDragActive() const {
    return m_config.enabled && m_config.freeDragMode;
}

void ProgressBarManager::beginDrag(CCPoint startWorld) {
    m_dragging = true;
    // compute offset between current bar pos and touch pos so that the
    // bar doesn't snap its centre to the finger.
    m_dragOffset = ccp(m_config.posX - startWorld.x, m_config.posY - startWorld.y);
    if (!m_config.useCustomPosition) {
        // When first enabling drag we need a sensible anchor point.
        m_config.useCustomPosition = true;
    }
}

void ProgressBarManager::updateDrag(CCPoint currentWorld) {
    if (!m_dragging) return;
    m_config.posX = currentWorld.x + m_dragOffset.x;
    m_config.posY = currentWorld.y + m_dragOffset.y;
}

void ProgressBarManager::endDrag() {
    if (!m_dragging) return;
    m_dragging = false;
    saveConfig();
}

// ──────────────────────────────────────────────────────────────
// custom textures (static + GIF)
// ──────────────────────────────────────────────────────────────

void ProgressBarManager::captureSpriteBaseline(CCSprite* spr, TextureBaseline& tb) {
    if (!spr || tb.captured) return;
    tb.texture = spr->getTexture();
    tb.rect = spr->getTextureRect();
    tb.captured = true;
}

void ProgressBarManager::restoreSpriteBaseline(CCSprite* spr, TextureBaseline& tb) {
    if (!spr || !tb.captured) return;
    if (tb.texture) {
        spr->setTexture(tb.texture.data());
        spr->setTextureRect(tb.rect);
    }
    tb.captured = false;
    tb.texture = nullptr;
}

CCTexture2D* ProgressBarManager::resolveCustomTexture(
    CCNode* host, CustomTexture& slot, std::string const& path
) {
    if (path.empty()) {
        slot = {};
        return nullptr;
    }
    // Already loaded for this path? Use cache.
    if (slot.path == path) {
        if (slot.animHost) {
            // GIF: return current frame.
            auto* gif = typeinfo_cast<AnimatedGIFSprite*>(slot.animHost);
            if (gif) return gif->getTexture();
        }
        return slot.staticTex.data();
    }

    // Different path → drop old host.
    if (slot.animHost && slot.animHost->getParent()) {
        slot.animHost->removeFromParent();
    }
    slot.animHost = nullptr;
    slot.staticTex = nullptr;
    slot.path = path;

    std::filesystem::path fsPath(path);
    std::error_code ec;
    if (!std::filesystem::exists(fsPath, ec)) {
        log::warn("[ProgressBar] Custom texture not found: {}", path);
        return nullptr;
    }

    // GIF / APNG → AnimatedGIFSprite living as an invisible child of the
    // PlayLayer root, so its scheduled update keeps ticking.
    if (ImageLoadHelper::isAnimatedImage(fsPath)) {
        auto* anim = AnimatedGIFSprite::create(path);
        if (!anim) {
            log::warn("[ProgressBar] Failed to load GIF: {}", path);
            return nullptr;
        }
        anim->setVisible(false);
        anim->setPosition({-9999.f, -9999.f}); // off-screen safety
        if (host) host->addChild(anim, -1);
        slot.animHost = anim;  // PlayLayer owns it
        return anim->getTexture();
    }

    // Static image.
    auto img = ImageLoadHelper::loadStaticImage(fsPath, /*maxSizeMB*/ 16);
    if (!img.success || !img.texture) {
        log::warn("[ProgressBar] Failed to load image: {}", path);
        return nullptr;
    }
    slot.staticTex = img.texture;
    img.texture->release(); // Ref<> retained above
    return slot.staticTex.data();
}

// ──────────────────────────────────────────────────────────────
// decoration management
// ──────────────────────────────────────────────────────────────

int ProgressBarManager::addDecoration(BarDecoration const& d) {
    m_config.decorations.push_back(d);
    m_liveDecorations.push_back(nullptr);
    m_liveDecorationPaths.emplace_back();
    saveConfig();
    return static_cast<int>(m_config.decorations.size()) - 1;
}

void ProgressBarManager::removeDecoration(int index) {
    if (index < 0 || index >= static_cast<int>(m_config.decorations.size())) return;
    if (index < static_cast<int>(m_liveDecorations.size())) {
        auto* live = m_liveDecorations[index];
        if (live && live->getParent()) live->removeFromParent();
        m_liveDecorations.erase(m_liveDecorations.begin() + index);
    }
    if (index < static_cast<int>(m_liveDecorationPaths.size())) {
        m_liveDecorationPaths.erase(m_liveDecorationPaths.begin() + index);
    }
    m_config.decorations.erase(m_config.decorations.begin() + index);
    saveConfig();
}

CCNode* ProgressBarManager::getDecorationNode(int index) {
    if (index < 0 || index >= static_cast<int>(m_liveDecorations.size())) return nullptr;
    return m_liveDecorations[index];
}

namespace {
// Spawns a sprite (CCSprite or AnimatedGIFSprite) for a decoration path.
// Returns nullptr if the file can't be loaded. The caller parents it.
CCNode* createDecorationSprite(std::string const& path) {
    if (path.empty()) return nullptr;
    std::filesystem::path fsPath(path);
    std::error_code ec;
    if (!std::filesystem::exists(fsPath, ec)) return nullptr;
    if (ImageLoadHelper::isAnimatedImage(fsPath)) {
        return AnimatedGIFSprite::create(path);
    }
    auto img = ImageLoadHelper::loadStaticImage(fsPath, /*maxSizeMB*/ 24);
    if (!img.success || !img.texture) return nullptr;
    auto* spr = CCSprite::createWithTexture(img.texture);
    img.texture->release();
    return spr;
}
} // namespace

void ProgressBarManager::releaseCustomTextures() {
    if (m_fillCustom.animHost && m_fillCustom.animHost->getParent())
        m_fillCustom.animHost->removeFromParent();
    if (m_bgCustom.animHost && m_bgCustom.animHost->getParent())
        m_bgCustom.animHost->removeFromParent();
    m_fillCustom = {};
    m_bgCustom = {};
    m_fillBaselineTex = {};
    m_bgBaselineTex = {};

    // Also drop live decoration nodes; they'll respawn next tick.
    for (auto* d : m_liveDecorations) {
        if (d && d->getParent()) d->removeFromParent();
    }
    m_liveDecorations.clear();
    m_liveDecorationPaths.clear();
}

// ──────────────────────────────────────────────────────────────
// apply to live node
// ──────────────────────────────────────────────────────────────

void ProgressBarManager::applyToPlayLayer(CCNode* playLayerRoot) {
    if (!playLayerRoot) return;
    auto* bar = findProgressBarNode(playLayerRoot);
    if (!bar) return;

    auto* pct = playLayerRoot->getChildByIDRecursive("percentage-label");

    if (!m_config.enabled) {
        // If we were applying customizations and just got disabled, do a
        // one-shot restore. Otherwise leave the bar completely vanilla:
        // we never touch it, so GD's own logic is fully in control.
        if (m_wasActive && m_baselineCaptured) {
            bar->setPosition(m_baselinePos);
            bar->setScaleX(m_baselineScaleX);
            bar->setScaleY(m_baselineScaleY);
            bar->setRotation(m_baselineRotation);
            if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(bar)) {
                rgba->setOpacity(255);
                rgba->setColor(ccc3(255, 255, 255));
            }
            // Pre-identify the biggest sprite so we can restore textures
            // correctly on the bg vs. fill slots.
            CCSprite* biggestR = nullptr;
            float biggestAreaR = -1.f;
            std::vector<CCSprite*> allSprR;
            if (auto* children = bar->getChildren()) {
                for (auto* obj : CCArrayExt<CCNode*>(children)) {
                    auto* spr = typeinfo_cast<CCSprite*>(obj);
                    if (!spr) continue;
                    allSprR.push_back(spr);
                    auto sz = spr->getContentSize();
                    float area = sz.width * sz.height;
                    if (area > biggestAreaR) { biggestAreaR = area; biggestR = spr; }
                }
            }
            for (auto* spr : allSprR) {
                spr->setColor(ccc3(255, 255, 255));
                spr->setOpacity(255);
                if (spr == biggestR) restoreSpriteBaseline(spr, m_bgBaselineTex);
                else restoreSpriteBaseline(spr, m_fillBaselineTex);
            }
            releaseCustomTextures();
            if (pct) {
                pct->setVisible(true);
                if (m_labelBaselineCaptured) {
                    pct->setPosition(m_labelBaselinePos);
                    pct->setScale(m_labelBaselineScale);
                } else {
                    pct->setScale(1.f);
                }
                if (auto* label = typeinfo_cast<CCLabelBMFont*>(pct)) {
                    label->setColor(ccc3(255, 255, 255));
                }
                if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(pct)) {
                    rgba->setOpacity(255);
                }
            }
            m_wasActive = false;
            // Baseline is invalidated so next enable re-samples the
            // vanilla state (GD may have shifted things between levels).
            m_baselineCaptured = false;
            m_labelBaselineCaptured = false;
        }
        return;
    }

    // Sample vanilla baseline the first frame we become active (at which
    // point the bar is guaranteed to be in its vanilla state because we
    // haven't touched it yet).
    if (!m_baselineCaptured) {
        captureVanillaBaseline(bar);
    }
    if (!m_labelBaselineCaptured && pct) {
        m_labelBaselinePos = pct->getPosition();
        m_labelBaselineScale = pct->getScale();
        m_labelBaselineCaptured = true;
    }
    m_wasActive = true;

    // Advance animation clock using wall-clock delta so we keep going
    // at the correct speed even when framerate is uncapped / paused.
    {
        using clock = std::chrono::steady_clock;
        static clock::time_point s_lastTick = clock::now();
        auto now = clock::now();
        float dt = std::chrono::duration<float>(now - s_lastTick).count();
        s_lastTick = now;
        dt = std::clamp(dt, 0.f, 0.1f); // cap to avoid huge jumps
        m_animTime += dt;
    }

    // Position. posX/posY are stored in world-space coordinates so
    // sliders and the free-edit-mode drag agree on the same frame of
    // reference (winSize). Convert to parent-local before applying.
    if (m_config.useCustomPosition) {
        CCPoint world = ccp(m_config.posX, m_config.posY);
        CCPoint local = bar->getParent() ? bar->getParent()->convertToNodeSpace(world) : world;
        bar->setPosition(local);
    } else {
        bar->setPosition(m_baselinePos);
    }

    // Scale: scaleLength acts along the axis of the bar (X if
    // horizontal, Y if vertical). scaleThickness is perpendicular.
    float sx = m_baselineScaleX * m_config.scaleLength;
    float sy = m_baselineScaleY * m_config.scaleThickness;
    bar->setScaleX(sx);
    bar->setScaleY(sy);

    // Orientation + user free-form rotation.
    float rot = (m_config.vertical ? -90.f : m_baselineRotation)
              + m_config.userRotation;
    bar->setRotation(rot);

    // Opacity + colour for children
    int op = std::clamp(m_config.opacity, 0, 255);
    if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(bar)) {
        rgba->setOpacity(static_cast<GLubyte>(op));
        rgba->setCascadeOpacityEnabled(true);
    }

    // Walk children to tint/colour them. We consider the first sprite
    // (largest by content size) as the background/outline and the
    // remaining sprites as "fill" overlays.
    CCSprite* biggest = nullptr;
    float biggestArea = -1.f;
    std::vector<CCSprite*> allSprites;
    auto* children = bar->getChildren();
    if (children) {
        for (auto* obj : CCArrayExt<CCNode*>(children)) {
            auto* spr = typeinfo_cast<CCSprite*>(obj);
            if (!spr) continue;
            allSprites.push_back(spr);
            auto sz = spr->getContentSize();
            float area = sz.width * sz.height;
            if (area > biggestArea) {
                biggestArea = area;
                biggest = spr;
            }
        }
    }

    // Resolve animated colors once per frame (cheap).
    ccColor3B fillCol = m_config.useCustomFillColor
        ? resolveAnimatedColor(m_config.fillColorMode,
                               m_config.fillColor, m_config.fillColor2,
                               m_animTime, m_config.colorAnimSpeed)
        : ccc3(255, 255, 255);
    ccColor3B bgCol = m_config.useCustomBgColor
        ? resolveAnimatedColor(m_config.bgColorMode,
                               m_config.bgColor, m_config.bgColor2,
                               m_animTime, m_config.colorAnimSpeed)
        : ccc3(255, 255, 255);

    // Resolve custom textures (GIFs or statics) that might be needed.
    CCTexture2D* fillTex = nullptr;
    CCTexture2D* bgTex   = nullptr;
    if (m_config.useFillTexture)
        fillTex = resolveCustomTexture(playLayerRoot, m_fillCustom, m_config.fillTexturePath);
    if (m_config.useBgTexture)
        bgTex = resolveCustomTexture(playLayerRoot, m_bgCustom, m_config.bgTexturePath);

    // When the user has turned off a texture since last frame, restore
    // the vanilla texture of the corresponding sprite.
    if (!m_config.useFillTexture && m_fillBaselineTex.captured) {
        // biggest is bg, so fill is "other"
        for (auto* s : allSprites) {
            if (s != biggest) { restoreSpriteBaseline(s, m_fillBaselineTex); break; }
        }
        m_fillCustom = {};
    }
    if (!m_config.useBgTexture && m_bgBaselineTex.captured) {
        restoreSpriteBaseline(biggest, m_bgBaselineTex);
        m_bgCustom = {};
    }

    for (auto* spr : allSprites) {
        if (!spr) continue;
        spr->setOpacity(static_cast<GLubyte>(op));

        if (spr == biggest) {
            spr->setColor(bgCol);
            if (m_config.useBgTexture && bgTex) {
                captureSpriteBaseline(spr, m_bgBaselineTex);
                spr->setTexture(bgTex);
                spr->setTextureRect({0, 0, bgTex->getContentSize().width,
                                           bgTex->getContentSize().height});
            }
        } else {
            spr->setColor(fillCol);
            if (m_config.useFillTexture && fillTex) {
                captureSpriteBaseline(spr, m_fillBaselineTex);
                spr->setTexture(fillTex);
                spr->setTextureRect({0, 0, fillTex->getContentSize().width,
                                             fillTex->getContentSize().height});
            }
        }
    }

    // Percentage label lookup (child of PlayLayer / UILayer).
    if (pct) {
        pct->setVisible(m_config.showPercentage);
        if (m_config.showPercentage) {
            // Scale: apply baseline * user multiplier so default (1.0)
            // keeps the label exactly as GD sized it.
            float base = m_labelBaselineCaptured ? m_labelBaselineScale : 1.f;
            pct->setScale(std::max(0.05f, base * m_config.percentageScale));
            CCNode* pctParent = pct->getParent();

            if (m_config.useCustomLabelPosition) {
                // absolute world position chosen by the user
                CCPoint world = ccp(m_config.labelPosX, m_config.labelPosY);
                CCPoint local = pctParent ? pctParent->convertToNodeSpace(world) : world;
                pct->setPosition(local);
            } else if (m_config.useCustomPosition) {
                // Bar moved but label not: keep label at its vanilla
                // offset RELATIVE to the bar so it follows the bar.
                CCPoint barWorld = bar->getParent()
                    ? bar->getParent()->convertToWorldSpace(bar->getPosition())
                    : bar->getPosition();
                CCPoint baselineBarWorld = bar->getParent()
                    ? bar->getParent()->convertToWorldSpace(m_baselinePos)
                    : m_baselinePos;
                CCPoint baselineLabelWorld = pctParent && m_labelBaselineCaptured
                    ? pctParent->convertToWorldSpace(m_labelBaselinePos)
                    : CCPoint(0, 0);
                CCPoint offset = baselineLabelWorld - baselineBarWorld;
                CCPoint newWorld = barWorld + offset
                    + ccp(m_config.percentageOffsetX, m_config.percentageOffsetY);
                pct->setPosition(pctParent
                    ? pctParent->convertToNodeSpace(newWorld)
                    : newWorld);
            } else if (m_labelBaselineCaptured) {
                // No custom position anywhere: stay at vanilla, plus any
                // manual offset the user dialed in via sliders.
                pct->setPosition(m_labelBaselinePos
                    + ccp(m_config.percentageOffsetX, m_config.percentageOffsetY));
            }

            if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(pct)) {
                rgba->setOpacity(static_cast<GLubyte>(op));
            }
            if (auto* label = typeinfo_cast<CCLabelBMFont*>(pct)) {
                ccColor3B pctCol = m_config.useCustomPercentageColor
                    ? resolveAnimatedColor(m_config.pctColorMode,
                                           m_config.percentageColor,
                                           m_config.pctColor2,
                                           m_animTime, m_config.colorAnimSpeed)
                    : ccc3(255, 255, 255);
                label->setColor(pctCol);
                // Apply custom font if requested. setFntFile is cheap when
                // called with the same filename (it early-outs internally).
                if (!m_config.percentageFont.empty()) {
                    auto fntPath = CCFileUtils::sharedFileUtils()
                        ->fullPathForFilename(m_config.percentageFont.c_str(), false);
                    if (!fntPath.empty() && fntPath != m_config.percentageFont) {
                        label->setFntFile(m_config.percentageFont.c_str());
                    }
                }
            }
        }
    }

    // ── Decorations ─────────────────────────────────────────
    // Sync the live vector with config: spawn new sprites, drop
    // removed ones, and reposition the survivors.
    size_t N = m_config.decorations.size();
    while (m_liveDecorations.size() > N) {
        auto* back = m_liveDecorations.back();
        if (back && back->getParent()) back->removeFromParent();
        m_liveDecorations.pop_back();
    }
    while (m_liveDecorationPaths.size() > N) m_liveDecorationPaths.pop_back();
    m_liveDecorations.resize(N);
    m_liveDecorationPaths.resize(N);
    for (size_t i = 0; i < N; ++i) {
        auto const& cfg = m_config.decorations[i];
        auto* live = m_liveDecorations[i];
        // (Re)spawn if the path changed or the node died / was orphaned.
        bool needsSpawn = !live || !live->getParent()
                       || m_liveDecorationPaths[i] != cfg.path;
        if (needsSpawn) {
            if (live && live->getParent()) live->removeFromParent();
            live = createDecorationSprite(cfg.path);
            m_liveDecorations[i] = live;
            m_liveDecorationPaths[i] = cfg.path;
            if (live) {
                playLayerRoot->addChild(live, 9000);
            }
        }
        if (!live) continue;
        // Convert stored world-space position to the PlayLayer's local
        // space (PlayLayer is usually at (0,0) but let's be safe).
        CCPoint world = ccp(cfg.posX, cfg.posY);
        CCPoint local = playLayerRoot->convertToNodeSpace(world);
        live->setPosition(local);
        live->setScale(std::max(0.05f, cfg.scale));
        live->setRotation(cfg.rotation);
    }
}
