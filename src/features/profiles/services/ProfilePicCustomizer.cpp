#include "ProfilePicCustomizer.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/string.hpp>

using namespace geode::prelude;
using namespace cocos2d;

namespace {
using NamedSprite = std::pair<std::string, std::string>;

bool canUseSpriteFrame(std::string const& frameName) {
    return paimon::SpriteHelper::safeCreateWithFrameName(frameName.c_str()) != nullptr;
}

void addDecorationIfAvailable(std::vector<NamedSprite>& out, char const* stem, char const* label) {
    auto frameName = std::string(stem) + ".png";
    if (canUseSpriteFrame(frameName)) {
        out.emplace_back(std::move(frameName), label);
    }
}
}

ProfilePicCustomizer& ProfilePicCustomizer::get() {
    static ProfilePicCustomizer inst;
    if (!inst.m_loaded) {
        inst.load();
        inst.m_loaded = true;
    }
    return inst;
}

ProfilePicCustomizer::ProfilePicCustomizer() {}

ProfilePicConfig ProfilePicCustomizer::getConfig() const {
    return m_config;
}

void ProfilePicCustomizer::setConfig(ProfilePicConfig const& config) {
    m_config = config;
}

void ProfilePicCustomizer::save() {
    auto savePath = Mod::get()->getSaveDir() / "profile_pic_config.json";

    matjson::Value root;
    root["scaleX"] = m_config.scaleX;
    root["scaleY"] = m_config.scaleY;
    root["size"] = m_config.size;
    root["rotation"] = m_config.rotation;
    root["imageZoom"] = m_config.imageZoom;
    root["imageRotation"] = m_config.imageRotation;
    root["imageOffsetX"] = m_config.imageOffsetX;
    root["imageOffsetY"] = m_config.imageOffsetY;
    root["frameEnabled"] = m_config.frameEnabled;
    root["stencilSprite"] = m_config.stencilSprite;
    root["offsetX"] = m_config.offsetX;
    root["offsetY"] = m_config.offsetY;

    // Frame
    matjson::Value frameObj;
    frameObj["spriteFrame"] = m_config.frame.spriteFrame;
    frameObj["colorR"] = static_cast<int>(m_config.frame.color.r);
    frameObj["colorG"] = static_cast<int>(m_config.frame.color.g);
    frameObj["colorB"] = static_cast<int>(m_config.frame.color.b);
    frameObj["opacity"] = m_config.frame.opacity;
    frameObj["thickness"] = m_config.frame.thickness;
    frameObj["offsetX"] = m_config.frame.offsetX;
    frameObj["offsetY"] = m_config.frame.offsetY;
    root["frame"] = frameObj;

    // Decorations
    matjson::Value decoArray = matjson::Value::array();
    for (auto const& deco : m_config.decorations) {
        matjson::Value d;
        d["spriteName"] = deco.spriteName;
        d["posX"] = deco.posX;
        d["posY"] = deco.posY;
        d["scale"] = deco.scale;
        d["rotation"] = deco.rotation;
        d["colorR"] = static_cast<int>(deco.color.r);
        d["colorG"] = static_cast<int>(deco.color.g);
        d["colorB"] = static_cast<int>(deco.color.b);
        d["opacity"] = deco.opacity;
        d["flipX"] = deco.flipX;
        d["flipY"] = deco.flipY;
        d["zOrder"] = deco.zOrder;
        decoArray.push(std::move(d));
    }
    root["decorations"] = decoArray;

    auto res = file::writeStringSafe(savePath, root.dump());
    if (!res) {
        log::error("[ProfilePicCustomizer] Failed to save config: {}", res.unwrapErr());
        return;
    }

    log::info("[ProfilePicCustomizer] Config saved");
}

