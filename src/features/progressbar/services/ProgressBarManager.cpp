#include "ProgressBarManager.hpp"
#include "../../../utils/AnimatedGIFSprite.hpp"
#include "../../../utils/ImageLoadHelper.hpp"
#include "../../../utils/LocalAssetStore.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/cocos.hpp>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <fstream>

using namespace geode::prelude;
using namespace cocos2d;

// ──────────────────────────────────────────────────────────────
// Local helpers
// ──────────────────────────────────────────────────────────────

namespace {

CCNode* findChildByIDDeep(CCNode* root, char const* id) {
    if (!root || !id) return nullptr;
    if (auto* direct = root->getChildByIDRecursive(id)) return direct;
    return nullptr;
}

// Fallback walker: finds the bar by ID substring or by sibling test.
// Used when geode.node-ids isn't loaded for some reason.
CCNode* findProgressBarFallback(CCNode* root) {
    if (!root) return nullptr;
    auto* children = root->getChildren();
    if (!children) return nullptr;
    for (auto* obj : CCArrayExt<CCNode*>(children)) {
        if (!obj) continue;
        std::string id = obj->getID();
        if (id.find("progress-bar") != std::string::npos) return obj;
        if (auto* found = findProgressBarFallback(obj)) return found;
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
    float r = 0.f, g = 0.f, b = 0.f;
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

// Splits the bar's child sprites into background (the largest by area)
// and "fill" sprites (everything else). The bar typically has 2-3 child
// sprites: an outline and one or two fill quads.
struct SpriteSlots {
    CCSprite* bg = nullptr;
    std::vector<CCSprite*> all;
};
SpriteSlots collectBarSprites(CCNode* bar) {
    SpriteSlots out;
    if (!bar) return out;
    auto* children = bar->getChildren();
    if (!children) return out;
    float bgArea = -1.f;
    for (auto* obj : CCArrayExt<CCNode*>(children)) {
        auto* spr = typeinfo_cast<CCSprite*>(obj);
        if (!spr) continue;
        out.all.push_back(spr);
        auto sz = spr->getContentSize();
        float area = sz.width * sz.height;
        if (area > bgArea) {
            bgArea = area;
            out.bg = spr;
        }
    }
    return out;
}

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

// ──────────────────────────────────────────────────────────────
// Singleton + persistence
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
    if (!rawRes) {
        log::warn("[ProgressBar] Failed to read config: {}", rawRes.unwrapErr());
        return;
    }

    auto res = matjson::parse(rawRes.unwrap());
    if (res.isErr()) {
        log::warn("[ProgressBar] Config JSON parse failed: {}", res.unwrapErr());
        return;
    }
    auto j = res.unwrap();

    auto getBool = [&](char const* k, bool d) { return j[k].asBool().unwrapOr(d); };
    auto getInt  = [&](char const* k, int d)  { return j[k].asInt().unwrapOr(d); };
    auto getDbl  = [&](char const* k, double d){ return j[k].asDouble().unwrapOr(d); };
    auto getStr  = [&](char const* k, std::string const& d) { return j[k].asString().unwrapOr(d); };

    m_config.enabled            = getBool("enabled", false);
    m_config.vertical           = getBool("vertical", false);
    m_config.useCustomPosition  = getBool("useCustomPosition", false);
    m_config.posX               = static_cast<float>(getDbl("posX", 0.0));
    m_config.posY               = static_cast<float>(getDbl("posY", 0.0));
    m_config.scaleLength        = static_cast<float>(getDbl("scaleLength", 1.0));
    m_config.scaleThickness     = static_cast<float>(getDbl("scaleThickness", 1.0));
    m_config.freeDragMode       = getBool("freeDragMode", false);
    m_config.opacity            = getInt("opacity", 255);
    m_config.useCustomFillColor = getBool("useCustomFillColor", false);
    m_config.fillColor.r        = clampColor(getInt("fillR", 80));
    m_config.fillColor.g        = clampColor(getInt("fillG", 220));
    m_config.fillColor.b        = clampColor(getInt("fillB", 255));
    m_config.useCustomBgColor   = getBool("useCustomBgColor", false);
    m_config.bgColor.r          = clampColor(getInt("bgR", 255));
    m_config.bgColor.g          = clampColor(getInt("bgG", 255));
    m_config.bgColor.b          = clampColor(getInt("bgB", 255));
    m_config.showPercentage     = getBool("showPercentage", true);
    m_config.percentageScale    = static_cast<float>(getDbl("percentageScale", 1.0));
    m_config.percentageOffsetX  = static_cast<float>(getDbl("percentageOffsetX", 0.0));
    m_config.percentageOffsetY  = static_cast<float>(getDbl("percentageOffsetY", 0.0));
    m_config.useCustomPercentageColor = getBool("useCustomPercentageColor", false);
    m_config.percentageColor.r  = clampColor(getInt("pctR", 255));
    m_config.percentageColor.g  = clampColor(getInt("pctG", 255));
    m_config.percentageColor.b  = clampColor(getInt("pctB", 255));
    m_config.percentageFont     = getStr("percentageFont", "");
    m_config.useCustomLabelPosition = getBool("useCustomLabelPosition", false);
    m_config.labelPosX          = static_cast<float>(getDbl("labelPosX", 0.0));
    m_config.labelPosY          = static_cast<float>(getDbl("labelPosY", 0.0));

    auto modeOf = [&](char const* k) {
        int v = static_cast<int>(getInt(k, 0));
        return static_cast<BarColorMode>(std::clamp(v, 0, 2));
    };
    m_config.fillColorMode = modeOf("fillColorMode");
    m_config.bgColorMode   = modeOf("bgColorMode");
    m_config.pctColorMode  = modeOf("pctColorMode");

    m_config.fillColor2 = { (GLubyte)clampColor(getInt("fill2R", 255)),
                            (GLubyte)clampColor(getInt("fill2G",  64)),
                            (GLubyte)clampColor(getInt("fill2B",  64)) };
    m_config.bgColor2   = { (GLubyte)clampColor(getInt("bg2R",  64)),
                            (GLubyte)clampColor(getInt("bg2G",  64)),
                            (GLubyte)clampColor(getInt("bg2B", 255)) };
    m_config.pctColor2  = { (GLubyte)clampColor(getInt("pct2R", 255)),
                            (GLubyte)clampColor(getInt("pct2G", 255)),
                            (GLubyte)clampColor(getInt("pct2B",  64)) };
    m_config.colorAnimSpeed = static_cast<float>(getDbl("colorAnimSpeed", 1.0));

    m_config.useFillTexture  = getBool("useFillTexture", false);
    m_config.useBgTexture    = getBool("useBgTexture", false);
    m_config.fillTexturePath = getStr("fillTexturePath", "");
    m_config.bgTexturePath   = getStr("bgTexturePath", "");
    m_config.userRotation    = static_cast<float>(getDbl("userRotation", 0.0));

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

    // Re-import any stored asset paths that point to legacy locations.
    bool migrated = false;
    auto migratePath = [&](std::string& path, std::string const& bucket) {
        if (path.empty()) return;
        auto imported = paimon::assets::importStoredPath(path, bucket, paimon::assets::Kind::Image);
        if (imported.success && !imported.path.empty()) {
            auto normalized = paimon::assets::normalizePathString(imported.path);
            if (normalized != path) {
                path = normalized;
                migrated = true;
            }
        }
    };
    migratePath(m_config.fillTexturePath, "progressbar_fill");
    migratePath(m_config.bgTexturePath,   "progressbar_bg");
    for (auto& deco : m_config.decorations) {
        migratePath(deco.path, "progressbar_decorations");
    }

    sanitizeConfig();
    if (migrated) saveConfig();

    log::info("[ProgressBar] Config loaded ({} decorations)", m_config.decorations.size());
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
        log::error("[ProgressBar] Failed to write config to {}",
                   geode::utils::string::pathToString(path));
        return;
    }
    auto txt = j.dump();
    out.write(txt.data(), static_cast<std::streamsize>(txt.size()));
}

void ProgressBarManager::resetToDefaults() {
    m_config = ProgressBarConfig{};
    // Keep baseline + wasActive so the next tick's "was active, now
    // disabled" branch in applyToPlayLayer actually restores the
    // vanilla look. Clearing them would leave the bar in its tweaked
    // visual state until the next scene change.
    saveConfig();
}

void ProgressBarManager::sanitizeConfig() {
    auto& c = m_config;
    c.opacity = std::clamp(c.opacity, 0, 255);
    c.scaleLength    = std::clamp(c.scaleLength,    0.05f, 5.f);
    c.scaleThickness = std::clamp(c.scaleThickness, 0.05f, 5.f);
    c.percentageScale = std::clamp(c.percentageScale, 0.1f, 5.f);
    c.colorAnimSpeed  = std::clamp(c.colorAnimSpeed, 0.1f, 5.f);
    auto modeClamp = [](BarColorMode m) {
        int v = std::clamp(static_cast<int>(m), 0, 2);
        return static_cast<BarColorMode>(v);
    };
    c.fillColorMode = modeClamp(c.fillColorMode);
    c.bgColorMode   = modeClamp(c.bgColorMode);
    c.pctColorMode  = modeClamp(c.pctColorMode);

    // Drop empty-path decorations (defensive).
    c.decorations.erase(
        std::remove_if(c.decorations.begin(), c.decorations.end(),
            [](BarDecoration const& d) { return d.path.empty(); }),
        c.decorations.end());
    for (auto& d : c.decorations) {
        d.scale = std::clamp(d.scale, 0.05f, 8.f);
    }
}

// ──────────────────────────────────────────────────────────────
// Free-drag helpers
// ──────────────────────────────────────────────────────────────

bool ProgressBarManager::isFreeDragActive() const {
    return m_config.enabled && m_config.freeDragMode;
}

void ProgressBarManager::beginDrag(CCPoint startWorld) {
    m_dragging = true;
    m_dragOffset = ccp(m_config.posX - startWorld.x, m_config.posY - startWorld.y);
    if (!m_config.useCustomPosition) {
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
// Custom textures (static + GIF)
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
        if (slot.animHost && slot.animHost->getParent())
            slot.animHost->removeFromParent();
        slot = {};
        return nullptr;
    }
    // Already loaded for this path? Use cache.
    if (slot.path == path) {
        slot.justChanged = false;
        if (slot.animHost) {
            // GIF: return current frame.
            if (auto* gif = typeinfo_cast<AnimatedGIFSprite*>(slot.animHost))
                return gif->getTexture();
            return nullptr;
        }
        return slot.staticTex.data();
    }

    // Different path → drop old host.
    if (slot.animHost && slot.animHost->getParent())
        slot.animHost->removeFromParent();
    slot.animHost = nullptr;
    slot.staticTex = nullptr;
    slot.path = path;
    slot.justChanged = true;

    std::filesystem::path fsPath(path);
    std::error_code ec;
    if (!std::filesystem::exists(fsPath, ec)) {
        log::warn("[ProgressBar] Custom texture not found: {}", path);
        return nullptr;
    }

    // GIF / APNG → AnimatedGIFSprite living as an invisible child of
    // the PlayLayer root, so its scheduled update keeps ticking.
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
// Decoration management
// ──────────────────────────────────────────────────────────────

int ProgressBarManager::addDecoration(BarDecoration const& d) {
    if (d.path.empty()) return -1;
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

    // Note: the edit overlay validates its own indices each frame in
    // its update() loop, so no event is needed here.
}

CCNode* ProgressBarManager::getDecorationNode(int index) {
    if (index < 0 || index >= static_cast<int>(m_liveDecorations.size())) return nullptr;
    return m_liveDecorations[index];
}

// ──────────────────────────────────────────────────────────────
// Apply: top-level + helpers
// ──────────────────────────────────────────────────────────────

CCNode* ProgressBarManager::findBarNode(CCNode* root) {
    if (!root) return nullptr;
    if (auto* found = findChildByIDDeep(root, "progress-bar")) return found;
    return findProgressBarFallback(root);
}
CCNode* ProgressBarManager::findLabelNode(CCNode* root) {
    return findChildByIDDeep(root, "percentage-label");
}

void ProgressBarManager::captureBaselineIfNeeded(CCNode* bar, CCNode* label) {
    if (!m_baselineCaptured && bar) {
        m_baselinePos      = bar->getPosition();
        m_baselineScaleX   = bar->getScaleX();
        m_baselineScaleY   = bar->getScaleY();
        m_baselineRotation = bar->getRotation();
        m_baselineCaptured = true;
    }
    if (!m_labelBaselineCaptured && label) {
        m_labelBaselinePos = label->getPosition();
        m_labelBaselineScale = label->getScale();
        m_labelBaselineCaptured = true;
    }
}

void ProgressBarManager::restoreVanillaState(CCNode* bar, CCNode* label) {
    if (bar && m_baselineCaptured) {
        bar->setPosition(m_baselinePos);
        bar->setScaleX(m_baselineScaleX);
        bar->setScaleY(m_baselineScaleY);
        bar->setRotation(m_baselineRotation);
        if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(bar)) {
            rgba->setOpacity(255);
            rgba->setColor(ccc3(255, 255, 255));
        }
        auto slots = collectBarSprites(bar);
        for (auto* spr : slots.all) {
            if (!spr) continue;
            spr->setColor(ccc3(255, 255, 255));
            spr->setOpacity(255);
            if (spr == slots.bg) restoreSpriteBaseline(spr, m_bgBaselineTex);
            else                  restoreSpriteBaseline(spr, m_fillBaselineTex);
        }
    }
    releaseCustomTextures();
    if (label) {
        label->setVisible(true);
        if (m_labelBaselineCaptured) {
            label->setPosition(m_labelBaselinePos);
            label->setScale(m_labelBaselineScale);
        } else {
            label->setScale(1.f);
        }
        if (auto* lb = typeinfo_cast<CCLabelBMFont*>(label)) {
            lb->setColor(ccc3(255, 255, 255));
        }
        if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(label)) {
            rgba->setOpacity(255);
        }
    }
    m_wasActive = false;
    // Force baseline re-sample on next enable in case GD shifted it.
    m_baselineCaptured = false;
    m_labelBaselineCaptured = false;
}

