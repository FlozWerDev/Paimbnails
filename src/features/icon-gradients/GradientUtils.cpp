#include "GradientUtils.hpp"

#include "hooks/GradientSimplePlayer.hpp"
#include "services/GradientAnimationManager.hpp"
#include "services/GradientImage.hpp"

#include "../../utils/GLSLLoader.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <numeric>
#include <vector>

// Gradient rendering core, after zilko's "Icon Gradients" (independent implementation, own expression).
// Save schema, program key format, node ids and sentinels (-4732, SDI keys) preserved for compatibility.

using namespace geode::prelude;
using namespace paimon::icon_gradients;

namespace {

// ---------------------------------------------------------------------------
// Data tables (replace the switch ladders)
// ---------------------------------------------------------------------------

struct IconRow {
    IconType type;
    int (*live)(GameManager*);
    char const* sdiKey;
};

constexpr IconRow kIconRows[] = {
    {IconType::Cube, [](GameManager* gm) { return gm->getPlayerFrame(); }, "cube"},
    {IconType::Ship, [](GameManager* gm) { return gm->getPlayerShip(); }, "ship"},
    {IconType::Ball, [](GameManager* gm) { return gm->getPlayerBall(); }, "roll"},
    {IconType::Ufo, [](GameManager* gm) { return gm->getPlayerBird(); }, "bird"},
    {IconType::Wave, [](GameManager* gm) { return gm->getPlayerDart(); }, "dart"},
    {IconType::Robot, [](GameManager* gm) { return gm->getPlayerRobot(); }, "robot"},
    {IconType::Spider, [](GameManager* gm) { return gm->getPlayerSpider(); }, "spider"},
    {IconType::Swing, [](GameManager* gm) { return gm->getPlayerSwing(); }, "swing"},
    {IconType::Jetpack, [](GameManager* gm) { return gm->getPlayerJetpack(); }, "jetpack"},
};

IconRow const* findIconRow(IconType type) {
    for (auto& row : kIconRows)
        if (row.type == type) return &row;
    return &kIconRows[0];
}

struct ColorRow {
    ColorType type;
    int (*live)(GameManager*);
    char const* sdiKey;
    int sdiDefault;
};

constexpr ColorRow kColorRows[] = {
    {ColorType::Main, [](GameManager* gm) { return gm->getPlayerColor(); }, "color1", 13},
    {ColorType::Secondary, [](GameManager* gm) { return gm->getPlayerColor2(); }, "color2", 14},
    {ColorType::Glow, [](GameManager* gm) { return gm->getPlayerGlowColor(); }, "colorglow", 14},
};

ColorRow const* findColorRow(ColorType type) {
    for (auto& row : kColorRows)
        if (row.type == type) return &row;
    return nullptr;
}

constexpr char const* kIconNames[] = {
    "cube", "ship", "ball", "ufo", "wave", "robot", "spider", "swing", "jetpack",
};

// One sprite plus the id its shader program is cached under.
struct PaintTarget {
    CCSprite* sprite;
    int shaderId;
};

// Robot/spider icons animate through a GJRobotSprite; whichever form is
// currently visible owns the paint.
GJRobotSprite* visibleMech(SimplePlayer* icon) {
    GJRobotSprite* mech = nullptr;
    if (icon->m_robotSprite && icon->m_robotSprite->isVisible()) mech = icon->m_robotSprite;
    if (icon->m_spiderSprite && icon->m_spiderSprite->isVisible()) mech = icon->m_spiderSprite;
    return mech;
}

// Finds the line overlay hosted on a sprite, creating and fitting it on first
// use. The overlay always mirrors the host frame afterwards.
CCSprite* lineOverlay(CCSprite* host, char const* nodeId, bool keepVisible) {
    CCSprite* overlay = typeinfo_cast<CCSprite*>(host->getChildByID(nodeId));
    if (!overlay) {
        overlay = CCSprite::createWithSpriteFrame(host->displayFrame());
        overlay->setID(nodeId);
        if (keepVisible) {
            overlay->runAction(CCRepeatForever::create(
                CCSequence::create(CCShow::create(), nullptr)));
        }
        host->addChild(overlay, 1);
    } else {
        overlay->setDisplayFrame(host->displayFrame());
    }
    overlay->setContentSize(host->getContentSize());
    overlay->setPosition(host->getContentSize() / 2.f);
    return overlay;
}

// Farthest point from a reference, starting the search at the origin so an
// empty/all-identical set still yields a deterministic axis.
CCPoint farthestFrom(std::vector<SimplePoint> const& points, CCPoint from) {
    CCPoint best = {0.f, 0.f};
    float bestDist = 0.f;
    for (auto& point : points) {
        float dist = ccpDistance(point.pos, from);
        if (dist > bestDist) {
            bestDist = dist;
            best = point.pos;
        }
    }
    return best;
}

// Line-shader edge threshold from the texture quality setting.
float lineThreshold() {
    if (GradientCache::get().m_increaseLineTolerance) return 1.f;
    static constexpr float levels[] = {-10.f, -1.25f, -2.5f};
    int quality = GameManager::get()->m_texQuality;
    float threshold = (quality >= 0 && quality <= 2) ? levels[quality] : -10.f;
#ifdef GEODE_IS_MOBILE
    if (!Loader::get()->isModLoaded("weebify.high-graphics-android")) threshold = -2.5f;
#endif
    return threshold;
}

std::string fragmentName(bool linear, bool blend, bool line) {
    return fmt::format("{}_gradient{}.fsh",
        linear ? "linear" : "radial", line ? "_line" : blend ? "_blend" : "");
}

CCGLProgram* compileProgram(std::string const& vertSrc, std::string const& fragSrc) {
    CCGLProgram* program = new CCGLProgram();
    if (!program->initWithVertexShaderByteArray(vertSrc.c_str(), fragSrc.c_str())) {
        log::error("[IconGradients] Shader compile failed");
        program->release();
        return nullptr;
    }
    program->addAttribute(kCCAttributeNamePosition, kCCVertexAttrib_Position);
    program->addAttribute(kCCAttributeNameColor, kCCVertexAttrib_Color);
    program->addAttribute(kCCAttributeNameTexCoord, kCCVertexAttrib_TexCoords);
    if (!program->link()) {
        log::error("[IconGradients] Shader link failed");
        program->release();
        return nullptr;
    }
    program->updateUniforms();
    return program;
}

struct IconBounds {
    bool found = false;
    float minX = 0.f;
    float minY = 0.f;
    float maxX = 0.f;
    float maxY = 0.f;

