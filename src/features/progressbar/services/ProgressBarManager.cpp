#include "../../../utils/FilesystemPath.hpp"
#include "ProgressBarManager.hpp"
#include "ProgressBarMath.hpp"
#include "../visuals/CustomProgressBarNode.hpp"
#include "../../colorful-icons/services/IconColorMath.hpp"
#include "../../../utils/AnimatedGIFSprite.hpp"
#include "../../../utils/AtomicFileWrite.hpp"
#include "../../../utils/ImageLoadHelper.hpp"
#include "../../../utils/LocalAssetStore.hpp"

#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/CheckpointObject.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/Geode.hpp>
#include <algorithm>
#include <cmath>
#include <chrono>

using namespace geode::prelude;
using namespace cocos2d;

namespace {

using paimon::icons::math::clampByte;

CCNode* findChildByIDDeep(CCNode* root, char const* id) {
    if (!root || !id) return nullptr;
    return root->getChildByIDRecursive(id);
}

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

CCNode* createDecorationSprite(std::string const& path) {
    if (path.empty()) return nullptr;
    std::filesystem::path fsPath = paimon::assets::pathFromUtf8(path);
    std::error_code ec;
    if (!std::filesystem::exists(fsPath, ec)) return nullptr;
    if (ImageLoadHelper::isAnimatedImage(fsPath)) {
        return AnimatedGIFSprite::create(path);
    }
    auto img = ImageLoadHelper::loadStaticImage(fsPath, 24);
    if (!img.success || !img.texture) return nullptr;
    auto* spr = CCSprite::createWithTexture(img.texture);
    img.texture->release();
    return spr;
}

struct ProgressBarPreset {
    char const* name;
    BarStyle style;
    BarColorMode fillMode;
    ccColor3B fill1;
    ccColor3B fill2;
    ccColor3B fill3;
    ccColor3B bg;
    ccColor3B border;
    float width;
    float height;
    float radius;
    float borderW;
    bool tipGlow;
    bool shadow;
    bool vertical;
    BarLabelPlacement placement;
};

constexpr ProgressBarPreset kPresets[] = {
    {"Neon Cyber",     BarStyle::NeonGlow,  BarColorMode::Gradient2,        {0, 235, 255},   {185, 70, 255},  {255, 110, 210}, {12, 16, 30},  {120, 230, 255}, 230.f, 12.f, 6.f, 1.4f, true,  true,  false, BarLabelPlacement::RightOfBar},
    {"Minimal Pro",    BarStyle::Minimal,   BarColorMode::Solid,            {245, 248, 255}, {200, 220, 255}, {255, 215, 95},  {20, 24, 34},  {255, 255, 255}, 240.f,  7.f, 3.5f, 0.f, false, false, false, BarLabelPlacement::RightOfBar},
    {"Sunset Glow",    BarStyle::Sleek,     BarColorMode::Gradient3,        {255, 95, 109},  {255, 175, 85},  {255, 235, 115}, {26, 16, 28},  {255, 210, 170}, 225.f, 13.f, 6.5f, 1.5f, true,  true,  false, BarLabelPlacement::InsideCenter},
    {"Frosted Glass",  BarStyle::Glass,     BarColorMode::Gradient2,        {135, 220, 255}, {225, 245, 255}, {255, 255, 255}, {35, 45, 68},  {210, 235, 255}, 230.f, 14.f, 7.f, 1.3f, true,  true,  false, BarLabelPlacement::InsideCenter},
    {"Rainbow Wave",   BarStyle::Striped,   BarColorMode::Rainbow,          {255, 80, 120},  {80, 255, 180},  {90, 160, 255},  {16, 18, 28},  {240, 240, 255}, 230.f, 13.f, 6.5f, 1.5f, true,  true,  false, BarLabelPlacement::RightOfBar},
    {"Retro Blocks",   BarStyle::Segmented, BarColorMode::ProgressReactive, {75, 230, 130},  {255, 215, 65},  {255, 80, 110},  {18, 20, 26},  {220, 230, 245}, 235.f, 12.f, 2.f, 1.6f, false, true,  false, BarLabelPlacement::RightOfBar},
    {"Gold Hunter",    BarStyle::Classic,   BarColorMode::Gradient2,        {255, 170, 30},  {255, 240, 120}, {255, 255, 210}, {24, 18, 12},  {255, 225, 130}, 225.f, 12.f, 2.5f, 1.8f, true,  true,  false, BarLabelPlacement::AboveBar},
    {"Vertical HUD",   BarStyle::Sleek,     BarColorMode::Gradient2,        {95, 245, 180},  {60, 175, 255},  {255, 115, 225}, {14, 18, 28},  {210, 245, 255}, 210.f, 11.f, 5.5f, 1.4f, true,  true,  true,  BarLabelPlacement::FollowTip},
};

} // namespace