void ProfilePicCustomizer::load() {
    auto savePath = Mod::get()->getSaveDir() / "profile_pic_config.json";

    auto contentRes = file::readString(savePath);
    if (!contentRes) return;

    auto parsed = matjson::parse(contentRes.unwrap());
    if (!parsed.isOk()) return;
    auto root = parsed.unwrap();

    if (root.contains("scaleX")) m_config.scaleX = root["scaleX"].asDouble().unwrapOr(1.0);
    if (root.contains("scaleY")) m_config.scaleY = root["scaleY"].asDouble().unwrapOr(1.0);
    if (root.contains("size")) m_config.size = root["size"].asDouble().unwrapOr(120.0);
    if (root.contains("rotation")) m_config.rotation = root["rotation"].asDouble().unwrapOr(0.0);
    if (root.contains("imageZoom")) m_config.imageZoom = root["imageZoom"].asDouble().unwrapOr(1.0);
    if (root.contains("imageRotation")) m_config.imageRotation = root["imageRotation"].asDouble().unwrapOr(0.0);
    if (root.contains("imageOffsetX")) m_config.imageOffsetX = root["imageOffsetX"].asDouble().unwrapOr(0.0);
    if (root.contains("imageOffsetY")) m_config.imageOffsetY = root["imageOffsetY"].asDouble().unwrapOr(0.0);
    if (root.contains("frameEnabled")) m_config.frameEnabled = root["frameEnabled"].asBool().unwrapOr(false);
    if (root.contains("stencilSprite")) m_config.stencilSprite = root["stencilSprite"].asString().unwrapOr("circle");
    if (root.contains("offsetX")) m_config.offsetX = root["offsetX"].asDouble().unwrapOr(0.0);
    if (root.contains("offsetY")) m_config.offsetY = root["offsetY"].asDouble().unwrapOr(0.0);

    // Frame
    if (root.contains("frame")) {
        auto& f = root["frame"];
        if (f.contains("spriteFrame")) m_config.frame.spriteFrame = f["spriteFrame"].asString().unwrapOr("");
        if (f.contains("colorR")) m_config.frame.color.r = f["colorR"].asInt().unwrapOr(255);
        if (f.contains("colorG")) m_config.frame.color.g = f["colorG"].asInt().unwrapOr(255);
        if (f.contains("colorB")) m_config.frame.color.b = f["colorB"].asInt().unwrapOr(255);
        if (f.contains("opacity")) m_config.frame.opacity = f["opacity"].asDouble().unwrapOr(255.0);
        if (f.contains("thickness")) m_config.frame.thickness = f["thickness"].asDouble().unwrapOr(4.0);
        if (f.contains("offsetX")) m_config.frame.offsetX = f["offsetX"].asDouble().unwrapOr(0.0);
        if (f.contains("offsetY")) m_config.frame.offsetY = f["offsetY"].asDouble().unwrapOr(0.0);
    }

    // Decorations
    if (root.contains("decorations") && root["decorations"].isArray()) {
        m_config.decorations.clear();
        auto decoArr = root["decorations"].asArray();
        if (decoArr.isOk()) {
        for (auto& d : decoArr.unwrap()) {
            PicDecoration deco;
            deco.spriteName = d["spriteName"].asString().unwrapOr("");
            deco.posX = d["posX"].asDouble().unwrapOr(0.0);
            deco.posY = d["posY"].asDouble().unwrapOr(0.0);
            deco.scale = d["scale"].asDouble().unwrapOr(1.0);
            deco.rotation = d["rotation"].asDouble().unwrapOr(0.0);
            deco.color.r = d["colorR"].asInt().unwrapOr(255);
            deco.color.g = d["colorG"].asInt().unwrapOr(255);
            deco.color.b = d["colorB"].asInt().unwrapOr(255);
            deco.opacity = d["opacity"].asDouble().unwrapOr(255.0);
            deco.flipX = d["flipX"].asBool().unwrapOr(false);
            deco.flipY = d["flipY"].asBool().unwrapOr(false);
            deco.zOrder = d["zOrder"].asInt().unwrapOr(0);
            if (!deco.spriteName.empty()) {
                m_config.decorations.push_back(deco);
            }
        }
        } // if (decoArr.isOk())
    }

    log::info("[ProfilePicCustomizer] Config loaded ({} decorations)", m_config.decorations.size());
}

std::vector<std::pair<std::string, std::string>> ProfilePicCustomizer::getAvailableFrames() {
    return {
        {"GJ_square01.png", "Square"},
        {"GJ_square02.png", "Square Dark"},
        {"GJ_square03.png", "Square Blue"},
        {"GJ_square04.png", "Square Green"},
        {"GJ_square05.png", "Square Purple"},
        {"GJ_square06.png", "Square Brown"},
        {"GJ_square07.png", "Square Pink"},
        {"square02b_001.png", "Rounded"},
        {"GJ_button_01.png", "Green Button"},
        {"GJ_button_02.png", "Pink Button"},
        {"GJ_button_03.png", "Blue Button"},
        {"GJ_button_04.png", "Gray Button"},
        {"GJ_button_05.png", "Red Button"},
        {"GJ_button_06.png", "Cyan Button"},
    };
}