    void include(CCPoint point) {
        if (!found) {
            minX = maxX = point.x;
            minY = maxY = point.y;
            found = true;
            return;
        }
        minX = std::min(minX, point.x);
        minY = std::min(minY, point.y);
        maxX = std::max(maxX, point.x);
        maxY = std::max(maxY, point.y);
    }
};

void collectIconBounds(CCNode* node, SimplePlayer* icon, IconBounds& bounds) {
    if (!node->isVisible()) return;

    if (auto sprite = typeinfo_cast<CCSprite*>(node); sprite && sprite->getOpacity() > 0) {
        auto size = sprite->getContentSize();
        for (auto point : std::array{
            CCPoint{0.f, 0.f},
            CCPoint{size.width, 0.f},
            CCPoint{0.f, size.height},
            CCPoint{size.width, size.height},
        }) {
            bounds.include(icon->convertToNodeSpace(sprite->convertToWorldSpace(point)));
        }
    }

    if (auto children = node->getChildren()) {
        for (auto child : CCArrayExt<CCNode*>(children)) {
            collectIconBounds(child, icon, bounds);
        }
    }
}

bool isGradientContainer(matjson::Value const& value) {
    if (!value.isObject()) return false;
    for (int color = ColorType::Main; color <= ColorType::Line; color++) {
        std::string key = "color" + std::to_string(color);
        if (value.contains(key) && value[key].isObject()) return true;
    }
    return false;
}

int64_t currentIconID(IconType type) {
    auto gm = GameManager::get();
    switch (type) {
        case IconType::Cube: return gm->getPlayerFrame();
        case IconType::Ship: return gm->getPlayerShip();
        case IconType::Robot: return gm->getPlayerRobot();
        case IconType::Spider: return gm->getPlayerSpider();
        case IconType::Swing: return gm->getPlayerSwing();
        case IconType::Jetpack: return gm->getPlayerJetpack();
        default: return 0;
    }
}

// Paint targets for a robot/spider form, grouped by color slot. The shader
// ids (100s head parts, 200s body parts, 300s glow parts, 400 extra,
// 500/600/700 line overlays) are part of the program cache key scheme.
void collectMechTargets(GJRobotSprite* mech, ColorType color, bool lineVisible,
        std::vector<PaintTarget>& out) {
    switch (color) {
        case ColorType::Main: {
            int id = 100;
            for (auto* part : mech->m_headSprite->getParent()->getChildrenExt<CCSpritePart*>())
                out.push_back({part, ++id});
            break;
        }
        case ColorType::Secondary: {
            int id = 200;
            for (auto* spr : CCArrayExt<CCSprite*>(mech->m_secondArray)) {
                if (spr == mech->m_headSprite) continue;
                out.push_back({spr, ++id});
            }
            break;
        }
        case ColorType::Glow: {
            int id = 300;
            for (auto* spr : mech->m_glowSprite->getChildrenExt<CCSprite*>())
                out.push_back({spr, ++id});
            break;
        }
        case ColorType::White:
            mech->m_extraSprite->setZOrder(2);
            out.push_back({mech->m_extraSprite, 400});
            break;
        case ColorType::Line: {
            int id = 500;
            for (auto* part : mech->m_headSprite->getParent()->getChildrenExt<CCSpritePart*>()) {
                auto* overlay = lineOverlay(part, "gradient-line"_spr, false);
                overlay->setVisible(lineVisible);
                out.push_back({overlay, ++id});
            }
            id = 600;
            for (auto* spr : CCArrayExt<CCSprite*>(mech->m_secondArray)) {
                if (spr == mech->m_headSprite) continue;
                auto* overlay = lineOverlay(spr, "gradient-line2"_spr, false);
                overlay->setVisible(lineVisible);
                out.push_back({overlay, ++id});
            }
            auto* extra = lineOverlay(mech->m_extraSprite, "gradient-line"_spr, false);
            extra->setVisible(lineVisible);
            out.push_back({extra, 700});
            break;
        }
    }
}

// Paint targets for a plain icon: one sprite per slot, three line overlays
// for the Line slot. The ball keeps its outline visible when the
// fine-outline mod is around.
void collectIconTargets(SimplePlayer* icon, IconType kind, ColorType color, bool lineVisible,
        std::vector<PaintTarget>& out) {
    switch (color) {
        case ColorType::Main:
            out.push_back({icon->m_firstLayer, 100});
            break;
        case ColorType::Secondary:
            out.push_back({icon->m_secondLayer, 200});
            break;
        case ColorType::Glow:
            out.push_back({icon->m_outlineSprite, 300});
            break;
        case ColorType::White:
            icon->m_detailSprite->setZOrder(2);
            out.push_back({icon->m_detailSprite, 400});
            break;
        case ColorType::Line: {
            bool keepVisible = kind == IconType::Ball
                && Loader::get()->isModLoaded("alphalaneous.fine_outline");
            CCSprite* hosts[] = {icon->m_firstLayer, icon->m_secondLayer, icon->m_detailSprite};
            int id = 500;
            for (auto* host : hosts) {
                auto* overlay = lineOverlay(host, "gradient-line"_spr, keepVisible);
                overlay->setVisible(lineVisible);
                out.push_back({overlay, id});
                id += 100;
            }
            break;
        }
    }
}

} // namespace