ProgressBarManager& ProgressBarManager::get() {
    static ProgressBarManager inst;
    return inst;
}

int ProgressBarManager::presetCount() {
    return static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));
}

char const* ProgressBarManager::presetName(int index) {
    if (index < 0 || index >= presetCount()) return "Custom";
    return kPresets[index].name;
}

void ProgressBarManager::applyPreset(int presetIndex) {
    if (presetIndex < 0 || presetIndex >= presetCount()) return;
    auto const& p = kPresets[presetIndex];
    m_config.enabled = true;
    m_config.presetId = presetIndex;
    m_config.style = p.style;
    m_config.fillColorMode = p.fillMode;
    m_config.useCustomFillColor = true;
    m_config.useCustomBgColor = true;
    m_config.fillColor = p.fill1;
    m_config.fillColor2 = p.fill2;
    m_config.fillColor3 = p.fill3;
    m_config.bgColor = p.bg;
    m_config.borderColor = p.border;
    m_config.barWidth = p.width;
    m_config.barHeight = p.height;
    m_config.cornerRadius = p.radius;
    m_config.borderWidth = p.borderW;
    m_config.tipGlow = p.tipGlow;
    m_config.shadowEnabled = p.shadow;
    m_config.showPbMarker = true;
    m_config.showDeathMarker = true;
    m_config.newBestFlash = true;
    m_config.decimalPrecision = 1;
    m_config.smoothing = 0.15f;
    m_config.vertical = p.vertical;
    m_config.labelPlacement = p.placement;
    m_config.useFillTexture = false;
    m_config.useBgTexture = false;
    if (p.vertical) {
        auto winSize = CCDirector::get()->getWinSize();
        m_config.useCustomPosition = true;
        m_config.posX = 24.f;
        m_config.posY = winSize.height * 0.5f;
        m_config.userRotation = 0.f;
    } else {
        m_config.useCustomPosition = false;
        m_config.userRotation = 0.f;
    }
    sanitizeConfig();
    saveConfig();
}

void ProgressBarManager::applyFontIfNeeded(CCLabelBMFont* lb, std::string const& targetFont) {
    if (!lb || targetFont.empty()) return;
    char const* current = lb->getFntFile();
    if (!current || targetFont != current) {
        auto resolved = CCFileUtils::sharedFileUtils()
            ->fullPathForFilename(targetFont.c_str(), false);
        if (!resolved.empty()) {
            lb->setFntFile(targetFont.c_str());
        }
    }
}

std::filesystem::path ProgressBarManager::configPath() const {
    return paimon::paths::toStdPath(Mod::get()->getSaveDir()) / "progressbar_config.json";
}