void ProgressBarManager::tickAnimClock() {
    using clock = std::chrono::steady_clock;
    auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        clock::now().time_since_epoch()).count();
    if (m_lastTickNs == 0) {
        m_lastTickNs = now_ns;
        return;
    }
    float dt = static_cast<float>(now_ns - m_lastTickNs) / 1e9f;
    m_lastTickNs = now_ns;
    dt = std::clamp(dt, 0.f, 0.1f); // cap to avoid huge jumps
    m_animTime += dt;
}

void ProgressBarManager::applyTransform(CCNode* bar) {
    // Position. posX/posY are stored in world-space pixels so sliders
    // and the free-edit drag agree on the same frame of reference.
    if (m_config.useCustomPosition) {
        CCPoint world = ccp(m_config.posX, m_config.posY);
        auto* parent = bar->getParent();
        bar->setPosition(parent ? parent->convertToNodeSpace(world) : world);
    } else {
        bar->setPosition(m_baselinePos);
    }
    // Scale: scaleLength acts along the axis of the bar (X if
    // horizontal, Y if vertical). scaleThickness is perpendicular.
    bar->setScaleX(m_baselineScaleX * m_config.scaleLength);
    bar->setScaleY(m_baselineScaleY * m_config.scaleThickness);

    // Orientation + user rotation. Vertical replaces baseline rotation,
    // userRotation is layered on top.
    float rot = (m_config.vertical ? -90.f : m_baselineRotation) + m_config.userRotation;
    bar->setRotation(rot);
}