bool GradientConfig::isEmpty(ColorType colorType, bool secondPlayer) const {
    if (points.empty()) return true;

    ccColor3B color = GradientUtils::getPlayerColor(colorType, secondPlayer);
    for (auto& point : points)
        if (!point.imagePath.empty() || point.color != color)
            return false;
    return true;
}

SimplePlayer* GradientUtils::createIcon(IconType type, bool secondPlayer) {
    SimplePlayer* icon = SimplePlayer::create(1);
    icon->updatePlayerFrame(getIconID(type, secondPlayer), type);
    icon->disableGlowOutline();
    return icon;
}

void GradientUtils::fitIcon(SimplePlayer* icon, CCSize box, CCPoint center) {
    icon->setScale(1.f);

    IconBounds bounds;
    collectIconBounds(icon, icon, bounds);

    if (!bounds.found) {
        icon->setScale(std::min(box.width, box.height) / 30.f);
        icon->setPosition(center);
        return;
    }

    float width = std::max(bounds.maxX - bounds.minX, 1.f);
    float height = std::max(bounds.maxY - bounds.minY, 1.f);
    float scale = std::min(box.width / width, box.height / height);
    CCPoint visualCenter = {
        (bounds.minX + bounds.maxX) / 2.f,
        (bounds.minY + bounds.maxY) / 2.f,
    };

    icon->setScale(scale);
    icon->setPosition(center - visualCenter * scale);
}