std::vector<std::pair<std::string, std::string>> ProfilePicCustomizer::getAvailableStencils() {
    return {
        {"circle", "Circle"},
        {"rounded", "Rounded"},
        {"square", "Square"},
        {"rectangle", "Rect"},
        {"pill", "Pill"},
        {"triangle", "Triangle"},
        {"diamond", "Diamond"},
        {"pentagon", "Pentagon"},
        {"hexagon", "Hexagon"},
        {"octagon", "Octagon"},
        {"arch", "Arch"},
        {"teardrop", "Drop"},
        {"cloud", "Cloud"},
        {"cross", "Cross"},
        {"moon", "Moon"},
        {"shield", "Shield"},
        {"badge", "Badge"},
        {"star", "Star 5"},
        {"star6", "Star 6"},
        {"heart", "Heart"},
    };
}

std::vector<std::pair<std::string, std::string>> ProfilePicCustomizer::getAvailableDecorations() {
    std::vector<std::pair<std::string, std::string>> decorations;
    decorations.reserve(31);

    // Estrellas
    addDecorationIfAvailable(decorations, "star_small01_001", "Star Small");
    addDecorationIfAvailable(decorations, "star_small02_001", "Star Small 2");
    addDecorationIfAvailable(decorations, "star_small03_001", "Star Small 3");
    addDecorationIfAvailable(decorations, "GJ_bigStar_001", "Big Star");
    addDecorationIfAvailable(decorations, "GJ_sStar_001", "Small Star");
    addDecorationIfAvailable(decorations, "diamond_small01_001", "Diamond");
    addDecorationIfAvailable(decorations, "diamond_small02_001", "Diamond 2");
    addDecorationIfAvailable(decorations, "currencyDiamondIcon_001", "Gem Diamond");
    addDecorationIfAvailable(decorations, "currencyOrbIcon_001", "Orb");

    // Logros
    addDecorationIfAvailable(decorations, "GJ_sRecentIcon_001", "Recent");
    addDecorationIfAvailable(decorations, "GJ_sTrendingIcon_001", "Trending");
    addDecorationIfAvailable(decorations, "GJ_sMagicIcon_001", "Magic");
    addDecorationIfAvailable(decorations, "GJ_sAwardedIcon_001", "Awarded");
    addDecorationIfAvailable(decorations, "GJ_sFeaturedIcon_001", "Featured");
    addDecorationIfAvailable(decorations, "GJ_sHallOfFameIcon_001", "Hall of Fame");

    // Flechas
    addDecorationIfAvailable(decorations, "GJ_arrow_01_001", "Arrow Right");
    addDecorationIfAvailable(decorations, "GJ_arrow_02_001", "Arrow Left");
    addDecorationIfAvailable(decorations, "GJ_arrow_03_001", "Arrow Up");

    // Badges
    addDecorationIfAvailable(decorations, "modBadge_01_001", "Mod Badge");
    addDecorationIfAvailable(decorations, "modBadge_02_001", "Elder Mod Badge");
    addDecorationIfAvailable(decorations, "modBadge_03_001", "Leaderboard Badge");

    // Efectos
    addDecorationIfAvailable(decorations, "particle_01_001", "Particle Circle");
    addDecorationIfAvailable(decorations, "particle_02_001", "Particle Square");
    addDecorationIfAvailable(decorations, "particle_03_001", "Particle Triangle");
    addDecorationIfAvailable(decorations, "fireEffect_01_001", "Fire");

    // Dificultad
    addDecorationIfAvailable(decorations, "diffIcon_01_btn_001", "Easy");
    addDecorationIfAvailable(decorations, "diffIcon_02_btn_001", "Normal");
    addDecorationIfAvailable(decorations, "diffIcon_03_btn_001", "Hard");
    addDecorationIfAvailable(decorations, "diffIcon_04_btn_001", "Harder");
    addDecorationIfAvailable(decorations, "diffIcon_05_btn_001", "Insane");
    addDecorationIfAvailable(decorations, "diffIcon_06_btn_001", "Demon");

    // Locks
    addDecorationIfAvailable(decorations, "GJ_lock_001", "Lock");
    addDecorationIfAvailable(decorations, "GJ_completesIcon_001", "Complete");
    addDecorationIfAvailable(decorations, "GJ_deleteIcon_001", "Delete");

    // Social
    addDecorationIfAvailable(decorations, "GJ_heart_01", "Heart");
    addDecorationIfAvailable(decorations, "gj_heartOn_001", "Heart On");

    // Misc
    addDecorationIfAvailable(decorations, "GJ_infoIcon_001", "Info");
    addDecorationIfAvailable(decorations, "GJ_playBtn2_001", "Play");
    addDecorationIfAvailable(decorations, "GJ_pauseBtn_001", "Pause");
    addDecorationIfAvailable(decorations, "edit_eRotateBtn_001", "Rotate");
    addDecorationIfAvailable(decorations, "edit_eScaleBtn_001", "Scale");

    return decorations;
}