void ProgressBarManager::applyOpacity(CCNode* bar) {
    int op = std::clamp(m_config.opacity, 0, 255);
    if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(bar)) {
        rgba->setOpacity(static_cast<GLubyte>(op));
        rgba->setCascadeOpacityEnabled(true);
    }
}

void ProgressBarManager::applySprites(CCNode* bar, CCNode* playLayerRoot) {
    auto slots = collectBarSprites(bar);
    if (slots.all.empty()) return;

    int op = std::clamp(m_config.opacity, 0, 255);

    // Resolve animated colors once per frame.
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

    // Resolve textures only if requested. Tracks whether the texture
    // has *just* been swapped so we can do the one-shot setTextureRect
    // exactly once (instead of every frame, which would clobber GD's
    // own per-frame fill rect updates that drive the % animation).
    CCTexture2D* fillTex = nullptr;
    CCTexture2D* bgTex   = nullptr;
    if (m_config.useFillTexture)
        fillTex = resolveCustomTexture(playLayerRoot, m_fillCustom, m_config.fillTexturePath);
    if (m_config.useBgTexture)
        bgTex = resolveCustomTexture(playLayerRoot, m_bgCustom, m_config.bgTexturePath);

    // Restore vanilla textures when the user turned the toggle off.
    if (!m_config.useFillTexture && m_fillBaselineTex.captured) {
        for (auto* s : slots.all) {
            if (s != slots.bg) { restoreSpriteBaseline(s, m_fillBaselineTex); break; }
        }
        if (m_fillCustom.animHost && m_fillCustom.animHost->getParent())
            m_fillCustom.animHost->removeFromParent();
        m_fillCustom = {};
    }
    if (!m_config.useBgTexture && m_bgBaselineTex.captured) {
        restoreSpriteBaseline(slots.bg, m_bgBaselineTex);
        if (m_bgCustom.animHost && m_bgCustom.animHost->getParent())
            m_bgCustom.animHost->removeFromParent();
        m_bgCustom = {};
    }

    for (auto* spr : slots.all) {
        if (!spr) continue;
        spr->setOpacity(static_cast<GLubyte>(op));

        if (spr == slots.bg) {
            spr->setColor(bgCol);
            if (m_config.useBgTexture && bgTex) {
                bool firstApply = !m_bgBaselineTex.captured;
                if (firstApply) captureSpriteBaseline(spr, m_bgBaselineTex);
                spr->setTexture(bgTex);
                // Bg sprite isn't animated by GD, so it's fine to set
                // the rect to the full texture every frame (cheap).
                spr->setTextureRect({0, 0,
                    bgTex->getContentSize().width,
                    bgTex->getContentSize().height});
            }
        } else {
            // Fill sprite: GD updates its texture rect every frame to
            // animate the percentage. We must NOT setTextureRect here
            // every frame — only when we just swapped textures. After
            // that, GD's own width-update code keeps animating the bar.
            spr->setColor(fillCol);
            if (m_config.useFillTexture && fillTex) {
                bool firstApply = !m_fillBaselineTex.captured;
                if (firstApply) captureSpriteBaseline(spr, m_fillBaselineTex);
                if (firstApply || m_fillCustom.justChanged) {
                    spr->setTexture(fillTex);
                    spr->setTextureRect({0, 0,
                        fillTex->getContentSize().width,
                        fillTex->getContentSize().height});
                    m_fillCustom.justChanged = false;
                } else if (spr->getTexture() != fillTex) {
                    // GIF advanced to a new frame: only swap the
                    // texture pointer, keeping GD's animated rect.
                    auto curRect = spr->getTextureRect();
                    spr->setTexture(fillTex);
                    spr->setTextureRect(curRect);
                }
            }
        }
    }
}