CCMenuItemToggler* GradientUtils::createTypeToggle(bool radial, CCPoint pos, CCObject* target, SEL_MenuHandler callback) {
    auto face = [](char const* bg, char const* glyph) {
        CCSprite* face = CCSprite::create(bg);
        CCSprite* mark = CCSprite::createWithSpriteFrameName(glyph);
        mark->setScale(1.6f);
        mark->setPosition(face->getContentSize() / 2.f);
        face->addChild(mark);
        return face;
    };
    char const* glyph = radial ? "edit_areaModeBtn04_001.png" : "edit_areaModeBtn03_001.png";

    CCMenuItemToggler* toggle = CCMenuItemToggler::create(
        face("GJ_button_04.png", glyph), face("GJ_button_02.png", glyph), target, callback);
    toggle->setScale(0.475f);
    toggle->setPosition(pos);
    return toggle;
}

ccColor3B GradientUtils::getPlayerColor(ColorType colorType, bool secondPlayer) {
    auto gm = GameManager::get();

    if (colorType == ColorType::White) return ccWHITE;
    if (colorType == ColorType::Line) return ccBLACK;

    // The second player mirrors the primary palette unless it has its own kit.
    if (secondPlayer && colorType != ColorType::Glow)
        colorType = colorType == ColorType::Main ? ColorType::Secondary : ColorType::Main;

    ColorRow const* row = findColorRow(colorType);
    if (!row) return ccWHITE;

    int index = (secondPlayer && sdiEnabled() && isSettingEnabled(P2_SEPARATE))
        ? static_cast<int>(sdiSaved<int64_t>(row->sdiKey, row->sdiDefault))
        : row->live(gm);
    return gm->colorForIdx(index);
}

int GradientUtils::getIconID(IconType type, bool secondPlayer) {
    IconRow const* row = findIconRow(type);
    if (secondPlayer && sdiEnabled())
        return static_cast<int>(sdiSaved<int64_t>(row->sdiKey, 1));
    return row->live(GameManager::get());
}

bool GradientUtils::isGradientSaved(GradientConfig config) {
    for (auto& saved : Mod::get()->getSavedValue<matjson::Value>(kSavedGradientsKey))
        if (configFromObject(saved) == config)
            return true;
    return false;
}

bool GradientUtils::isSettingEnabled(int setting) {
    struct FlagRow {
        int id;
        bool (*read)();
    };
    static constexpr FlagRow rows[] = {
        {MOD_DISABLED, &GradientCache::isModDisabled},
        {P2_DISABLED, &GradientCache::is2PDisabled},
        {P2_FLIP, &GradientCache::is2PFlip},
        {MENU_GRADIENTS, &GradientCache::isMenuGradientsEnabled},
        {P2_SEPARATE, &GradientCache::is2PSeparate},
    };
    for (auto& row : rows)
        if (row.id == setting) return row.read();
    return false;
}

GradientConfig GradientUtils::getDefaultConfig(ColorType colorType, bool secondPlayer) {
    ccColor3B color = getPlayerColor(colorType, secondPlayer);
    return {{{{0.5f, 1.1f}, color}, {{0.5f, -0.1f}, color}}, true};
}

matjson::Value GradientUtils::getSaveObject(GradientConfig config) {
    matjson::Value points = matjson::Value::array();
    for (auto& point : config.points) {
        matjson::Value node;
        node["pos"]["x"] = point.pos.x;
        node["pos"]["y"] = point.pos.y;
        node["color"]["r"] = point.color.r;
        node["color"]["g"] = point.color.g;
        node["color"]["b"] = point.color.b;
        if (!point.imagePath.empty()) node["image"] = point.imagePath;
        points.push(node);
    }
    matjson::Value ret;
    ret["points"] = points;
    ret["linear"] = config.isLinear;
    return ret;
}

void GradientUtils::removeSavedGradient(GradientConfig config) {
    matjson::Value kept = matjson::Value::array();
    for (auto& saved : Mod::get()->getSavedValue<matjson::Value>(kSavedGradientsKey))
        if (configFromObject(saved) != config)
            kept.push(saved);
    Mod::get()->setSavedValue(kSavedGradientsKey, kept);
}

