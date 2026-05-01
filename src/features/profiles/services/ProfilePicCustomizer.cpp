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

    // Font
    root["profileFont"] = m_config.profileFont;

    // Icon Mode
    root["onlyIconMode"] = m_config.onlyIconMode;
    matjson::Value iconObj;
    iconObj["iconId"] = m_config.iconConfig.iconId;
    iconObj["iconType"] = m_config.iconConfig.iconType;
    iconObj["color1R"] = static_cast<int>(m_config.iconConfig.color1.r);
    iconObj["color1G"] = static_cast<int>(m_config.iconConfig.color1.g);
    iconObj["color1B"] = static_cast<int>(m_config.iconConfig.color1.b);
    iconObj["color2R"] = static_cast<int>(m_config.iconConfig.color2.r);
    iconObj["color2G"] = static_cast<int>(m_config.iconConfig.color2.g);
    iconObj["color2B"] = static_cast<int>(m_config.iconConfig.color2.b);
    iconObj["glowEnabled"] = m_config.iconConfig.glowEnabled;
    iconObj["glowColorR"] = static_cast<int>(m_config.iconConfig.glowColor.r);
    iconObj["glowColorG"] = static_cast<int>(m_config.iconConfig.glowColor.g);
    iconObj["glowColorB"] = static_cast<int>(m_config.iconConfig.glowColor.b);
    iconObj["colorSource"] = static_cast<int>(m_config.iconConfig.colorSource);
    iconObj["glowColorSource"] = static_cast<int>(m_config.iconConfig.glowColorSource);
    iconObj["scale"] = m_config.iconConfig.scale;
    iconObj["animationType"] = m_config.iconConfig.animationType;
    iconObj["animationSpeed"] = m_config.iconConfig.animationSpeed;
    iconObj["animationAmount"] = m_config.iconConfig.animationAmount;
    iconObj["iconImageEnabled"] = m_config.iconConfig.iconImageEnabled;
    iconObj["iconImagePath"] = m_config.iconConfig.iconImagePath;
    iconObj["iconImageScale"] = m_config.iconConfig.iconImageScale;
    root["iconConfig"] = iconObj;

    // Custom Icons
    matjson::Value customIconsArray = matjson::Value::array();
    for (auto const& ci : m_config.customIcons) {
        matjson::Value ciObj;
        ciObj["spriteName"] = ci.spriteName;
        ciObj["path"] = ci.path;
        ciObj["isModAsset"] = ci.isModAsset;
        customIconsArray.push(std::move(ciObj));
    }
    root["customIcons"] = customIconsArray;
    root["selectedCustomIconIndex"] = m_config.selectedCustomIconIndex;

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

    // Font
    if (root.contains("profileFont")) m_config.profileFont = root["profileFont"].asString().unwrapOr("goldFont.fnt");

    // Icon Mode
    if (root.contains("onlyIconMode")) m_config.onlyIconMode = root["onlyIconMode"].asBool().unwrapOr(false);
    if (root.contains("iconConfig")) {
        auto& ic = root["iconConfig"];
        if (ic.contains("iconId")) m_config.iconConfig.iconId = ic["iconId"].asInt().unwrapOr(0);
        if (ic.contains("iconType")) m_config.iconConfig.iconType = ic["iconType"].asInt().unwrapOr(0);
        if (ic.contains("color1R")) m_config.iconConfig.color1.r = ic["color1R"].asInt().unwrapOr(255);
        if (ic.contains("color1G")) m_config.iconConfig.color1.g = ic["color1G"].asInt().unwrapOr(255);
        if (ic.contains("color1B")) m_config.iconConfig.color1.b = ic["color1B"].asInt().unwrapOr(255);
        if (ic.contains("color2R")) m_config.iconConfig.color2.r = ic["color2R"].asInt().unwrapOr(255);
        if (ic.contains("color2G")) m_config.iconConfig.color2.g = ic["color2G"].asInt().unwrapOr(255);
        if (ic.contains("color2B")) m_config.iconConfig.color2.b = ic["color2B"].asInt().unwrapOr(255);
        if (ic.contains("glowEnabled")) m_config.iconConfig.glowEnabled = ic["glowEnabled"].asBool().unwrapOr(false);
        if (ic.contains("glowColorR")) m_config.iconConfig.glowColor.r = ic["glowColorR"].asInt().unwrapOr(255);
        if (ic.contains("glowColorG")) m_config.iconConfig.glowColor.g = ic["glowColorG"].asInt().unwrapOr(255);
        if (ic.contains("glowColorB")) m_config.iconConfig.glowColor.b = ic["glowColorB"].asInt().unwrapOr(0);
        if (ic.contains("colorSource")) m_config.iconConfig.colorSource = static_cast<IconColorSource>(ic["colorSource"].asInt().unwrapOr(0));
        if (ic.contains("glowColorSource")) m_config.iconConfig.glowColorSource = static_cast<IconColorSource>(ic["glowColorSource"].asInt().unwrapOr(0));
        if (ic.contains("scale")) m_config.iconConfig.scale = ic["scale"].asDouble().unwrapOr(1.0);
        if (ic.contains("animationType")) m_config.iconConfig.animationType = ic["animationType"].asInt().unwrapOr(0);
        if (ic.contains("animationSpeed")) m_config.iconConfig.animationSpeed = ic["animationSpeed"].asDouble().unwrapOr(1.0);
        if (ic.contains("animationAmount")) m_config.iconConfig.animationAmount = ic["animationAmount"].asDouble().unwrapOr(1.0);
        if (ic.contains("iconImageEnabled")) m_config.iconConfig.iconImageEnabled = ic["iconImageEnabled"].asBool().unwrapOr(false);
        if (ic.contains("iconImagePath")) m_config.iconConfig.iconImagePath = ic["iconImagePath"].asString().unwrapOr("");
        if (ic.contains("iconImageScale")) m_config.iconConfig.iconImageScale = ic["iconImageScale"].asDouble().unwrapOr(1.0);
    }

    // Custom Icons
    if (root.contains("customIcons") && root["customIcons"].isArray()) {
        m_config.customIcons.clear();
        auto ciArr = root["customIcons"].asArray();
        if (ciArr.isOk()) {
            for (auto& ci : ciArr.unwrap()) {
                PicCustomIcon customIcon;
                customIcon.spriteName = ci["spriteName"].asString().unwrapOr("");
                customIcon.path = ci["path"].asString().unwrapOr("");
                customIcon.isModAsset = ci["isModAsset"].asBool().unwrapOr(false);
                if (!customIcon.spriteName.empty()) {
                    m_config.customIcons.push_back(customIcon);
                }
            }
        }
    }
    if (root.contains("selectedCustomIconIndex")) m_config.selectedCustomIconIndex = root["selectedCustomIconIndex"].asInt().unwrapOr(-1);

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
        {"GJ_fullStar_001", "Full Star"},
        {"GJ_emptyStar_001", "Empty Star"},
    });

    pushCat("gems", "Gems", {
        {"diamond_small01_001", "Diamond 1"},
        {"diamond_small02_001", "Diamond 2"},
        {"currencyDiamondIcon_001", "Gem"},
        {"currencyOrbIcon_001", "Orb"},
        {"currencyMoonIcon_001", "Moon"},
        {"secretCoin_01_001", "Secret Coin"},
        {"secretCoin_b_01_001", "Blue Coin"},
        {"usercoin_01_001", "User Coin"},
        {"usercoin_unverified_001", "Unverified"},
        {"usercoin_shadow_001", "Shadow"},
        {"key_01_001", "Key"},
        {"key_2_01_001", "Key 2"},
        {"key_3_01_001", "Key 3"},
    });

    pushCat("icons", "Icons", {
        {"GJ_sRecentIcon_001", "Recent"},
        {"GJ_sTrendingIcon_001", "Trending"},
        {"GJ_sMagicIcon_001", "Magic"},
        {"GJ_sAwardedIcon_001", "Awarded"},
        {"GJ_sFeaturedIcon_001", "Featured"},
        {"GJ_sHallOfFameIcon_001", "Hall of Fame"},
        {"GJ_lock_001", "Lock"},
        {"GJ_lock_open_001", "Unlock"},
        {"GJ_completesIcon_001", "Complete"},
        {"GJ_infoIcon_001", "Info"},
        {"GJ_playBtn2_001", "Play"},
        {"GJ_pauseBtn_001", "Pause"},
        {"GJ_likesIcon_001", "Likes"},
        {"GJ_downloadsIcon_001", "Downloads"},
        {"GJ_commentsIcon_001", "Comments"},
        {"GJ_check_001", "Check"},
        {"GJ_x_001", "X"},
        {"GJ_plus_001", "Plus"},
        {"GJ_profileButton_001", "Profile"},
        {"GJ_leaderboardsBtn_001", "Leaderboard"},
        {"GJ_creatorBtn_001", "Creator"},
        {"GJ_searchBtn_001", "Search"},
        {"GJ_menuBtn_001", "Menu"},
        {"GJ_garageBtn_001", "Garage"},
        {"GJ_optionsBtn_001", "Settings"},
        {"GJ_storeBtn_001", "Store"},
        {"GJ_achievements_001", "Achievements"},
        {"GJ_statsBtn_001", "Stats"},
        {"GJ_dailyBtn_001", "Daily"},
        {"GJ_weeklyBtn_001", "Weekly"},
        {"GJ_rateStarBtn_001", "Rate Star"},
        {"GJ_rateDemonBtn_001", "Rate Demon"},
    });

    pushCat("difficulty", "Difficulty", {
        {"diffIcon_01_btn_001", "Easy"},
        {"diffIcon_02_btn_001", "Normal"},
        {"diffIcon_03_btn_001", "Hard"},
        {"diffIcon_04_btn_001", "Harder"},
        {"diffIcon_05_btn_001", "Insane"},
        {"diffIcon_06_btn_001", "Demon"},
        {"diffIcon_07_btn_001", "Easy Demon"},
        {"diffIcon_08_btn_001", "Medium Demon"},
        {"diffIcon_09_btn_001", "Hard Demon"},
        {"diffIcon_10_btn_001", "Insane Demon"},
        {"diffIcon_11_btn_001", "Extreme Demon"},
        {"diffIcon_auto_btn_001", "Auto"},
        {"diffIcon_demon_btn_001", "Demon"},
        {"diffIcon_na_btn_001", "N/A"},
        {"diffIcon_01_btn2_001", "Easy 2"},
        {"diffIcon_02_btn2_001", "Normal 2"},
        {"diffIcon_03_btn2_001", "Hard 2"},
        {"diffIcon_04_btn2_001", "Harder 2"},
        {"diffIcon_05_btn2_001", "Insane 2"},
        {"diffIcon_06_btn2_001", "Demon 2"},
        {"diffIcon_07_btn2_001", "Easy D2"},
        {"diffIcon_08_btn2_001", "Medium D2"},
        {"diffIcon_09_btn2_001", "Hard D2"},
        {"diffIcon_10_btn2_001", "Insane D2"},
        {"diffIcon_11_btn2_001", "Extreme D2"},
    });

    pushCat("badges", "Badges", {
        {"modBadge_01_001", "Mod"},
        {"modBadge_02_001", "Elder Mod"},
        {"modBadge_03_001", "Leaderboard"},
        {"modBadge_04_001", "Bot"},
        {"modBadge_05_001", "Dev"},
    });

    pushCat("effects", "Effects", {
        {"particle_01_001", "Particle Circle"},
        {"particle_02_001", "Particle Square"},
        {"particle_03_001", "Particle Triangle"},
        {"fireEffect_01_001", "Fire"},
        {"fireEffect_02_001", "Fire 2"},
        {"fireEffect_03_001", "Fire 3"},
        {"deatheffect_01_001", "Death 1"},
        {"deatheffect_02_001", "Death 2"},
        {"deatheffect_03_001", "Death 3"},
        {"deatheffect_04_001", "Death 4"},
        {"deatheffect_05_001", "Death 5"},
        {"deatheffect_06_001", "Death 6"},
        {"deatheffect_07_001", "Death 7"},
        {"deatheffect_08_001", "Death 8"},
        {"deatheffect_09_001", "Death 9"},
        {"deatheffect_10_001", "Death 10"},
        {"deatheffect_11_001", "Death 11"},
        {"deatheffect_12_001", "Death 12"},
        {"deatheffect_13_001", "Death 13"},
        {"deatheffect_14_001", "Death 14"},
        {"deatheffect_15_001", "Death 15"},
        {"deatheffect_16_001", "Death 16"},
        {"deatheffect_17_001", "Death 17"},
        {"deatheffect_18_001", "Death 18"},
        {"deatheffect_19_001", "Death 19"},
        {"deatheffect_20_001", "Death 20"},
        {"portalEffect_01_001", "Portal"},
        {"portalEffect_02_001", "Portal 2"},
    });

    pushCat("arrows", "Arrows", {
        {"GJ_arrow_01_001", "Right"},
        {"GJ_arrow_02_001", "Left"},
        {"GJ_arrow_03_001", "Up"},
        {"GJ_arrow_04_001", "Down"},
        {"edit_eRotateBtn_001", "Rotate"},
        {"edit_eScaleBtn_001", "Scale"},
        {"edit_eMoveBtn_001", "Move"},
        {"edit_eCopyBtn_001", "Copy"},
        {"edit_ePasteBtn_001", "Paste"},
        {"edit_eUndoBtn_001", "Undo"},
        {"edit_eRedoBtn_001", "Redo"},
        {"edit_eDeleteBtn_001", "Delete"},
        {"edit_eLinkBtn_001", "Link"},
        {"edit_eUnlinkBtn_001", "Unlink"},
        {"edit_eOrderUpBtn_001", "Order Up"},
        {"edit_eOrderDownBtn_001", "Order Down"},
        {"edit_eGroupBtn_001", "Group"},
        {"edit_eUngroupBtn_001", "Ungroup"},
    });

    pushCat("social", "Social", {
        {"GJ_heart_01", "Heart"},
        {"gj_heartOn_001", "Heart On"},
        {"GJ_heartOff_001", "Heart Off"},
        {"GJ_deleteIcon_001", "Delete"},
        {"GJ_likeBtn_001", "Like"},
        {"GJ_dislikeBtn_001", "Dislike"},
        {"GJ_reportBtn_001", "Report"},
        {"GJ_messageBtn_001", "Message"},
        {"GJ_friendRequestBtn_001", "Friend"},
    });

    return cats;
}