void ProgressBarManager::applyLabel(CCNode* label, CCNode* bar) {
    if (!label) return;
    label->setVisible(m_config.showPercentage);
    if (!m_config.showPercentage) return;

    int op = std::clamp(m_config.opacity, 0, 255);

    float baseSc = m_labelBaselineCaptured ? m_labelBaselineScale : 1.f;
    label->setScale(std::max(0.05f, baseSc * m_config.percentageScale));
    auto* labelParent = label->getParent();

    if (m_config.useCustomLabelPosition) {
        CCPoint world = ccp(m_config.labelPosX, m_config.labelPosY);
        label->setPosition(labelParent
            ? labelParent->convertToNodeSpace(world)
            : world);
    } else if (m_config.useCustomPosition && bar && bar->getParent()) {
        // Bar moved but label not: keep label at vanilla offset RELATIVE
        // to the bar so it follows it.
        CCPoint barWorld = bar->getParent()->convertToWorldSpace(bar->getPosition());
        CCPoint baselineBarWorld = bar->getParent()->convertToWorldSpace(m_baselinePos);
        CCPoint baselineLabelWorld = (labelParent && m_labelBaselineCaptured)
            ? labelParent->convertToWorldSpace(m_labelBaselinePos)
            : CCPoint(0, 0);
        CCPoint offset  = baselineLabelWorld - baselineBarWorld;
        CCPoint newWorld = barWorld + offset
            + ccp(m_config.percentageOffsetX, m_config.percentageOffsetY);
        label->setPosition(labelParent
            ? labelParent->convertToNodeSpace(newWorld)
            : newWorld);
    } else if (m_labelBaselineCaptured) {
        label->setPosition(m_labelBaselinePos
            + ccp(m_config.percentageOffsetX, m_config.percentageOffsetY));
    }

    if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(label)) {
        rgba->setOpacity(static_cast<GLubyte>(op));
    }
    if (auto* lb = typeinfo_cast<CCLabelBMFont*>(label)) {
        ccColor3B pctCol = m_config.useCustomPercentageColor
            ? resolveAnimatedColor(m_config.pctColorMode,
                                   m_config.percentageColor,
                                   m_config.pctColor2,
                                   m_animTime, m_config.colorAnimSpeed)
            : ccc3(255, 255, 255);
        lb->setColor(pctCol);
        if (!m_config.percentageFont.empty()) {
            // Validate font exists before applying. Cocos2d crashes
            // hard if you give it a missing .fnt file.
            auto resolved = CCFileUtils::sharedFileUtils()
                ->fullPathForFilename(m_config.percentageFont.c_str(), false);
            if (!resolved.empty() && resolved != m_config.percentageFont) {
                lb->setFntFile(m_config.percentageFont.c_str());
            }
        }
    }
}