void GradientUtils::saveConfig(GradientConfig config, const std::string& id, const std::string& secondId) {
    matjson::Value container = Mod::get()->getSavedValue<matjson::Value>(id);
    if (secondId.empty()) {
        if (!container.isArray()) container = matjson::Value::array();
        container.push(getSaveObject(config));
    } else {
        container[secondId] = getSaveObject(config);
    }
    Mod::get()->setSavedValue(id, container);
}

GradientConfig GradientUtils::configFromObject(const matjson::Value& object) {
    GradientConfig config;
    config.isLinear = object["linear"].asBool().unwrapOr(true);
    for (auto& point : object["points"]) {
        config.points.push_back({
            ccp(point["pos"]["x"].asDouble().unwrapOr(0.0),
                point["pos"]["y"].asDouble().unwrapOr(0.0)),
            ccc3(point["color"]["r"].asInt().unwrapOr(0),
                 point["color"]["g"].asInt().unwrapOr(0),
                 point["color"]["b"].asInt().unwrapOr(0)),
            point["image"].asString().unwrapOr(object["image"].asString().unwrapOr("")),
        });
    }
    return config;
}

GradientConfig GradientUtils::getSavedConfig(IconType type, ColorType colorType, bool secondPlayer) {
    if (!isSettingEnabled(P2_SEPARATE)) secondPlayer = false;

    std::string id = getConfigKey(type, secondPlayer);
    if (!Mod::get()->hasSavedValue(id))
        return getDefaultConfig(colorType, secondPlayer);

    matjson::Value stored = Mod::get()->getSavedValue<matjson::Value>(id);
    std::string color = "color" + std::to_string(colorType);
    if (!stored.isObject() || !stored.contains(color) || !stored[color].isObject())
        return getDefaultConfig(colorType, secondPlayer);
    return configFromObject(stored[color]);
}

Gradient GradientUtils::getGradient(IconType type, bool secondPlayer) {
    Gradient gradient = {
        getSavedConfig(type, ColorType::Main, secondPlayer),
        getSavedConfig(type, ColorType::Secondary, secondPlayer),
        getSavedConfig(type, ColorType::Glow, secondPlayer),
        getSavedConfig(type, ColorType::White, secondPlayer),
        getSavedConfig(type, ColorType::Line, secondPlayer),
    };
    if (secondPlayer && isSettingEnabled(P2_FLIP) && !isSettingEnabled(P2_SEPARATE))
        std::swap(gradient.main, gradient.secondary);
    return gradient;
}

void GradientUtils::setIconColors(SimplePlayer* icon, ColorType, bool white, bool secondPlayer) {
    auto gm = GameManager::get();
    auto base = [&](ColorType type) {
        return white ? ccc3(255, 255, 255) : getPlayerColor(type, secondPlayer);
    };

    icon->setColor(base(ColorType::Main));
    icon->setSecondColor(base(ColorType::Secondary));

    bool halo = gm->getPlayerGlow();
    if (secondPlayer && sdiEnabled() && isSettingEnabled(P2_SEPARATE))
        halo = sdiSaved<bool>("glow", false);
    icon->m_hasGlowOutline = halo;

    if (halo)
        icon->enableCustomGlowColor(base(ColorType::Glow));
    else
        icon->disableCustomGlowColor();

    icon->updateColors();
}

std::string GradientUtils::getTypeID(IconType type) {
    int index = static_cast<int>(type);
    if (index < 0 || index >= static_cast<int>(std::size(kIconNames))) return "global";
    return kIconNames[index];
}

std::string GradientUtils::getTypeID(SpriteType type) {
    int index = static_cast<int>(type) - 1;
    static constexpr char const* names[] = {"icon", "vehicle", "animation"};
    if (index < 0 || index >= static_cast<int>(std::size(names))) return "icon";
    return names[index];
}

std::string GradientUtils::getConfigKey(IconType type, bool secondPlayer) {
    std::string key = "icon-gradients-" + getTypeID(type);
    if (secondPlayer) key += "-p2";
    return key;
}