std::vector<DecorationCategory> ProfilePicCustomizer::getDecorationCategories() {
    std::vector<DecorationCategory> cats;

    auto pushCat = [&](std::string const& id, std::string const& name,
                       std::initializer_list<std::pair<char const*, char const*>> items) {
        DecorationCategory cat;
        cat.id = id;
        cat.displayName = name;
        for (auto const& [stem, label] : items) {
            auto frameName = std::string(stem) + ".png";
            if (canUseSpriteFrame(frameName)) {
                cat.decorations.emplace_back(std::move(frameName), label);
            }
        }
        if (!cat.decorations.empty()) cats.push_back(std::move(cat));
    };

    pushCat("stars", "Stars", {
        {"star_small01_001", "Small 1"},
        {"star_small02_001", "Small 2"},
        {"star_small03_001", "Small 3"},
        {"GJ_bigStar_001", "Big Star"},
        {"GJ_sStar_001", "Star"},
    });

    pushCat("gems", "Gems", {
        {"diamond_small01_001", "Diamond 1"},
        {"diamond_small02_001", "Diamond 2"},
        {"currencyDiamondIcon_001", "Gem"},
        {"currencyOrbIcon_001", "Orb"},
    });

    pushCat("icons", "Icons", {
        {"GJ_sRecentIcon_001", "Recent"},
        {"GJ_sTrendingIcon_001", "Trending"},
        {"GJ_sMagicIcon_001", "Magic"},
        {"GJ_sAwardedIcon_001", "Awarded"},
        {"GJ_sFeaturedIcon_001", "Featured"},
        {"GJ_sHallOfFameIcon_001", "Hall of Fame"},
        {"GJ_lock_001", "Lock"},
        {"GJ_completesIcon_001", "Complete"},
        {"GJ_infoIcon_001", "Info"},
        {"GJ_playBtn2_001", "Play"},
        {"GJ_pauseBtn_001", "Pause"},
    });

    pushCat("difficulty", "Difficulty", {
        {"diffIcon_01_btn_001", "Easy"},
        {"diffIcon_02_btn_001", "Normal"},
        {"diffIcon_03_btn_001", "Hard"},
        {"diffIcon_04_btn_001", "Harder"},
        {"diffIcon_05_btn_001", "Insane"},
        {"diffIcon_06_btn_001", "Demon"},
    });

    pushCat("badges", "Badges", {
        {"modBadge_01_001", "Mod"},
        {"modBadge_02_001", "Elder Mod"},
        {"modBadge_03_001", "Leaderboard"},
    });

    pushCat("effects", "Effects", {
        {"particle_01_001", "Particle Circle"},
        {"particle_02_001", "Particle Square"},
        {"particle_03_001", "Particle Triangle"},
        {"fireEffect_01_001", "Fire"},
    });

    pushCat("arrows", "Arrows", {
        {"GJ_arrow_01_001", "Right"},
        {"GJ_arrow_02_001", "Left"},
        {"GJ_arrow_03_001", "Up"},
        {"edit_eRotateBtn_001", "Rotate"},
        {"edit_eScaleBtn_001", "Scale"},
    });

    pushCat("social", "Social", {
        {"GJ_heart_01", "Heart"},
        {"gj_heartOn_001", "Heart On"},
        {"GJ_deleteIcon_001", "Delete"},
    });

    return cats;
}

std::vector<std::pair<std::string, cocos2d::ccColor3B>> ProfilePicCustomizer::getColorPalette() {
    return {
        {"White",   {255, 255, 255}},
        {"Black",   {30, 30, 30}},
        {"Red",     {255, 70, 70}},
        {"Orange",  {255, 150, 0}},
        {"Yellow",  {255, 220, 50}},
        {"Lime",    {170, 255, 80}},
        {"Green",   {50, 230, 100}},
        {"Cyan",    {0, 220, 220}},
        {"Blue",    {60, 120, 255}},
        {"Purple",  {160, 70, 255}},
        {"Pink",    {255, 100, 200}},
        {"Gold",    {255, 215, 0}},
        {"Silver",  {200, 200, 210}},
        {"Crimson", {180, 30, 60}},
        {"Teal",    {0, 160, 160}},
        {"Coral",   {255, 127, 100}},
    };
}