void ProgressBarManager::loadConfig() {
    auto path = configPath();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return;

    auto rawRes = geode::utils::file::readString(paimon::paths::toSdkPath(path));
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
    auto getInt  = [&](char const* k, int d)  { return static_cast<int>(j[k].asInt().unwrapOr(d)); };
    auto getDbl  = [&](char const* k, double d){ return j[k].asDouble().unwrapOr(d); };
    auto getStr  = [&](char const* k, std::string const& d) { return j[k].asString().unwrapOr(d); };

    m_config.enabled            = getBool("enabled", false);
    m_config.style              = static_cast<BarStyle>(std::clamp(getInt("style", 0), 0, 8));
    m_config.presetId           = getInt("presetId", -1);
    m_config.vertical           = getBool("vertical", false);
    m_config.useCustomPosition  = getBool("useCustomPosition", false);
    m_config.posX               = static_cast<float>(getDbl("posX", 0.0));
    m_config.posY               = static_cast<float>(getDbl("posY", 0.0));
    m_config.barWidth           = static_cast<float>(getDbl("barWidth", 210.0));
    m_config.barHeight          = static_cast<float>(getDbl("barHeight", 10.0));
    m_config.scaleLength        = static_cast<float>(getDbl("scaleLength", 1.0));
    m_config.scaleThickness     = static_cast<float>(getDbl("scaleThickness", 1.0));
    m_config.borderWidth        = static_cast<float>(getDbl("borderWidth", 1.4));
    m_config.cornerRadius       = static_cast<float>(getDbl("cornerRadius", 5.0));
    m_config.shadowEnabled      = getBool("shadowEnabled", false);
    m_config.smoothing          = static_cast<float>(getDbl("smoothing", 0.0));
    m_config.tipGlow            = getBool("tipGlow", false);
    m_config.showPbMarker       = getBool("showPbMarker", false);
    m_config.showCheckpoints    = getBool("showCheckpoints", true);
    m_config.showMilestones     = getBool("showMilestones", false);
    m_config.showDeathMarker    = getBool("showDeathMarker", false);
    m_config.newBestFlash       = getBool("newBestFlash", false);
    m_config.freeDragMode       = getBool("freeDragMode", false);
    m_config.opacity            = getInt("opacity", 255);
    m_config.useCustomFillColor = getBool("useCustomFillColor", false);
    m_config.fillColor.r        = clampByte(getInt("fillR", 0));
    m_config.fillColor.g        = clampByte(getInt("fillG", 255));
    m_config.fillColor.b        = clampByte(getInt("fillB", 40));
    m_config.useCustomBgColor   = getBool("useCustomBgColor", false);
    m_config.bgColor.r          = clampByte(getInt("bgR", 18));
    m_config.bgColor.g          = clampByte(getInt("bgG", 22));
    m_config.bgColor.b          = clampByte(getInt("bgB", 34));
    m_config.borderColor.r      = clampByte(getInt("borderR", 235));
    m_config.borderColor.g      = clampByte(getInt("borderG", 245));
    m_config.borderColor.b      = clampByte(getInt("borderB", 255));
    m_config.hideInPractice     = getBool("hideInPractice", false);
    m_config.showPercentage     = getBool("showPercentage", true);
    m_config.decimalPrecision   = std::clamp(getInt("decimalPrecision", 0), 0, 2);
    m_config.labelFormat        = static_cast<BarLabelFormat>(std::clamp(getInt("labelFormat", 0), 0, 4));
    m_config.labelPlacement     = static_cast<BarLabelPlacement>(std::clamp(getInt("labelPlacement", 1), 0, 6));
    m_config.percentageScale    = static_cast<float>(getDbl("percentageScale", 1.0));
    m_config.percentageOffsetX  = static_cast<float>(getDbl("percentageOffsetX", 0.0));
    m_config.percentageOffsetY  = static_cast<float>(getDbl("percentageOffsetY", 0.0));
    m_config.useCustomPercentageColor = getBool("useCustomPercentageColor", false);
    m_config.percentageColor.r  = clampByte(getInt("pctR", 255));
    m_config.percentageColor.g  = clampByte(getInt("pctG", 255));
    m_config.percentageColor.b  = clampByte(getInt("pctB", 255));
    m_config.percentageFont     = getStr("percentageFont", "");
    m_config.useCustomLabelPosition = getBool("useCustomLabelPosition", false);
    m_config.labelPosX          = static_cast<float>(getDbl("labelPosX", 0.0));
    m_config.labelPosY          = static_cast<float>(getDbl("labelPosY", 0.0));

    auto modeOf = [&](char const* k, BarColorMode def) {
        int v = getInt(k, static_cast<int>(def));
        return static_cast<BarColorMode>(std::clamp(v, 0, 5));
    };
    m_config.fillColorMode = modeOf("fillColorMode", BarColorMode::Solid);
    m_config.bgColorMode   = modeOf("bgColorMode", BarColorMode::Solid);
    m_config.pctColorMode  = modeOf("pctColorMode", BarColorMode::Solid);

    m_config.fillColor2 = { clampByte(getInt("fill2R", 160)),
                            clampByte(getInt("fill2G", 100)),
                            clampByte(getInt("fill2B", 255)) };
    m_config.fillColor3 = { clampByte(getInt("fill3R", 255)),
                            clampByte(getInt("fill3G", 195)),
                            clampByte(getInt("fill3B",  70)) };
    m_config.bgColor2   = { clampByte(getInt("bg2R",  40)),
                            clampByte(getInt("bg2G",  48)),
                            clampByte(getInt("bg2B",  72)) };
    m_config.pctColor2  = { clampByte(getInt("pct2R", 255)),
                            clampByte(getInt("pct2G", 230)),
                            clampByte(getInt("pct2B", 100)) };
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

    bool migrated = false;
    auto migratePath = [&](std::string& p, std::string const& bucket) {
        if (p.empty()) return;
        auto imported = paimon::assets::importStoredPath(p, bucket, paimon::assets::Kind::Image);
        if (imported.success && !imported.path.empty()) {
            auto normalized = paimon::assets::normalizePathString(imported.path);
            if (normalized != p) {
                p = normalized;
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

    log::info("[ProgressBar] Config loaded (style={}, decorations={})",
        static_cast<int>(m_config.style), m_config.decorations.size());
}

void ProgressBarManager::saveConfig() {
    matjson::Value j;
    j["enabled"]            = m_config.enabled;
    j["style"]              = static_cast<int>(m_config.style);
    j["presetId"]           = m_config.presetId;
    j["vertical"]           = m_config.vertical;
    j["useCustomPosition"]  = m_config.useCustomPosition;
    j["posX"]               = m_config.posX;
    j["posY"]               = m_config.posY;
    j["barWidth"]           = m_config.barWidth;
    j["barHeight"]          = m_config.barHeight;
    j["scaleLength"]        = m_config.scaleLength;
    j["scaleThickness"]     = m_config.scaleThickness;
    j["borderWidth"]        = m_config.borderWidth;
    j["cornerRadius"]       = m_config.cornerRadius;
    j["shadowEnabled"]      = m_config.shadowEnabled;
    j["smoothing"]          = m_config.smoothing;
    j["tipGlow"]            = m_config.tipGlow;
    j["showPbMarker"]       = m_config.showPbMarker;
    j["showCheckpoints"]    = m_config.showCheckpoints;
    j["showMilestones"]     = m_config.showMilestones;
    j["showDeathMarker"]    = m_config.showDeathMarker;
    j["newBestFlash"]       = m_config.newBestFlash;
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
    j["borderR"]            = static_cast<int>(m_config.borderColor.r);
    j["borderG"]            = static_cast<int>(m_config.borderColor.g);
    j["borderB"]            = static_cast<int>(m_config.borderColor.b);
    j["hideInPractice"]     = m_config.hideInPractice;
    j["showPercentage"]     = m_config.showPercentage;
    j["decimalPrecision"]   = m_config.decimalPrecision;
    j["labelFormat"]        = static_cast<int>(m_config.labelFormat);
    j["labelPlacement"]     = static_cast<int>(m_config.labelPlacement);
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
    j["fill3R"]             = static_cast<int>(m_config.fillColor3.r);
    j["fill3G"]             = static_cast<int>(m_config.fillColor3.g);
    j["fill3B"]             = static_cast<int>(m_config.fillColor3.b);
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
    if (!paimon::file::writeAtomically(path, std::string_view(j.dump()))) {
        log::error("[ProgressBar] Failed to write config to {}",
                   paimon::paths::toUtf8(path));
    }
}

void ProgressBarManager::resetToDefaults() {
    m_config = ProgressBarConfig{};
    saveConfig();
}

void ProgressBarManager::sanitizeConfig() {
    auto& c = m_config;
    using paimon::progressbar::sanitizeFloat;
    using paimon::progressbar::wrapRotation;

    c.style = static_cast<BarStyle>(std::clamp(static_cast<int>(c.style), 0, 8));
    c.opacity = std::clamp(c.opacity, 0, 255);
    c.decimalPrecision = std::clamp(c.decimalPrecision, 0, 2);
    c.labelFormat = static_cast<BarLabelFormat>(std::clamp(static_cast<int>(c.labelFormat), 0, 4));
    c.labelPlacement = static_cast<BarLabelPlacement>(std::clamp(static_cast<int>(c.labelPlacement), 0, 6));

    c.barWidth       = sanitizeFloat(c.barWidth,       220.f, 60.f, 520.f);
    c.barHeight      = sanitizeFloat(c.barHeight,       12.f,  4.f,  44.f);
    c.borderWidth    = sanitizeFloat(c.borderWidth,    1.5f,  0.f,   5.f);
    c.cornerRadius   = sanitizeFloat(c.cornerRadius,   6.f,   0.f,  24.f);
    c.smoothing      = sanitizeFloat(c.smoothing,      0.15f, 0.f,   1.f);
    c.scaleLength    = sanitizeFloat(c.scaleLength,    1.f,  0.2f,  3.f);
    c.scaleThickness = sanitizeFloat(c.scaleThickness, 1.f,  0.2f,  3.f);
    c.percentageScale = sanitizeFloat(c.percentageScale, 1.f, 0.25f, 3.f);
    c.colorAnimSpeed  = sanitizeFloat(c.colorAnimSpeed, 1.f, 0.1f,  5.f);

    c.posX      = sanitizeFloat(c.posX,      0.f, -4096.f, 8192.f);
    c.posY      = sanitizeFloat(c.posY,      0.f, -4096.f, 8192.f);
    c.labelPosX = sanitizeFloat(c.labelPosX, 0.f, -4096.f, 8192.f);
    c.labelPosY = sanitizeFloat(c.labelPosY, 0.f, -4096.f, 8192.f);
    c.percentageOffsetX = sanitizeFloat(c.percentageOffsetX, 0.f, -1000.f, 1000.f);
    c.percentageOffsetY = sanitizeFloat(c.percentageOffsetY, 0.f, -1000.f, 1000.f);
    c.userRotation = sanitizeFloat(c.userRotation, 0.f, -720.f, 720.f);

    auto modeClamp = [](BarColorMode m) {
        int v = std::clamp(static_cast<int>(m), 0, 5);
        return static_cast<BarColorMode>(v);
    };
    c.fillColorMode = modeClamp(c.fillColorMode);
    c.bgColorMode   = modeClamp(c.bgColorMode);
    c.pctColorMode  = modeClamp(c.pctColorMode);

    auto& font = c.percentageFont;
    if (!font.empty() && (font.find('/') != std::string::npos
        || font.find('\\') != std::string::npos
        || font.size() < 4 || font.substr(font.size() - 4) != ".fnt")) {
        font.clear();
    }

    c.decorations.erase(
        std::remove_if(c.decorations.begin(), c.decorations.end(),
            [](BarDecoration const& d) { return d.path.empty(); }),
        c.decorations.end());
    if (c.decorations.size() > 32) c.decorations.resize(32);
    for (auto& d : c.decorations) {
        d.posX = sanitizeFloat(d.posX, 0.f, -4096.f, 8192.f);
        d.posY = sanitizeFloat(d.posY, 0.f, -4096.f, 8192.f);
        d.scale = sanitizeFloat(d.scale, 1.f, 0.05f, 8.f);
        d.rotation = wrapRotation(d.rotation);
    }
}

bool ProgressBarManager::isFreeDragActive() const {
    return m_config.enabled && m_config.freeDragMode;
}

void ProgressBarManager::beginDrag(CCPoint startWorld) {
    m_dragging = true;
    if (!m_config.useCustomPosition && m_customNode && m_customNode->getParent()) {
        CCPoint w = m_customNode->getParent()->convertToWorldSpace(m_customNode->getPosition());
        m_config.posX = w.x;
        m_config.posY = w.y;
        m_config.useCustomPosition = true;
    } else if (!m_config.useCustomPosition) {
        m_config.useCustomPosition = true;
    }
    m_dragOffset = ccp(m_config.posX - startWorld.x, m_config.posY - startWorld.y);
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

CCNode* ProgressBarManager::getActiveBarNode(CCNode* playLayerRoot) {
    if (m_config.enabled && m_customNode && m_customNode->getParent()) {
        return m_customNode;
    }
    if (!playLayerRoot) playLayerRoot = m_cachedPlayLayer;
    return findBarNode(playLayerRoot);
}

CCNode* ProgressBarManager::getActiveLabelNode(CCNode* playLayerRoot) {
    if (m_config.enabled && m_customNode && m_customNode->getParent()) {
        if (auto* lbl = m_customNode->getLabelNode()) return lbl;
    }
    if (!playLayerRoot) playLayerRoot = m_cachedPlayLayer;
    return findLabelNode(playLayerRoot);
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
    if (slot.path == path) {
        slot.justChanged = false;
        if (slot.animHost) {
            if (auto* gif = typeinfo_cast<AnimatedGIFSprite*>(slot.animHost))
                return gif->getTexture();
            return nullptr;
        }
        return slot.staticTex.data();
    }

    if (slot.animHost && slot.animHost->getParent())
        slot.animHost->removeFromParent();
    slot.animHost = nullptr;
    slot.staticTex = nullptr;
    slot.path = path;
    slot.justChanged = true;

    std::filesystem::path fsPath = paimon::assets::pathFromUtf8(path);
    std::error_code ec;
    if (!std::filesystem::exists(fsPath, ec)) {
        log::warn("[ProgressBar] Custom texture not found: {}", path);
        return nullptr;
    }

    if (ImageLoadHelper::isAnimatedImage(fsPath)) {
        auto* anim = AnimatedGIFSprite::create(path);
        if (!anim) return nullptr;
        anim->setVisible(false);
        anim->setPosition({-9999.f, -9999.f});
        if (host) host->addChild(anim, -1);
        slot.animHost = anim;
        return anim->getTexture();
    }

    auto img = ImageLoadHelper::loadStaticImage(fsPath, 16);
    if (!img.success || !img.texture) return nullptr;
    slot.staticTex = img.texture;
    img.texture->release();
    return slot.staticTex.data();
}

void ProgressBarManager::invalidateBaseline() {
    m_baselineCaptured = false;
    m_labelBaselineCaptured = false;
    m_labelBaselineFont.clear();
    m_practiceHidden = false;
    m_cachedPlayLayer = nullptr;
    m_cachedBar = nullptr;
    m_cachedLabel = nullptr;
    if (m_customNode && m_customNode->getParent()) {
        m_customNode->removeFromParent();
    }
    m_customNode = nullptr;
    m_barSearchCooldown = 0;
    m_telemetry = ProgressBarTelemetry{};
}

void ProgressBarManager::releaseCustomTextures() {
    if (m_fillCustom.animHost && m_fillCustom.animHost->getParent())
        m_fillCustom.animHost->removeFromParent();
    if (m_bgCustom.animHost && m_bgCustom.animHost->getParent())
        m_bgCustom.animHost->removeFromParent();
    m_fillCustom = {};
    m_bgCustom = {};

    for (auto* d : m_liveDecorations) {
        if (d && d->getParent()) d->removeFromParent();
    }
    m_liveDecorations.clear();
    m_liveDecorationPaths.clear();
}

int ProgressBarManager::addDecoration(BarDecoration const& d) {
    if (d.path.empty()) return -1;
    if (m_config.decorations.size() >= 32) return -1;
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
        m_baselineZOrder   = bar->getZOrder();
        m_baselineCaptured = true;
    }
    if (!m_labelBaselineCaptured && label) {
        m_labelBaselinePos   = label->getPosition();
        m_labelBaselineScale = label->getScale();
        if (auto* lb = typeinfo_cast<CCLabelBMFont*>(label)) {
            if (char const* fnt = lb->getFntFile()) m_labelBaselineFont = fnt;
        }
        m_labelBaselineCaptured = true;
    }
}

void ProgressBarManager::restoreVanillaState(CCNode* bar, CCNode* label) {
    m_practiceHidden = false;
    if (m_customNode) {
        m_customNode->setVisible(false);
    }
    if (bar) {
        bar->setVisible(true);
        if (m_baselineCaptured) {
            bar->setPosition(m_baselinePos);
            bar->setScaleX(m_baselineScaleX);
            bar->setScaleY(m_baselineScaleY);
            bar->setRotation(m_baselineRotation);
        }
    }
    if (label) {
        label->setVisible(true);
        if (m_labelBaselineCaptured) {
            label->setPosition(m_labelBaselinePos);
            label->setScale(m_labelBaselineScale);
        }
        if (auto* lb = typeinfo_cast<CCLabelBMFont*>(label)) {
            applyFontIfNeeded(lb, m_labelBaselineFont);
        }
    }
    releaseCustomTextures();
    m_wasActive = false;
    m_baselineCaptured = false;
    m_labelBaselineCaptured = false;
    m_labelBaselineFont.clear();
}

float ProgressBarManager::tickAnimClock() {
    using clock = std::chrono::steady_clock;
    auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        clock::now().time_since_epoch()).count();
    if (m_lastTickNs == 0) {
        m_lastTickNs = now_ns;
        return 0.016f;
    }
    float dt = static_cast<float>(now_ns - m_lastTickNs) / 1e9f;
    m_lastTickNs = now_ns;
    dt = std::clamp(dt, 0.f, 0.1f);
    m_animTime += dt;
    return dt;
}

void ProgressBarManager::notifyLevelReset(
    float startPercent, float bestPercent, bool isPlatformer, bool isPractice
) {
    m_telemetry.startPosPercent = std::clamp(startPercent, 0.f, 100.f);
    m_telemetry.bestPercent = std::clamp(bestPercent, 0.f, 100.f);
    m_telemetry.isPlatformer = isPlatformer;
    m_telemetry.isPractice = isPractice;
    m_telemetry.rawPercent = m_telemetry.startPosPercent;
    m_telemetry.smoothedPercent = m_telemetry.startPosPercent;
    m_telemetry.newBestReached = false;
    m_telemetry.newBestFlashTimer = 0.f;
}

void ProgressBarManager::notifyDeath(float deathPercent) {
    if (std::isfinite(deathPercent) && deathPercent > 0.f) {
        m_telemetry.lastDeathPercent = std::clamp(deathPercent, 0.f, 100.f);
    }
}

void ProgressBarManager::updateTelemetryFromPlayLayer(
    CCNode* playLayerRoot, float dt, bool isPractice
) {
    auto* pl = typeinfo_cast<PlayLayer*>(playLayerRoot);
    if (!pl) return;

    m_telemetry.isPractice = isPractice;
    m_telemetry.isPlatformer = pl->m_isPlatformer;
    m_telemetry.elapsedSeconds = pl->m_attemptTime;

    float pct = pl->getCurrentPercent();
    if (!std::isfinite(pct) || (pct <= 0.f && pl->m_player1 && pl->m_levelLength > 1.f)) {
        pct = (pl->m_player1->getPositionX() / pl->m_levelLength) * 100.f;
    }
    pct = std::clamp(pct, 0.f, 100.f);
    m_telemetry.rawPercent = pct;
    m_telemetry.smoothedPercent = paimon::progressbar::dampProgress(
        m_telemetry.smoothedPercent, pct, m_config.smoothing, dt);

    if (pl->m_level) {
        m_telemetry.bestPercent = static_cast<float>(
            isPractice ? pl->m_level->m_practicePercent : pl->m_level->m_normalPercent.value());
    }

    if (!isPractice && m_telemetry.bestPercent > 1.f && pct > m_telemetry.bestPercent) {
        if (!m_telemetry.newBestReached) {
            m_telemetry.newBestReached = true;
            m_telemetry.newBestFlashTimer = 1.0f;
        }
    }
    if (m_telemetry.newBestFlashTimer > 0.f) {
        m_telemetry.newBestFlashTimer = std::max(0.f, m_telemetry.newBestFlashTimer - dt * 1.8f);
    }

    m_telemetry.checkpoints.clear();
    if (isPractice && pl->m_checkpointArray && pl->m_levelLength > 1.f) {
        for (auto* obj : CCArrayExt<CheckpointObject*>(pl->m_checkpointArray)) {
            if (!obj || !obj->m_physicalCheckpointObject) continue;
            float cx = obj->m_physicalCheckpointObject->getPositionX();
            m_telemetry.checkpoints.push_back(std::clamp((cx / pl->m_levelLength) * 100.f, 0.f, 100.f));
        }
    }
}

void ProgressBarManager::ensureCustomNode(CCNode* playLayerRoot, CCNode* vanillaBar) {
    if (!playLayerRoot || !vanillaBar) return;
    CCNode* parent = vanillaBar->getParent() ? vanillaBar->getParent() : playLayerRoot;
    if (!m_customNode || !m_customNode->getParent() || m_customNode->getParent() != parent) {
        if (m_customNode && m_customNode->getParent()) {
            m_customNode->removeFromParent();
        }
        m_customNode = CustomProgressBarNode::create();
        if (!m_customNode) return;
        parent->addChild(m_customNode, std::max(10, m_baselineZOrder));
    }

    m_customNode->setVisible(true);
    if (m_config.useCustomPosition) {
        CCPoint world = ccp(m_config.posX, m_config.posY);
        m_customNode->setPosition(parent->convertToNodeSpace(world));
    } else {
        m_customNode->setPosition(m_baselinePos);
    }
    m_customNode->setScaleX(std::max(0.15f, m_config.scaleLength));
    m_customNode->setScaleY(std::max(0.15f, m_config.scaleThickness));
    float rot = (m_config.vertical ? -90.f : 0.f) + m_config.userRotation;
    m_customNode->setRotation(rot);
}

void ProgressBarManager::applyDecorations(CCNode* playLayerRoot) {
    if (!playLayerRoot) return;
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

        CCPoint world = ccp(cfg.posX, cfg.posY);
        live->setPosition(playLayerRoot->convertToNodeSpace(world));
        live->setScale(std::max(0.05f, cfg.scale));
        live->setRotation(cfg.rotation);
    }
}

void ProgressBarManager::applyToPlayLayer(CCNode* playLayerRoot, bool isPracticeMode) {
    if (!playLayerRoot) return;
    if (!m_config.enabled && !m_wasActive) return;

    if (m_cachedPlayLayer != playLayerRoot) {
        m_cachedPlayLayer = playLayerRoot;
        m_cachedBar = findBarNode(playLayerRoot);
        m_cachedLabel = findLabelNode(playLayerRoot);
        m_barSearchCooldown = 0;
    }
    auto* bar   = m_cachedBar;
    auto* label = m_cachedLabel;
    if (!bar || !bar->getParent()) {
        if (--m_barSearchCooldown > 0) return;
        m_barSearchCooldown = kBarSearchInterval;
        m_cachedBar = findBarNode(playLayerRoot);
        m_cachedLabel = findLabelNode(playLayerRoot);
        bar = m_cachedBar;
        label = m_cachedLabel;
    }
    if (!bar) return;

    if (!m_config.enabled) {
        if (m_wasActive) restoreVanillaState(bar, label);
        return;
    }

    captureBaselineIfNeeded(bar, label);
    m_wasActive = true;

    if (m_config.hideInPractice && isPracticeMode) {
        bar->setVisible(false);
        if (label) label->setVisible(false);
        if (m_customNode) m_customNode->setVisible(false);
        m_practiceHidden = true;
        return;
    }
    if (m_practiceHidden) {
        m_practiceHidden = false;
    }

    // Hide vanilla bar & vanilla label; our CustomProgressBarNode renders the
    // full custom progress engine cleanly without stretching 9-slices.
    bar->setVisible(false);
    if (label) label->setVisible(false);

    float dt = tickAnimClock();
    updateTelemetryFromPlayLayer(playLayerRoot, dt, isPracticeMode);
    ensureCustomNode(playLayerRoot, bar);

    if (m_customNode) {
        CCTexture2D* fillTex = m_config.useFillTexture
            ? resolveCustomTexture(playLayerRoot, m_fillCustom, m_config.fillTexturePath)
            : nullptr;
        CCTexture2D* bgTex = m_config.useBgTexture
            ? resolveCustomTexture(playLayerRoot, m_bgCustom, m_config.bgTexturePath)
            : nullptr;
        m_customNode->renderState(m_config, m_telemetry, m_animTime, fillTex, bgTex);
    }

    applyDecorations(playLayerRoot);
}