void GradientUtils::migrateLegacyStorage() {
    auto mod = Mod::get();

    constexpr std::array types = {
        static_cast<IconType>(-1),
        IconType::Cube,
        IconType::Ship,
        IconType::Ball,
        IconType::Ufo,
        IconType::Wave,
        IconType::Robot,
        IconType::Spider,
        IconType::Swing,
        IconType::Jetpack,
    };

    for (IconType type : types) {
        for (bool secondPlayer : {false, true}) {
            std::string legacyKey = getTypeID(type);
            if (secondPlayer) legacyKey += "-p2";
            if (!mod->hasSavedValue(legacyKey)) continue;

            matjson::Value legacy = mod->getSavedValue<matjson::Value>(legacyKey);
            if (!isGradientContainer(legacy)) continue;

            std::string key = getConfigKey(type, secondPlayer);
            if (!mod->hasSavedValue(key))
                mod->setSavedValue(key, legacy);

            mod->getSaveContainer().erase(legacyKey);

            int64_t iconID = secondPlayer ? 0 : currentIconID(type);
            if (iconID > 0)
                mod->setSavedValue<int64_t>(legacyKey, iconID);
        }
    }

    for (bool secondPlayer : {false, true}) {
        std::string globalKey = getConfigKey(static_cast<IconType>(-1), secondPlayer);
        if (!mod->hasSavedValue(globalKey)) continue;
        matjson::Value global = mod->getSavedValue<matjson::Value>(globalKey);
        if (!isGradientContainer(global)) continue;
        // The old shared kit becomes each icon's own starting point; later edits stay per icon.
        for (size_t i = 1; i < types.size(); ++i) {
            std::string key = getConfigKey(types[i], secondPlayer);
            if (!mod->hasSavedValue(key))
                mod->setSavedValue(key, global);
        }
        mod->getSaveContainer().erase(globalKey);
    }

    if (!mod->hasSavedValue(kSavedGradientsKey) && mod->hasSavedValue("saved-gradients")) {
        matjson::Value saved = mod->getSavedValue<matjson::Value>("saved-gradients");
        if (saved.isArray()) {
            mod->setSavedValue(kSavedGradientsKey, saved);
            mod->getSaveContainer().erase("saved-gradients");
        }
    }
}

IconType GradientUtils::getIconType(SimplePlayer* icon) {
    return static_cast<GradientSimplePlayer*>(icon)->m_fields->m_type;
}

std::vector<GradientConfig> GradientUtils::getSavedGradients() {
    std::vector<GradientConfig> ret;
    for (auto obj : Mod::get()->getSavedValue<matjson::Value>(kSavedGradientsKey))
        ret.push_back(configFromObject(obj));
    return ret;
}

void GradientUtils::applyGradient(SimplePlayer* icon, Gradient gradient, bool blend, bool secondPlayer, int extra) {
    applyGradient(icon, gradient.main, ColorType::Main, blend, secondPlayer, extra);
    applyGradient(icon, gradient.secondary, ColorType::Secondary, blend, secondPlayer, extra);
    applyGradient(icon, gradient.glow, ColorType::Glow, blend, secondPlayer, extra);
    applyGradient(icon, gradient.white, ColorType::White, blend, secondPlayer, extra);
    applyGradient(icon, gradient.line, ColorType::Line, blend, secondPlayer, extra);
}

void GradientUtils::paintMenuIcon(SimplePlayer* icon, bool secondPlayer, int extra) {
    applyGradient(icon, getGradient(getIconType(icon), secondPlayer), false, secondPlayer, extra);
}

void GradientUtils::applyGradient(SimplePlayer* icon, GradientConfig config, ColorType colorType, bool blend, bool secondPlayer, int extra) {
    IconType kind = getIconType(icon);
    bool lineVisible = colorType != ColorType::Line
        || !config.isEmpty(ColorType::Line, secondPlayer);

    std::vector<PaintTarget> targets;
    if (GJRobotSprite* mech = visibleMech(icon))
        collectMechTargets(mech, colorType, lineVisible, targets);
    else
        collectIconTargets(icon, kind, colorType, lineVisible, targets);

    bool line = colorType == ColorType::Line;
    for (auto& target : targets)
        applyGradient(target.sprite, config, kind, colorType,
            target.shaderId, blend, secondPlayer, false, extra, line);
}