std::vector<ProfilePicPreset> ProfilePicCustomizer::getPresets() {
    std::vector<ProfilePicPreset> presets;

    // Clasico
    {
        ProfilePicPreset p;
        p.id = "classic";
        p.displayName = "Classic";
        p.config.stencilSprite = "circle";
        p.config.frameEnabled = false;
        presets.push_back(p);
    }

    // Real
    {
        ProfilePicPreset p;
        p.id = "royal";
        p.displayName = "Royal";
        p.config.stencilSprite = "hexagon";
        p.config.frameEnabled = true;
        p.config.frame.color = {255, 215, 0};
        p.config.frame.thickness = 5.f;
        p.config.frame.opacity = 255.f;

        auto addDeco = [&](char const* name, float ang, float scale, float rot, cocos2d::ccColor3B col) {
            PicDecoration d;
            d.spriteName = name;
            float r = 0.9f;
            d.posX = cosf(ang) * r;
            d.posY = sinf(ang) * r;
            d.scale = scale;
            d.rotation = rot;
            d.color = col;
            d.zOrder = static_cast<int>(p.config.decorations.size());
            p.config.decorations.push_back(d);
        };
        addDeco("GJ_bigStar_001.png", 1.5708f,  0.45f, 0.f, {255, 215, 0});
        addDeco("GJ_sStar_001.png",  -1.5708f,  0.55f, 0.f, {255, 215, 0});
        addDeco("GJ_sStar_001.png",   0.f,      0.4f,  0.f, {255, 215, 0});
        addDeco("GJ_sStar_001.png",   3.14159f, 0.4f,  0.f, {255, 215, 0});
        presets.push_back(p);
    }

    // Lindo
    {
        ProfilePicPreset p;
        p.id = "cute";
        p.displayName = "Cute";
        p.config.stencilSprite = "heart";
        p.config.frameEnabled = true;
        p.config.frame.color = {255, 120, 200};
        p.config.frame.thickness = 4.f;
        p.config.scaleY = 1.1f;

        PicDecoration d;
        d.spriteName = "gj_heartOn_001.png";
        d.posX = 0.75f; d.posY = 0.55f; d.scale = 0.45f; d.color = {255, 150, 200};
        d.zOrder = 0;
        p.config.decorations.push_back(d);
        d.posX = -0.75f; d.posY = -0.2f; d.scale = 0.35f; d.zOrder = 1;
        p.config.decorations.push_back(d);
        presets.push_back(p);
    }

    // Minimalista
    {
        ProfilePicPreset p;
        p.id = "minimal";
        p.displayName = "Minimal";
        p.config.stencilSprite = "circle";
        p.config.frameEnabled = false;
        presets.push_back(p);
    }

    // Gamer
    {
        ProfilePicPreset p;
        p.id = "gamer";
        p.displayName = "Gamer";
        p.config.stencilSprite = "square";
        p.config.frameEnabled = true;
        p.config.frame.color = {0, 220, 220};
        p.config.frame.thickness = 3.5f;

        PicDecoration d;
        d.spriteName = "diffIcon_06_btn_001.png";
        d.posX = 0.85f; d.posY = -0.85f; d.scale = 0.55f; d.zOrder = 0;
        p.config.decorations.push_back(d);
        presets.push_back(p);
    }

    // Estrella
    {
        ProfilePicPreset p;
        p.id = "starlight";
        p.displayName = "Starlight";
        p.config.stencilSprite = "star";
        p.config.scaleX = 1.1f;
        p.config.scaleY = 1.1f;
        p.config.frameEnabled = true;
        p.config.frame.color = {255, 230, 100};
        p.config.frame.thickness = 3.f;
        presets.push_back(p);
    }

    // Diamante
    {
        ProfilePicPreset p;
        p.id = "diamond";
        p.displayName = "Diamond";
        p.config.stencilSprite = "diamond";
        p.config.frameEnabled = true;
        p.config.frame.color = {120, 220, 255};
        p.config.frame.thickness = 3.f;

        PicDecoration d;
        d.spriteName = "diamond_small01_001.png";
        d.posX = 0.f; d.posY = 1.0f; d.scale = 0.6f; d.color = {150, 230, 255};
        p.config.decorations.push_back(d);
        presets.push_back(p);
    }

    return presets;
}
