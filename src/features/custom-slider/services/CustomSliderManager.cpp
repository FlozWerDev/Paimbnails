#include "CustomSliderManager.hpp"
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <Geode/binding/SliderThumb.hpp>
#include <Geode/utils/file.hpp>
#include "../../../utils/AnimatedGIFSprite.hpp"
#include "../../../utils/ImageLoadHelper.hpp"
#include "../../../utils/JsonHelper.hpp"
#include "../../../utils/ShapeStencil.hpp"
#include "../../../utils/EditorContext.hpp"

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::slider;

// ────────────────────────────────────────────────────────────
// Singleton
// ────────────────────────────────────────────────────────────

CustomSliderManager& CustomSliderManager::get() {
    static CustomSliderManager instance;
    return instance;
}

// ────────────────────────────────────────────────────────────
// Persistence
// ────────────────────────────────────────────────────────────

std::filesystem::path CustomSliderManager::configPath() const {
    return Mod::get()->getSaveDir() / "custom-slider.json";
}

std::filesystem::path CustomSliderManager::imagesDir() const {
    auto dir = Mod::get()->getSaveDir() / "slider-images";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

void CustomSliderManager::loadConfig() {
    auto path = configPath();
    auto res = file::readFromJson<matjson::Value>(path);
    if (!res) return;

    auto json = res.unwrap();

    m_config.enabled         = json["enabled"].asBool().unwrapOr(false);
    m_config.thumbMode       = static_cast<SliderThumbMode>(json["thumbMode"].asInt().unwrapOr(0));
    m_config.iconType        = static_cast<SliderIconType>(json["iconType"].asInt().unwrapOr(0));
    m_config.usePlayerIcon   = json["usePlayerIcon"].asBool().unwrapOr(true);
    m_config.customIconId    = json["customIconId"].asInt().unwrapOr(1);
    m_config.iconScale       = static_cast<float>(json["iconScale"].asDouble().unwrapOr(0.55));
    m_config.iconRotation    = static_cast<float>(json["iconRotation"].asDouble().unwrapOr(0.0));
    m_config.iconOpacity     = json["iconOpacity"].asInt().unwrapOr(255);
    m_config.usePlayerColors = json["usePlayerColors"].asBool().unwrapOr(true);
    m_config.enableGlow      = json["enableGlow"].asBool().unwrapOr(false);
    m_config.customImagePath = json["customImagePath"].asString().unwrapOr("");
    m_config.containerEnabled = json["containerEnabled"].asBool().unwrapOr(true);
    m_config.containerShape = json["containerShape"].asString().unwrapOr("circle");
    m_config.containerBorderEnabled = json["containerBorderEnabled"].asBool().unwrapOr(false);
    m_config.containerBorderThickness = static_cast<float>(json["containerBorderThickness"].asDouble().unwrapOr(2.0));
    m_config.animateOnDrag   = json["animateOnDrag"].asBool().unwrapOr(true);
    m_config.animType        = static_cast<SliderAnimType>(json["animType"].asInt().unwrapOr(3));
    m_config.animBounceScale = static_cast<float>(json["animBounceScale"].asDouble().unwrapOr(1.25));
    m_config.animRotateDeg   = static_cast<float>(json["animRotateDeg"].asDouble().unwrapOr(22.0));
    m_config.animDuration    = static_cast<float>(json["animDuration"].asDouble().unwrapOr(0.15));

    if (json.contains("color1") && json["color1"].isArray()) {
        auto arrRes = json["color1"].asArray();
        if (arrRes.isOk()) {
            auto arr = arrRes.unwrap();
            if (arr.size() >= 3) {
                m_config.color1 = {
                    static_cast<GLubyte>(arr[0].asInt().unwrapOr(0)),
                    static_cast<GLubyte>(arr[1].asInt().unwrapOr(255)),
                    static_cast<GLubyte>(arr[2].asInt().unwrapOr(100))
                };
            }
        }
    }
    if (json.contains("color2") && json["color2"].isArray()) {
        auto arrRes = json["color2"].asArray();
        if (arrRes.isOk()) {
            auto arr = arrRes.unwrap();
            if (arr.size() >= 3) {
                m_config.color2 = {
                    static_cast<GLubyte>(arr[0].asInt().unwrapOr(255)),
                    static_cast<GLubyte>(arr[1].asInt().unwrapOr(255)),
                    static_cast<GLubyte>(arr[2].asInt().unwrapOr(255))
                };
            }
        }
    }

    if (json.contains("containerBorderColor") && json["containerBorderColor"].isArray()) {
        auto arrRes = json["containerBorderColor"].asArray();
        if (arrRes.isOk()) {
            auto arr = arrRes.unwrap();
            if (arr.size() >= 3) {
                m_config.containerBorderColor = {
                    static_cast<GLubyte>(arr[0].asInt().unwrapOr(255)),
                    static_cast<GLubyte>(arr[1].asInt().unwrapOr(255)),
                    static_cast<GLubyte>(arr[2].asInt().unwrapOr(255))
                };
            }
        }
    }

    if (json.contains("targets") && json["targets"].isObject()) {
        auto t = json["targets"];
        m_config.targets.optionsSliders = t["optionsSliders"].asBool().unwrapOr(true);
        m_config.targets.editorSliders  = t["editorSliders"].asBool().unwrapOr(true);
        m_config.targets.colorSliders   = t["colorSliders"].asBool().unwrapOr(true);
        m_config.targets.garageSliders  = t["garageSliders"].asBool().unwrapOr(false);
    }
}

void CustomSliderManager::saveConfig() {
    auto json = matjson::Value::object();

    json["enabled"]         = m_config.enabled;
    json["thumbMode"]       = static_cast<int>(m_config.thumbMode);
    json["iconType"]        = static_cast<int>(m_config.iconType);
    json["usePlayerIcon"]   = m_config.usePlayerIcon;
    json["customIconId"]    = m_config.customIconId;
    json["iconScale"]       = static_cast<double>(m_config.iconScale);
    json["iconRotation"]    = static_cast<double>(m_config.iconRotation);
    json["iconOpacity"]     = m_config.iconOpacity;
    json["usePlayerColors"] = m_config.usePlayerColors;
    json["enableGlow"]      = m_config.enableGlow;
    json["customImagePath"] = m_config.customImagePath;
    json["containerEnabled"] = m_config.containerEnabled;
    json["containerShape"] = m_config.containerShape;
    json["containerBorderEnabled"] = m_config.containerBorderEnabled;
    json["containerBorderThickness"] = static_cast<double>(m_config.containerBorderThickness);
    json["animateOnDrag"]   = m_config.animateOnDrag;
    json["animType"]        = static_cast<int>(m_config.animType);
    json["animBounceScale"] = static_cast<double>(m_config.animBounceScale);
    json["animRotateDeg"]   = static_cast<double>(m_config.animRotateDeg);
    json["animDuration"]    = static_cast<double>(m_config.animDuration);

    auto color1Arr = matjson::Value::array();
    color1Arr.push(static_cast<int>(m_config.color1.r));
    color1Arr.push(static_cast<int>(m_config.color1.g));
    color1Arr.push(static_cast<int>(m_config.color1.b));
    json["color1"] = color1Arr;

    auto color2Arr = matjson::Value::array();
    color2Arr.push(static_cast<int>(m_config.color2.r));
    color2Arr.push(static_cast<int>(m_config.color2.g));
    color2Arr.push(static_cast<int>(m_config.color2.b));
    json["color2"] = color2Arr;

    auto cBorderArr = matjson::Value::array();
    cBorderArr.push(static_cast<int>(m_config.containerBorderColor.r));
    cBorderArr.push(static_cast<int>(m_config.containerBorderColor.g));
    cBorderArr.push(static_cast<int>(m_config.containerBorderColor.b));
    json["containerBorderColor"] = cBorderArr;

    auto targets = matjson::Value::object();
    targets["optionsSliders"] = m_config.targets.optionsSliders;
    targets["editorSliders"]  = m_config.targets.editorSliders;
    targets["colorSliders"]   = m_config.targets.colorSliders;
    targets["garageSliders"]  = m_config.targets.garageSliders;
    json["targets"] = targets;

    (void)file::writeToJson(configPath(), json);
}

void CustomSliderManager::resetToDefaults() {
    m_config = CustomSliderConfig{};
    saveConfig();
}

// ────────────────────────────────────────────────────────────
// Icon creation (SimplePlayer mode)
// ────────────────────────────────────────────────────────────

CCNode* CustomSliderManager::createIconNode() {
    auto* gm = GameManager::get();
    if (!gm) return nullptr;

    int iconId = m_config.customIconId;
    IconType gdIconType = IconType::Cube;

    switch (m_config.iconType) {
        case SliderIconType::Cube:   gdIconType = IconType::Cube;   break;
        case SliderIconType::Ship:   gdIconType = IconType::Ship;   break;
        case SliderIconType::Ball:   gdIconType = IconType::Ball;   break;
        case SliderIconType::Ufo:    gdIconType = IconType::Ufo;    break;
        case SliderIconType::Wave:   gdIconType = IconType::Wave;   break;
        case SliderIconType::Robot:  gdIconType = IconType::Robot;  break;
        case SliderIconType::Spider: gdIconType = IconType::Spider; break;
        case SliderIconType::Swing:  gdIconType = IconType::Swing;  break;
    }

    if (m_config.usePlayerIcon) {
        switch (m_config.iconType) {
            case SliderIconType::Cube:   iconId = gm->getPlayerFrame();  break;
            case SliderIconType::Ship:   iconId = gm->getPlayerShip();   break;
            case SliderIconType::Ball:   iconId = gm->getPlayerBall();   break;
            case SliderIconType::Ufo:    iconId = gm->getPlayerBird();   break;
            case SliderIconType::Wave:   iconId = gm->getPlayerDart();   break;
            case SliderIconType::Robot:  iconId = gm->getPlayerRobot();  break;
            case SliderIconType::Spider: iconId = gm->getPlayerSpider(); break;
            case SliderIconType::Swing:  iconId = gm->getPlayerSwing();  break;
        }
    }

    auto* player = SimplePlayer::create(iconId);
    if (!player) return nullptr;

    player->updatePlayerFrame(iconId, gdIconType);

    if (m_config.usePlayerColors) {
        auto col1 = gm->colorForIdx(gm->getPlayerColor());
        auto col2 = gm->colorForIdx(gm->getPlayerColor2());
        player->setColor(col1);
        player->setSecondColor(col2);
        if (gm->getPlayerGlow()) {
            player->setGlowOutline(gm->colorForIdx(gm->getPlayerGlowColor()));
        } else {
            player->disableGlowOutline();
        }
    } else {
        player->setColor(m_config.color1);
        player->setSecondColor(m_config.color2);
        if (m_config.enableGlow) {
            player->setGlowOutline(m_config.color2);
        } else {
            player->disableGlowOutline();
        }
    }

    player->setScale(m_config.iconScale);
    player->setRotation(m_config.iconRotation);

    return player;
}

// ────────────────────────────────────────────────────────────
// Image creation (static PNG/JPG)
// ────────────────────────────────────────────────────────────

// Wraps a raw image/gif sprite inside a CCClippingNode shaped like the
// configured container so the image looks like the profile-pic style
// (circle, square, hexagon, …) instead of floating "alone".
static cocos2d::CCNode* wrapInShapeContainer(
    cocos2d::CCNode* imageNode,
    paimon::slider::CustomSliderConfig const& cfg
) {
    if (!imageNode) return nullptr;
    if (!cfg.containerEnabled) return imageNode;

    using namespace cocos2d;
    using namespace geode::prelude;

    constexpr float kTargetSize = 30.f;

    auto* container = CCNode::create();
    container->setContentSize({kTargetSize, kTargetSize});
    container->setAnchorPoint({0.5f, 0.5f});
    container->ignoreAnchorPointForPosition(false);

    std::string shape = cfg.containerShape.empty() ? std::string("circle") : cfg.containerShape;

    auto* stencil = createShapeStencil(shape, kTargetSize);
    if (!stencil) stencil = createShapeStencil("circle", kTargetSize);
    if (!stencil) {
        // fallback: just return the raw image scaled
        imageNode->setPosition({kTargetSize / 2.f, kTargetSize / 2.f});
        container->addChild(imageNode);
        return container;
    }
    stencil->setPosition({0, 0});

    auto* clipper = CCClippingNode::create();
    clipper->setStencil(stencil);
    clipper->setAlphaThreshold(-1.0f);
    clipper->setContentSize({kTargetSize, kTargetSize});

    // Scale the image to "cover" the target square, then center inside the clip
    float iw = std::max(imageNode->getContentWidth(), 1.f);
    float ih = std::max(imageNode->getContentHeight(), 1.f);
    float coverScale = std::max(kTargetSize / iw, kTargetSize / ih);
    imageNode->setScale(coverScale);
    imageNode->setAnchorPoint({0.5f, 0.5f});
    imageNode->ignoreAnchorPointForPosition(false);
    imageNode->setPosition({kTargetSize / 2.f, kTargetSize / 2.f});
    clipper->addChild(imageNode);
    container->addChild(clipper);

    // Optional border that follows the shape
    if (cfg.containerBorderEnabled) {
        float thick = std::clamp(cfg.containerBorderThickness, 0.5f, 8.f);
        float borderSize = kTargetSize + thick * 2.f;
        if (auto* border = createShapeBorder(shape, borderSize, thick, cfg.containerBorderColor, 255)) {
            border->setAnchorPoint({0.5f, 0.5f});
            border->setPosition({kTargetSize / 2.f, kTargetSize / 2.f});
            container->addChild(border, 5);
        }
    }

    // Apply user-facing scale/rotation/opacity to the wrapper, not the inner image
    container->setScale(cfg.iconScale);
    container->setRotation(cfg.iconRotation);
    return container;
}

CCNode* CustomSliderManager::createImageNode() {
    if (m_config.customImagePath.empty()) return nullptr;

    std::filesystem::path imgPath(m_config.customImagePath);
    std::error_code ec;
    if (!std::filesystem::exists(imgPath, ec)) return nullptr;

    std::string pathStr = geode::utils::string::pathToString(imgPath);

    // Try loading via CCTextureCache
    CCTextureCache::sharedTextureCache()->removeTextureForKey(pathStr.c_str());
    auto* tex = CCTextureCache::sharedTextureCache()->addImage(pathStr.c_str(), false);

    if (!tex) {
        // Fallback: try STB loader
        auto stbResult = ImageLoadHelper::loadWithSTB(imgPath);
        if (!stbResult.success || !stbResult.texture) return nullptr;
        tex = stbResult.texture;
    }

    auto* spr = CCSprite::createWithTexture(tex);
    if (!spr) return nullptr;

    if (m_config.containerEnabled) {
        // Reset scale/rotation: the wrapper handles them on the container
        spr->setScale(1.f);
        spr->setRotation(0.f);
        spr->setOpacity(static_cast<GLubyte>(m_config.iconOpacity));
        return wrapInShapeContainer(spr, m_config);
    }

    // Legacy "raw" mode (no container): scale to ~30 px and apply user tweaks
    float maxDim = std::max(spr->getContentSize().width, spr->getContentSize().height);
    float targetSize = 30.f;
    float baseScale = targetSize / maxDim;
    spr->setScale(baseScale * m_config.iconScale);
    spr->setRotation(m_config.iconRotation);
    spr->setOpacity(static_cast<GLubyte>(m_config.iconOpacity));

    return spr;
}

// ────────────────────────────────────────────────────────────
// GIF creation (animated)
// ────────────────────────────────────────────────────────────

CCNode* CustomSliderManager::createGifNode() {
    if (m_config.customImagePath.empty()) return nullptr;

    std::filesystem::path gifPath(m_config.customImagePath);
    std::error_code ec;
    if (!std::filesystem::exists(gifPath, ec)) return nullptr;

    std::string pathStr = geode::utils::string::pathToString(gifPath);
    auto* gifSpr = AnimatedGIFSprite::create(pathStr);
    if (!gifSpr) {
        // Not a valid GIF, try as static image
        return createImageNode();
    }

    if (m_config.containerEnabled) {
        gifSpr->setScale(1.f);
        gifSpr->setRotation(0.f);
        gifSpr->setOpacity(static_cast<GLubyte>(m_config.iconOpacity));
        return wrapInShapeContainer(gifSpr, m_config);
    }

    // Scale to fit within thumb size
    float maxDim = std::max(gifSpr->getContentSize().width, gifSpr->getContentSize().height);
    if (maxDim > 0.f) {
        float targetSize = 30.f;
        float baseScale = targetSize / maxDim;
        gifSpr->setScale(baseScale * m_config.iconScale);
    } else {
        gifSpr->setScale(m_config.iconScale);
    }
    gifSpr->setRotation(m_config.iconRotation);
    gifSpr->setOpacity(static_cast<GLubyte>(m_config.iconOpacity));

    return gifSpr;
}

// ────────────────────────────────────────────────────────────
// Unified thumb node creation
// ────────────────────────────────────────────────────────────

CCNode* CustomSliderManager::createThumbNode() {
    switch (m_config.thumbMode) {
        case SliderThumbMode::Image:
            return createImageNode();
        case SliderThumbMode::Gif:
            return createGifNode();
        case SliderThumbMode::Icon:
        default:
            return createIconNode();
    }
}

// ────────────────────────────────────────────────────────────
// Slider detection
// ────────────────────────────────────────────────────────────

bool CustomSliderManager::shouldAffectSlider(CCNode* slider) {
    if (!slider) return false;
    if (!slider->getParent()) return false;

    // ── AISLAMIENTO DEL EDITOR (fuente unica de verdad, anti-crash) ──
    // Mientras el editor esta activo, el mod NO debe tocar NINGUN slider nativo
    // (color/HSV en ColorSelectPopup/CustomizeObjectLayer, sliders de triggers,
    // sliders de EditorUI, etc.). Reemplazar setNormalImage/setSelectedImage de
    // sus SliderThumb corrompe el estado que GD lee luego en
    // CustomizeObjectLayer::updateColorSprite al cerrar la popup -> crash.
    //
    // Este guard se basa en la ESCENA en ejecucion, no en typeid de los padres:
    // cuando otro mod hace $modify de ColorSelectPopup/CustomizeObjectLayer el
    // nombre de typeid deja de contener "ColorSelect"/"CustomizeObject" y la
    // exclusion por string fallaba. La unica excepcion es el propio popup de
    // configuracion del mod (CustomSliderPopup), que si debe verse skineado.
    if (paimon::isEditorScene()) {
        for (auto* p = slider->getParent(); p; p = p->getParent()) {
            if (std::string(typeid(*p).name()).find("CustomSliderPopup") != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    // ── EXCLUSION DURA (anti-crash) ──
    // Nunca tocar los sliders nativos del editor de color: ColorSelectPopup,
    // CustomizeObjectLayer y los widgets HSV (ConfigureHSVWidget /
    // HSVWidgetPopup). Al cerrar esas popups, GD reconstruye su estado de
    // color en CustomizeObjectLayer::updateColorSprite leyendo punteros
    // internos (m_colorSprite, canales, etc.). Reemplazar setNormalImage/
    // setSelectedImage del SliderThumb y ocultar sus hijos en ese contexto
    // es de alto riesgo y bajo valor para la feature, y coincide exactamente
    // con la cadena del crash conocido (ColorSelectPopup::closeColorSelect ->
    // CustomizeObjectLayer::updateColorSprite, con betteredit hookeando en
    // medio). Excluimos SIEMPRE estos sliders, incluso bajo el fallback de
    // "todos los targets activos" mas abajo.
    for (auto* p = slider->getParent(); p; p = p->getParent()) {
        auto cn = std::string(typeid(*p).name());
        if (cn.find("CustomizeObject") != std::string::npos ||
            cn.find("ColorSelect")     != std::string::npos ||
            cn.find("ConfigureHSV")    != std::string::npos ||
            cn.find("HSV")             != std::string::npos) {
            return false;
        }
    }

    auto* parent = slider->getParent();
    while (parent) {
        auto className = std::string(typeid(*parent).name());

        // Always affect sliders inside our own popup
        if (className.find("CustomSliderPopup") != std::string::npos) {
            return true;
        }

        if (m_config.targets.optionsSliders) {
            if (className.find("OptionsLayer") != std::string::npos ||
                className.find("MoreOptionsLayer") != std::string::npos ||
                className.find("VideoOptionsLayer") != std::string::npos ||
                className.find("AudioOptionsLayer") != std::string::npos) {
                return true;
            }
        }

        if (m_config.targets.editorSliders) {
            if (className.find("EditorUI") != std::string::npos ||
                className.find("EditorPauseLayer") != std::string::npos ||
                className.find("SetupTrigger") != std::string::npos ||
                className.find("LevelEditorLayer") != std::string::npos) {
                return true;
            }
        }

        // NOTA: el target `colorSliders` queda intencionalmente sin efecto.
        // Los sliders de color/HSV del editor (ColorSelectPopup,
        // CustomizeObjectLayer, widgets HSV) se excluyen de forma dura al
        // inicio de esta funcion por seguridad — ver comentario "EXCLUSION
        // DURA (anti-crash)" arriba. El campo se conserva en la config solo
        // por compatibilidad con saves existentes.

        if (m_config.targets.garageSliders) {
            if (className.find("GJGarageLayer") != std::string::npos ||
                className.find("CharacterColor") != std::string::npos) {
                return true;
            }
        }

        parent = parent->getParent();
    }

    // If all targets are enabled, affect all sliders as fallback
    if (m_config.targets.optionsSliders &&
        m_config.targets.editorSliders &&
        m_config.targets.colorSliders &&
        m_config.targets.garageSliders) {
        return true;
    }

    return false;
}

// ────────────────────────────────────────────────────────────
// Apply / Restore
// ────────────────────────────────────────────────────────────

static const char* kCustomIconID = "paimon-slider-icon";

bool CustomSliderManager::applyCustomThumb(CCNode* sliderThumb) {
    if (!m_config.enabled) return false;
    if (!sliderThumb) return false;

    // Don't double-apply
    if (sliderThumb->getChildByID("paimon-slider-icon"_spr)) {
        auto* existing = sliderThumb->getChildByID("paimon-slider-icon"_spr);
        existing->setScale(m_config.iconScale);
        existing->setRotation(m_config.iconRotation);
        return true;
    }

    // Check if this slider should be affected
    if (!shouldAffectSlider(sliderThumb)) return false;

    // Create the thumb visual
    auto* thumbNode = createThumbNode();
    if (!thumbNode) return false;

    thumbNode->setID("paimon-slider-icon"_spr);

    // Position at center of the thumb
    auto thumbSize = sliderThumb->getContentSize();
    thumbNode->setPosition({thumbSize.width / 2.f, thumbSize.height / 2.f});
    thumbNode->setAnchorPoint({0.5f, 0.5f});

    // ── CRITICAL FIX: Hide the original SliderThumb sprite texture ──
    // SliderThumb inherits from CCMenuItemSpriteExtra which is a CCSprite.
    // We need to make the original sprite texture invisible.
    // The thumb node itself is a sprite — set its texture rect to zero
    // and hide all existing children (the yellow circle + selection sprite).
    if (auto* thumbSprite = typeinfo_cast<CCSprite*>(sliderThumb)) {
        // Make the base sprite invisible by setting opacity to 0
        // but keep the node itself active for touch handling
        thumbSprite->setOpacity(0);

        // Also hide the "selected" sprite if it exists (m_pNormalImage, m_pSelectedImage)
        if (auto* menuItem = typeinfo_cast<CCMenuItemSprite*>(sliderThumb)) {
            if (auto* normalImg = menuItem->getNormalImage()) {
                normalImg->setVisible(false);
            }
            if (auto* selImg = menuItem->getSelectedImage()) {
                selImg->setVisible(false);
            }
        }
    }

    // Hide any pre-existing children (original thumb sprites)
    if (auto* children = sliderThumb->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(children)) {
            if (child && child->getID() != "paimon-slider-icon"_spr) {
                child->setVisible(false);
            }
        }
    }

    sliderThumb->addChild(thumbNode, 10);
    return true;
}

void CustomSliderManager::restoreOriginalThumb(CCNode* sliderThumb) {
    if (!sliderThumb) return;

    // Remove our custom icon
    auto* icon = sliderThumb->getChildByID("paimon-slider-icon"_spr);
    if (icon) {
        icon->removeFromParent();
    }

    // Restore the original sprite opacity
    if (auto* thumbSprite = typeinfo_cast<CCSprite*>(sliderThumb)) {
        thumbSprite->setOpacity(255);

        if (auto* menuItem = typeinfo_cast<CCMenuItemSprite*>(sliderThumb)) {
            if (auto* normalImg = menuItem->getNormalImage()) {
                normalImg->setVisible(true);
            }
            if (auto* selImg = menuItem->getSelectedImage()) {
                selImg->setVisible(true);
            }
        }
    }

    // Restore visibility of original children
    if (auto* children = sliderThumb->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(children)) {
            if (child) {
                child->setVisible(true);
            }
        }
    }
}

// ────────────────────────────────────────────────────────────
// Drag Animations
// ────────────────────────────────────────────────────────────

void CustomSliderManager::startDragAnimation(CCNode* sliderThumb) {
    // Deprecated — animation is now handled directly in the hook
    (void)sliderThumb;
}

void CustomSliderManager::stopDragAnimation(CCNode* sliderThumb) {
    // Deprecated — animation is now handled directly in the hook
    (void)sliderThumb;
}

// ────────────────────────────────────────────────────────────
// addIconToNode — creates the visual and adds it to a base node
// ────────────────────────────────────────────────────────────

void CustomSliderManager::addIconToNode(CCNode* baseNode, bool isSelected) {
    if (!baseNode) return;

    switch (m_config.thumbMode) {
        case SliderThumbMode::Image: {
            auto* node = createImageNode();
            if (node) baseNode->addChild(node);
            break;
        }
        case SliderThumbMode::Gif: {
            auto* node = createGifNode();
            if (node) baseNode->addChild(node);
            break;
        }
        case SliderThumbMode::Icon:
        default: {
            // Create SimplePlayer icon (same approach as RazoomGD's mod)
            auto* gm = GameManager::get();
            if (!gm) return;

            int iconId = m_config.customIconId;
            IconType gdIconType = IconType::Cube;

            switch (m_config.iconType) {
                case SliderIconType::Cube:   gdIconType = IconType::Cube;   break;
                case SliderIconType::Ship:   gdIconType = IconType::Ship;   break;
                case SliderIconType::Ball:   gdIconType = IconType::Ball;   break;
                case SliderIconType::Ufo:    gdIconType = IconType::Ufo;    break;
                case SliderIconType::Wave:   gdIconType = IconType::Wave;   break;
                case SliderIconType::Robot:  gdIconType = IconType::Robot;  break;
                case SliderIconType::Spider: gdIconType = IconType::Spider; break;
                case SliderIconType::Swing:  gdIconType = IconType::Swing;  break;
            }

            if (m_config.usePlayerIcon) {
                switch (m_config.iconType) {
                    case SliderIconType::Cube:   iconId = gm->getPlayerFrame();  break;
                    case SliderIconType::Ship:   iconId = gm->getPlayerShip();   break;
                    case SliderIconType::Ball:   iconId = gm->getPlayerBall();   break;
                    case SliderIconType::Ufo:    iconId = gm->getPlayerBird();   break;
                    case SliderIconType::Wave:   iconId = gm->getPlayerDart();   break;
                    case SliderIconType::Robot:  iconId = gm->getPlayerRobot();  break;
                    case SliderIconType::Spider: iconId = gm->getPlayerSpider(); break;
                    case SliderIconType::Swing:  iconId = gm->getPlayerSwing();  break;
                }
            }

            auto* player = SimplePlayer::create(iconId);
            if (!player) return;

            player->updatePlayerFrame(iconId, gdIconType);

            // Apply colors
            ccColor3B col1, col2;
            if (m_config.usePlayerColors) {
                col1 = gm->colorForIdx(gm->getPlayerColor());
                col2 = gm->colorForIdx(gm->getPlayerColor2());
            } else {
                col1 = m_config.color1;
                col2 = m_config.color2;
            }

            // Lighten colors for selected state (pressed)
            if (isSelected) {
                auto lighten = [](ccColor3B& c) {
                    const float add = 80.f, mul = 0.25f;
                    c.r = std::min(255, (int)c.r - (int)(c.r * mul) + (int)add);
                    c.g = std::min(255, (int)c.g - (int)(c.g * mul) + (int)add);
                    c.b = std::min(255, (int)c.b - (int)(c.b * mul) + (int)add);
                };
                lighten(col1);
                lighten(col2);
            }

            player->setColor(col1);
            player->setSecondColor(col2);

            if (m_config.usePlayerColors) {
                if (gm->getPlayerGlow()) {
                    player->setGlowOutline(gm->colorForIdx(gm->getPlayerGlowColor()));
                } else {
                    player->disableGlowOutline();
                }
            } else {
                if (m_config.enableGlow) {
                    player->setGlowOutline(col2);
                } else {
                    player->disableGlowOutline();
                }
            }

            player->setScale(m_config.iconScale);
            baseNode->addChild(player);
            break;
        }
    }
}