CCGLProgram* GradientUtils::createShader(const std::string& key, bool linear, bool blend, bool line) {
    // Compile from the file contents read manually. initWithVertexShaderFilename
    // resolves against the game's search path, not the mod's resources, so it
    // silently fails on packaged installs — that's the "gradients are blank"
    // bug. readShaderFile tries both the dev (resources/shaders/) and the
    // installed (flattened resources/) layouts.
    std::string fragName = fragmentName(linear, blend, line);

    if (!key.empty()) {
        if (CCGLProgram* cached = CCShaderCache::sharedShaderCache()->programForKey(key.c_str()))
            return cached;
        return paimon::shaders::loadShader(key, "position.vert", fragName, nullptr, nullptr);
    }

    // Uncacheable programs: each ColorToggle gets its own instance so
    // uniforms don't clobber each other. Compiled fresh every call.
    std::string vertSrc = paimon::shaders::readShaderFile("position.vert");
    std::string fragSrc = paimon::shaders::readShaderFile(fragName);
    if (vertSrc.empty() || fragSrc.empty()) return nullptr;

    CCGLProgram* program = compileProgram(vertSrc, fragSrc);
    if (!program) {
        log::error("[IconGradients] Failed to compile '{}'", fragName);
        return nullptr;
    }
    program->autorelease();
    return program;
}

void GradientUtils::applyGradient(CCSprite* sprite, GradientConfig config, IconType iconType, ColorType colorType, int id, bool blend, bool secondPlayer, bool playerObject, int extra, bool line) {
    if (!sprite) return;

    // The shaders take at most 24 stops; extra points never reach the GPU.
    if (config.points.size() > 24) config.points.resize(24);
    auto atlas = getGradientImageAtlas(config.points);
    bool image = atlas && atlas->texture;
    if (!atlas) setGradientImage(sprite, nullptr);

    if (config.isEmpty(colorType, secondPlayer)) {
        return sprite->setShaderProgram(
            CCShaderCache::sharedShaderCache()->programForKey(kCCShader_PositionTextureColor));
    }

    CCGLProgram* program = nullptr;

    // The key layout below is shared with the prewarm table in
    // GradientCache.cpp — keep the field order or prewarming misses.
    if (extra != -4732) {
        std::string key = fmt::format("{}-{}-{}-{}-{}-{}-{}-{}"_spr, config.isLinear,
            static_cast<int>(iconType), id, blend, line, secondPlayer, playerObject, extra);
        if (image) key += "-image";
        program = createShader(key, config.isLinear, blend, line);
    } else {
        program = createShader("", config.isLinear, blend, line);
    }

    if (!program) {
        sprite->setShaderProgram(
            CCShaderCache::sharedShaderCache()->programForKey(kCCShader_PositionTextureColor));
        return;
    }

    sprite->setShaderProgram(program);
    setGradientImage(sprite, atlas);

    // Uniform locations resolve through the program's own name cache, so the
    // driver string lookup happens once per program instead of once per
    // sprite on every repaint.
    program->use();
    program->setUniformsForBuiltins();

    auto uniform = [&](char const* name) {
        return program->getUniformLocationForName(name);
    };
    glUniform1i(uniform("u_imageMode"), image ? 1 : 0);

    if (extra != -4732)
        GradientAnimationManager::get().track(program);

    CCSpriteFrame* frame = sprite->displayFrame();
    CCRect rectInPixels = frame->getRectInPixels();
    CCSize texSize = frame->getTexture()->getContentSizeInPixels();
    bool rot = frame->m_bRotated;

    glUniform2f(uniform("uvMin"),
        rectInPixels.origin.x / texSize.width, rectInPixels.origin.y / texSize.height);
    glUniform2f(uniform("uvMax"),
        (rectInPixels.origin.x + rectInPixels.size.width) / texSize.width,
        (rectInPixels.origin.y + rectInPixels.size.height) / texSize.height);

    if (colorType == ColorType::Line) {
        glUniform2f(uniform("u_pixelSize"), 1.f / texSize.width, 1.f / texSize.height);
        glUniform1f(uniform("u_threshold"), lineThreshold());
    }

    size_t count = config.points.size();
    std::vector<float> imageSlots(count, -1.f);
    std::vector<ccColor4F> colors(count);
    for (size_t i = 0; i < count; ++i) {
        colors[i] = ccc4FFromccc3B(config.points[i].color);
        if (image) {
            if (auto slot = atlas->slots.find(config.points[i].imagePath); slot != atlas->slots.end())
                imageSlots[i] = static_cast<float>(slot->second);
        }
    }

    std::vector<size_t> order(count);
    std::iota(order.begin(), order.end(), 0);

    if (config.isLinear) {
        CCPoint start = farthestFrom(config.points, {0.5f, 0.5f});
        CCPoint end = farthestFrom(config.points, start);
        float span = ccpDistance(start, end);

        std::vector<float> stops(count);
        for (size_t i = 0; i < count; ++i)
            stops[i] = span > 0.f ? ccpDistance(config.points[i].pos, start) / span : 0.f;

        // Order stops along the axis, keeping the original order on ties.
        std::stable_sort(order.begin(), order.end(),
            [&](size_t a, size_t b) { return stops[a] < stops[b]; });

        std::vector<float> sortedStops(count);
        std::vector<GLfloat> sortedColors(count * 4);
        std::vector<GLfloat> sortedSlots(count);
        for (size_t i = 0; i < count; ++i) {
            size_t at = order[i];
            sortedStops[i] = stops[at];
            sortedColors[i * 4] = colors[at].r;
            sortedColors[i * 4 + 1] = colors[at].g;
            sortedColors[i * 4 + 2] = colors[at].b;
            sortedColors[i * 4 + 3] = colors[at].a;
            sortedSlots[i] = imageSlots[at];
        }

        glUniform2f(uniform("startPoint"),
            rot ? start.y : start.x, rot ? start.x : (1 - start.y));
        glUniform2f(uniform("endPoint"),
            rot ? end.y : end.x, rot ? end.x : (1 - end.y));
        glUniform1fv(uniform("stops"), static_cast<GLsizei>(count), sortedStops.data());
        glUniform1i(uniform("stopAt"), static_cast<GLint>(count));
        glUniform4fv(uniform("colors"), static_cast<GLsizei>(count), sortedColors.data());
        glUniform1fv(uniform("u_imageSlots"), static_cast<GLsizei>(count), sortedSlots.data());
    } else {
        std::vector<float> positions(count * 2);
        std::vector<GLfloat> flatColors(count * 4);
        for (size_t i = 0; i < count; ++i) {
            CCPoint pos = config.points[i].pos;
            positions[i * 2] = rot ? pos.y : pos.x;
            positions[i * 2 + 1] = rot ? pos.x : (1 - pos.y);
            flatColors[i * 4] = colors[i].r;
            flatColors[i * 4 + 1] = colors[i].g;
            flatColors[i * 4 + 2] = colors[i].b;
            flatColors[i * 4 + 3] = colors[i].a;
        }

        glUniform2fv(uniform("positions"), static_cast<GLsizei>(count), positions.data());
        glUniform1i(uniform("stopAt"), static_cast<GLint>(count));
        glUniform4fv(uniform("colors"), static_cast<GLsizei>(count), flatColors.data());
        glUniform1fv(uniform("u_imageSlots"), static_cast<GLsizei>(count), imageSlots.data());
    }
}