std::vector<std::pair<std::string, cocos2d::ccColor3B>> ProfilePicCustomizer::getColorPalette() {
    return {
        {"White", {255, 255, 255}},
        {"Black", {30, 30, 30}},
        {"Red",   {255, 70, 70}},
        {"Blue",  {60, 120, 255}},
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

std::vector<std::pair<int, std::string>> ProfilePicCustomizer::getAvailableGameIcons() {
    return {
        {0, "Cube"},
        {1, "Ship"},
        {2, "Ball"},
        {3, "UFO"},
        {4, "Wave"},
        {5, "Robot"},
        {6, "Spider"},
        {7, "Swing"},
        {8, "Jetpack"},
        {9, "Explosion"},
        {10, "Cube Green"},
        {11, "Ship Purple"},
        {12, "Ball Blue"},
        {13, "UFO Orange"},
        {14, "Wave Pink"},
        {15, "Robot Red"},
        {16, "Spider Cyan"},
        {17, "Swing Yellow"},
        {18, "Jetpack Gray"},
        {19, "Explosion White"},
    };
}

std::vector<std::pair<std::string, std::string>> ProfilePicCustomizer::getAvailableFonts() {
    return {
        {"goldFont.fnt", "Gold"},
        {"bigFont.fnt", "Big"},
        {"chatFont.fnt", "Chat"},
        {"GJSHFont_002.png", "Square"},
        {"GJSHFont_003.png", "Square Bold"},
        {"GJSHFont_001.png", "Square Light"},
    };
}