void ProgressBarManager::applyDecorations(CCNode* playLayerRoot) {
    if (!playLayerRoot) return;
    size_t N = m_config.decorations.size();

    // Trim live vector to match config.
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

        bool needsSpawn = !live || !live->getParent()
                       || m_liveDecorationPaths[i] != cfg.path;
        if (needsSpawn) {
            if (live && live->getParent()) live->removeFromParent();
            live = createDecorationSprite(cfg.path);
            m_liveDecorations[i] = live;
            m_liveDecorationPaths[i] = cfg.path;
            if (live) playLayerRoot->addChild(live, 9000);
        }
        if (!live) continue;

        // Convert stored world-space pos to PlayLayer's local space.
        CCPoint world = ccp(cfg.posX, cfg.posY);
        live->setPosition(playLayerRoot->convertToNodeSpace(world));
        live->setScale(std::max(0.05f, cfg.scale));
        live->setRotation(cfg.rotation);
    }
}

void ProgressBarManager::applyToPlayLayer(CCNode* playLayerRoot) {
    if (!playLayerRoot) return;

    // Perf: cache bar/label node references to avoid getChildByIDRecursive every frame
    if (m_cachedPlayLayer != playLayerRoot) {
        m_cachedPlayLayer = playLayerRoot;
        m_cachedBar = findBarNode(playLayerRoot);
        m_cachedLabel = findLabelNode(playLayerRoot);
    }
    auto* bar   = m_cachedBar;
    auto* label = m_cachedLabel;
    if (!bar || !bar->getParent()) {
        // Bar may have been removed, re-search
        m_cachedBar = findBarNode(playLayerRoot);
        m_cachedLabel = findLabelNode(playLayerRoot);
        bar = m_cachedBar;
        label = m_cachedLabel;
    }
    if (!bar) return;

    if (!m_config.enabled) {
        // Restore vanilla once, then leave the bar alone so GD's own
        // logic stays in full control.
        if (m_wasActive) restoreVanillaState(bar, label);
        return;
    }

    // Capture vanilla state on the first active frame; this ensures we
    // store the *real* baseline before applying any of our overrides.
    captureBaselineIfNeeded(bar, label);
    m_wasActive = true;

    tickAnimClock();
    applyTransform(bar);
    applyOpacity(bar);
    applySprites(bar, playLayerRoot);
    applyLabel(label, bar);
    applyDecorations(playLayerRoot);
}