void GradientUtils::patchBatchNode(CCSpriteBatchNode* node) {
    if (!node) return;

    // Lend this batch node the no-op draw behavior so gradient sprites inside
    // it are shaded by their own programs instead of the batch cache.
    static void* noDrawVTable = []() -> void* {
        FakeSpriteBatchNode standIn;
        return *reinterpret_cast<void**>(&standIn);
    }();

    *reinterpret_cast<void**>(node) = noDrawVTable;
}

void GradientUtils::hideSprite(CCSprite* sprite) {
    auto cache = CCShaderCache::sharedShaderCache();

    if (CCGLProgram* known = cache->programForKey("invis-shader"_spr)) {
        sprite->setShaderProgram(known);
        return;
    }

    static constexpr char const* vert = R"(
        attribute vec4 a_position; attribute vec2 a_texCoord; attribute vec4 a_color;
        #ifdef GL_ES
        varying lowp vec4 v_fragmentColor; varying mediump vec2 v_texCoord;
        #else
        varying vec4 v_fragmentColor; varying vec2 v_texCoord;
        #endif
        void main() {
            gl_Position = CC_MVPMatrix * a_position;
            v_fragmentColor = a_color; v_texCoord = a_texCoord;
        }
    )";
    static constexpr char const* frag = R"(
        #ifdef GL_ES
        precision mediump float;
        #endif
        void main() { gl_FragColor = vec4(0.0); }
    )";

    CCGLProgram* shader = compileProgram(vert, frag);
    if (!shader) return;
    shader->retain();
    cache->addProgram(shader, "invis-shader"_spr);

    sprite->setShaderProgram(shader);
}
